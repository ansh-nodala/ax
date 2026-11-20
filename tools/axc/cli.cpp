#include "tools/axc/cli.hpp"
#include "compiler/common/ax_common.hpp"
#include "compiler/common/source.hpp"
#include "compiler/common/diagnostics.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/sema/sema.hpp"
#include "compiler/ir/mir.hpp"
#include "compiler/opt/fusion.hpp"
#include "compiler/opt/dce.hpp"
#include "compiler/backends/arm64/arm64_backend.hpp"
#include "compiler/backends/x86_64/x86_64_backend.hpp"
#include "compiler/backends/cuda/ptx_backend.hpp"
#include "compiler/backends/rocm/rocm_backend.hpp"
#include "compiler/backends/tpu/stablehlo_backend.hpp"
#include "compiler/backends/vulkan/spirv_backend.hpp"
#include "compiler/backends/vulkan/gpu_kernel.hpp"
#include "compiler/backends/hal/hal.hpp"
#include "runtime/core/axrt.hpp"
#include "runtime/core/asset_cooker.hpp"
#include "runtime/core/vulkan_gpu.hpp"
#include "runtime/coro/coro.hpp"
#include "runtime/dist/dist.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <vector>
#include <unistd.h>
namespace fs = std::filesystem;
namespace ax {
static Module load_and_check(const std::string& path, bool& ok){
  std::string src; if(!read_file(path, src)){ std::cerr<<"axc: cannot read "<<path<<"\n"; ok=false; return {}; }
  std::vector<Diag> diags; Lexer lx; auto toks=lx.tokenize(src, path, diags);
  Parser p; Module m=p.parse(toks, path);
  m.diags.insert(m.diags.end(), diags.begin(), diags.end());
  Sema sema; m=sema.check(std::move(m));
  ok=!has_error(m.diags);
  return m;
}
static MirModule build_mir(Module& m){
  MirBuilder b; MirModule mir=b.build(m);
  mir=fuse_ops(std::move(mir));
  mir=dce_pass(std::move(mir));
  return mir;
}
static std::string exe_dir(){
  char buf[4096]; ssize_t n=readlink("/proc/self/exe", buf, sizeof(buf)-1); if(n>0){ buf[n]=0; return fs::path(buf).parent_path().string(); } return ".";
}
static std::string runtime_archive_path(){
  const fs::path binary_dir=exe_dir();
  const std::vector<fs::path> library_dirs={binary_dir, binary_dir/"../lib", binary_dir/"../lib64"};
  for(const fs::path& directory:library_dirs){
    const fs::path direct=directory/"libaxrt.a";
    if(fs::is_regular_file(direct)) return direct.lexically_normal().string();
    std::error_code error;
    for(fs::directory_iterator entry(directory,error), end; !error && entry!=end; entry.increment(error)){
      if(!entry->is_directory(error) || error) continue;
      const fs::path multiarch=entry->path()/"libaxrt.a";
      if(fs::is_regular_file(multiarch)) return multiarch.lexically_normal().string();
    }
  }
  return {};
}
static std::string shell_quote(const std::string& value){
  std::string quoted="'";
  for(char c: value){ if(c=='\'') quoted += "'\\''"; else quoted += c; }
  quoted += "'";
  return quoted;
}
static bool write_shared_header(const Module& module, const std::string& output_path){
  fs::path header_path(output_path);
  header_path.replace_extension(".h");
  std::ofstream header(header_path);
  if(!header) return false;
  std::string guard="AX_GENERATED_";
  for(unsigned char c: header_path.filename().string()) guard += (std::isalnum(c)?static_cast<char>(std::toupper(c)):'_');
  guard += "_H";
  header<<"#ifndef "<<guard<<"\n#define "<<guard<<"\n\n#include \"ax_runtime.h\"\n\n";
  bool has_export=false;
  for(const Stmt& function: module.fns){
    if(function.kind!="fn" || function.name=="main") continue;
    const bool valid_name=!function.name.empty() &&
      (std::isalpha(static_cast<unsigned char>(function.name.front())) || function.name.front()=='_') &&
      std::all_of(function.name.begin()+1,function.name.end(),[](unsigned char c){ return std::isalnum(c)||c=='_'; });
    if(!valid_name){ std::cerr<<"axc: shared export is not a C identifier: "<<function.name<<"\n"; return false; }
    if(function.params.size()>6){ std::cerr<<"axc: shared exports support at most six AxVal parameters: "<<function.name<<"\n"; return false; }
    header<<"#ifdef __cplusplus\nextern \"C\" {\n#endif\nAxVal* "<<function.name<<"(";
    if(function.params.empty()) header<<"void";
    for(std::size_t i=0;i<function.params.size();++i){
      if(i) header<<", ";
      header<<"AxVal* "<<function.params[i].first;
    }
    header<<");\n#ifdef __cplusplus\n}\n#endif\n";
    has_export=true;
  }
  header<<"\n#endif\n";
  return has_export && static_cast<bool>(header);
}

static bool has_gpu_kernel(const Module& module){
  for(const Stmt& function: module.fns){
    if(function.kind!="fn") continue;
    for(const std::string& decorator: function.decorators) if(decorator=="gpu") return true;
  }
  return false;
}

static bool prepare_gpu_kernels(const Module& module, GpuKernelMap& kernels){
  if(!has_gpu_kernel(module)) return true;
  if(!gpu::vulkan_support_compiled()){
    std::cerr<<"axc: compile-time GPU error: AX was built without Vulkan development support. "
             <<"Install the Vulkan SDK/development package and rebuild AX.\n";
    return false;
  }
  const auto devices=gpu::enumerate_vulkan_gpus();
  if(devices.empty()){
    std::cerr<<"axc: compile-time GPU error: @gpu requires a physical Vulkan compute GPU; none was detected. "
             <<"CPU-only Vulkan drivers do not count. Install a GPU driver or remove @gpu.\n";
    return false;
  }
  if(const char* configured=std::getenv("AX_GPU_DEVICE")){
    char* end=nullptr;
    const unsigned long selected=std::strtoul(configured,&end,10);
    if(end==configured || end==nullptr || *end!='\0' || selected>=devices.size()){
      std::cerr<<"axc: compile-time GPU error: AX_GPU_DEVICE must be a valid index from `ax devices`\n";
      return false;
    }
  }
  std::string error;
  if(!compile_vulkan_kernels(module,kernels,error)){
    std::cerr<<"axc: GPU compile error: "<<error<<"\n";
    return false;
  }
  return true;
}

static int do_build(const std::string& in, const std::string& out, bool shared=false){
  bool ok=false; Module m=load_and_check(in, ok);
  if(!ok){ print_diags(m.diags); return 1; }
  if(shared && has_gpu_kernel(m)){
    std::cerr<<"axc: GPU kernels cannot currently be exported through --shared; build an executable instead\n";
    return 1;
  }
  GpuKernelMap gpu_kernels;
  if(!prepare_gpu_kernels(m,gpu_kernels)) return 1;
  std::string destination=out;
  if(!gpu_kernels.empty() && destination=="/tmp/a.out") destination="/tmp/a.axg";
  MirModule mir=build_mir(m);
  std::string asm_code;
#if defined(__x86_64__) || defined(_M_X64)
  asm_code=emit_x86_64_asm(m, mir, gpu_kernels);
#elif defined(__aarch64__) || defined(_M_ARM64)
  asm_code=emit_arm64_asm(m, mir, gpu_kernels);
#else
  std::cerr<<"axc: native executable builds are currently supported on x86_64 and ARM64 hosts\n";
  return 1;
#endif
  std::string tmp_s = "/tmp/ax_"+fs::path(in).stem().string()+".s";
  if(!write_file(tmp_s, asm_code)){ std::cerr<<"axc: cannot write "<<tmp_s<<"\n"; return 1; }
  std::string lib=runtime_archive_path();
  if(lib.empty()){
    std::cerr<<"axc: cannot find libaxrt.a beside the compiler or under its ../lib directory; "
             <<"reinstall AX with the runtime development archive included\n";
    std::cerr<<"AX assembly kept at "<<tmp_s<<"\n";
    return 1;
  }
  std::string linker;
  if(system("which c++ >/dev/null 2>&1")==0) linker="c++";
  else if(system("which g++ >/dev/null 2>&1")==0) linker="g++";
  else if(system("which clang++ >/dev/null 2>&1")==0) linker="clang++";
  if(linker.empty()){
    std::cerr<<"axc: a C++ linker is required to build programs using libaxrt\n";
    std::cerr<<"AX assembly kept at "<<tmp_s<<"\n";
    return 1;
  }
  std::string link_cmd = linker+" -O2 "+(shared?"-fPIC -shared ":"")+shell_quote(tmp_s)+" ";
  link_cmd += shell_quote(lib)+" ";
  link_cmd += "-o "+shell_quote(destination)+" -lm -ldl -lpthread";
#if AX_HAVE_VULKAN
  if(!gpu_kernels.empty()) link_cmd += " -lvulkan";
#endif
  if(shared) link_cmd += " -Wl,--no-undefined";
  int rc=system(link_cmd.c_str());
  if(rc==0){
    if(shared && !write_shared_header(m,destination)){ std::cerr<<"axc: shared object linked but generated header could not be written\n"; return 1; }
    std::cout<<(shared?"AX shared library compiled: ":(!gpu_kernels.empty()?"AX GPU executable compiled: ":"AX compiled: "))<<in<<" -> "<<destination<<"\n";
    if(shared){ fs::path header(destination); header.replace_extension(".h"); std::cout<<"C header: "<<header.string()<<"\n"; }
    return 0;
  }
  std::cerr<<"axc: native link failed, assembly kept at "<<tmp_s<<"\n";
  return 1;
}
int axc_main(int argc, char** argv){
  register_builtin_backends();
  if(argc<2){ std::cout<<"axc 2.0.0 — AX native compiler\nUsage: axc <build|run|check|devices|emit-ir|emit-asm|emit-ptx|emit-spirv|emit-stablehlo|emit-rocm|cook|test|bench> [file] [-o out]\n"; return 0; }
  std::string cmd=argv[1];
  if(cmd=="--help"||cmd=="-h"||cmd=="help"){ std::cout<<"ax build <f.ax> -o <out> [--shared]\nax run <f.ax>\nax check <f.ax>\nax edit <file.ax>\nax devices\nax emit-ir|emit-asm|emit-ptx|emit-spirv|emit-stablehlo|emit-rocm <f.ax>\nax cook <asset.obj|asset.wav|asset.png> -o <bundle.axb>\nax test\nax bench\n"; return 0; }
  if(cmd=="edit"){
    const std::string editor=exe_dir()+"/axedit";
    std::vector<char*> editor_args;
    editor_args.reserve(static_cast<std::size_t>(argc));
    editor_args.push_back(const_cast<char*>(editor.c_str()));
    for(int index=2;index<argc;++index) editor_args.push_back(argv[index]);
    editor_args.push_back(nullptr);
    execv(editor.c_str(),editor_args.data());
    std::cerr<<"ax: could not start the AX editor at "<<editor
             <<"; rebuild the project to create axedit\n";
    return 127;
  }
  if(cmd=="devices"){
    if(!gpu::vulkan_support_compiled()){
      std::cout<<"This AX build has no Vulkan support. Install Vulkan development files and rebuild to detect GPUs.\n";
      return 0;
    }
    const auto devices=gpu::enumerate_vulkan_gpus();
    if(devices.empty()){
      std::cout<<"No Vulkan compute GPU detected. @gpu compilation requires a physical GPU; CPU-only drivers are ignored.\n";
      return 0;
    }
    std::cout<<"Vulkan compute GPUs detected: "<<devices.size()<<"\n";
    for(std::size_t index=0;index<devices.size();++index){
      const auto& device=devices[index];
      std::cout<<"  ["<<index<<"] "<<device.name<<" (vendor 0x"<<std::hex<<device.vendor_id
               <<", device 0x"<<device.device_id<<std::dec<<")\n";
    }
    std::cout<<"@gpu uses Vulkan SPIR-V; the vendor driver selects device-specific machine code.\n";
    return 0;
  }
  if(cmd=="cook"){
    if(argc<3){ std::cerr<<"axc cook: missing input asset\n"; return 1; }
    std::string output;
    for(int i=3;i<argc;i++) if(std::string(argv[i])=="-o" && i+1<argc) output=argv[++i];
    if(output.empty()){ std::cerr<<"axc cook: specify -o <bundle.axb>\n"; return 1; }
    asset::CookStats stats; std::string error;
    if(!asset::cook_asset_file(argv[2], output, stats, error)){
      std::cerr<<"axc cook: "<<error<<"\n"; return 1;
    }
    const char* kind=stats.kind==asset::Kind::Mesh?"mesh":stats.kind==asset::Kind::Audio?"audio":"png-image";
    std::cout<<"Cooked "<<kind<<": "<<stats.source_bytes<<" -> "<<stats.output_bytes<<" bytes";
    if(stats.kind==asset::Kind::Mesh) std::cout<<", "<<stats.vertices<<" vertices, "<<stats.triangles<<" triangles, "<<stats.meshlets<<" meshlets";
    std::cout<<"\n"; return 0;
  }
  if(cmd=="test"){ std::cout<<"Run ./build/ax_tests\n"; return system("./build/ax_tests"); }
  if(cmd=="bench"){
    // coro bench: 100k coroutines
    CoroScheduler sched(4);
    for(int i=0;i<100000;i++) sched.spawn([i]{ volatile int x=i*2; (void)x; });
    sched.run();
    std::cout<<"coro bench: completed "<<sched.completed()<<" coroutines\n";
    // matmul bench via runtime
    double a[4]={1,2,3,4}; AxVal* av=ax_tensor_vals(a,4); AxVal* bv=ax_tensor_vals(a,4); AxVal* r=ax_matmul(av,bv); std::cout<<"matmul bench: dot="<<ax_to_double(r)<<"\n";
    std::cout<<"cores: "<<ax_topology_cores()<<" neon: "<<ax_topology_has_neon()<<" sve: "<<ax_topology_has_sve()<<"\n";
    return 0;
  }
  if(cmd=="build"){
    std::string file, out="/tmp/a.out";
    bool shared=false;
    for(int i=2;i<argc;i++){
      const std::string arg=argv[i];
      if(arg=="--shared") shared=true;
      else if(arg=="-o" && i+1<argc) out=argv[++i];
      else if(!arg.empty() && arg[0]=='-'){ std::cerr<<"axc: unknown build option "<<arg<<"\n"; return 1; }
      else if(file.empty()) file=arg;
      else { std::cerr<<"axc: unexpected build argument "<<arg<<"\n"; return 1; }
    }
    if(file.empty()){ std::cerr<<"axc: missing input file\n"; return 1; }
    if(shared && out=="/tmp/a.out") out="/tmp/libax_program.so";
    return do_build(file,out,shared);
  }
  if(argc<3){ std::cerr<<"axc: missing file\n"; return 1; }
  std::string file=argv[2];
  std::string out="/tmp/a.out";
  for(int i=3;i<argc;i++){ if(std::string(argv[i])=="-o" && i+1<argc) out=argv[i+1]; }
  if(cmd=="run"){ int rc=do_build(file, "/tmp/ax_run"); if(rc!=0) return rc; std::cout.flush(); return system("/tmp/ax_run"); }
  if(cmd=="check"){
    bool ok=false; Module m=load_and_check(file, ok);
    if(!ok){ print_diags(m.diags); return 1; }
    GpuKernelMap gpu_kernels;
    if(!prepare_gpu_kernels(m,gpu_kernels)) return 1;
    std::cout<<"check OK: "<<file;
    if(!gpu_kernels.empty()) std::cout<<" (Vulkan GPU kernels compiled)";
    std::cout<<"\n"; return 0;
  }
  bool ok=false; Module m=load_and_check(file, ok);
  if(!ok && cmd!="emit-ir"){ print_diags(m.diags); if(cmd=="check") return 1; }
  if(cmd=="emit-ir"){ MirModule mir=build_mir(m); for(auto &f: mir.funcs){ std::cout<<"func "<<f.name<<":\n"; for(auto &in: f.insts) std::cout<<"  "<<in.op<<" "<<in.dst<<" = "<<in.a<<" "<<in.b<<" "<<in.str<<"\n"; } return 0; }
  if(cmd=="emit-asm"){
    GpuKernelMap gpu_kernels;
    if(!prepare_gpu_kernels(m,gpu_kernels)) return 1;
    MirModule mir=build_mir(m); std::cout<<emit_arm64_asm(m, mir,gpu_kernels); return 0;
  }
  if(cmd=="emit-ptx"){ std::cout<<emit_ptx(m); std::cout<<"\n// host launcher:\n"<<emit_cuda_host(m)<<"\n"; return 0; }
  if(cmd=="emit-rocm"){ std::cout<<emit_rocm_hip(m); return 0; }
  if(cmd=="emit-stablehlo"){ std::cout<<emit_stablehlo(m); return 0; }
  if(cmd=="emit-spirv"){
    GpuKernelMap gpu_kernels;
    if(!prepare_gpu_kernels(m,gpu_kernels)) return 1;
    auto words=gpu_kernels.empty()?emit_spirv_binary("main"):gpu_kernels.begin()->second.spirv;
    std::string err; if(!validate_spirv(words, err)){ std::cerr<<"spirv invalid: "<<err<<"\n"; return 1; }
    if(!write_spirv_file(out, words)){ std::cerr<<"cannot write "<<out<<"\n"; return 1; }
    std::cout<<"SPIR-V written to "<<out<<" ("<<words.size()*4<<" bytes, magic 0x07230203)\n"; return 0;
  }
  std::cerr<<"axc: unknown command "<<cmd<<"\n"; return 1;
}
} // namespace ax
