#include "compiler/backends/cuda/ptx_backend.hpp"
#include <sstream>
namespace ax {
std::string emit_ptx(const Module& m){
  std::ostringstream o;
  o << ".version 8.0\n.target sm_80\n.address_size 64\n";
  bool any=false;
  for(auto &fn: m.fns){
    if(fn.kind!="fn") continue;
    bool is_cuda=false; for(auto &d: fn.decorators) if(d=="cuda") is_cuda=true;
    if(!is_cuda) continue;
    any=true;
    o << ".visible .entry " << fn.name << "(\n";
    o << "  .param .u64 in_ptr,\n  .param .u64 out_ptr,\n  .param .u32 n\n)\n{\n";
    o << "  .reg .pred %p<2>;\n  .reg .f32 %f<8>;\n  .reg .b32 %r<8>;\n  .reg .b64 %rd<8>;\n";
    o << "  .shared .align 16 .b8 smem[4096];\n";
    o << "  mov.u32 %r1, %tid.x;\n  mov.u32 %r2, %ctaid.x;\n  mov.u32 %r3, %ntid.x;\n";
    o << "  mad.lo.u32 %r4, %r2, %r3, %r1;\n";
    o << "  ld.param.u64 %rd1, [in_ptr];\n  ld.param.u64 %rd2, [out_ptr];\n  ld.param.u32 %r5, [n];\n";
    o << "  setp.ge.u32 %p1, %r4, %r5;\n  @%p1 bra DONE;\n";
    o << "  mul.wide.u32 %rd3, %r4, 4;\n  add.u64 %rd4, %rd1, %rd3;\n  add.u64 %rd5, %rd2, %rd3;\n";
    o << "  ld.global.f32 %f1, [%rd4];\n  fma.rn.f32 %f2, %f1, 2.0, 1.0;\n  st.global.f32 [%rd5], %f2;\n";
    o << "  // Tensor Core example: mma.sync for 16x8x16 tile\n";
    o << "  .reg .f16 %h<8>; .reg .f32 %acc<4>;\n";
    o << "  mma.sync.aligned.m16n8k16.row.col.f32.f16.f16.f32 {%acc0,%acc1,%acc2,%acc3}, {%h0,%h1,%h2,%h3}, {%h4,%h5}, {%acc0,%acc1,%acc2,%acc3};\n";
    o << "  bar.sync 0;\n";
    o << "DONE: ret;\n}\n";
  }
  if(!any){
    o << ".visible .entry ax_default(.param .u64 in_ptr, .param .u64 out_ptr, .param .u32 n) { .reg .b32 %r<2>; ret; }\n";
  }
  return o.str();
}
std::string emit_cuda_host(const Module& m){
  std::ostringstream o;
  o << "// CUDA host launcher via dlopen(libcuda.so) — no link-time CUDA dependency\n";
  o << "#include <dlfcn.h>\n#include <cstdint>\n";
  o << "typedef int CUresult; typedef void* CUmodule; typedef void* CUfunction; typedef void* CUstream;\n";
  o << "extern \"C\" int ax_cuda_launch(const char* ptx, const char* entry, const void* in, void* out, unsigned n){\n";
  o << "  void* h = dlopen(\"libcuda.so.1\", RTLD_NOW); if(!h) h = dlopen(\"libcuda.so\", RTLD_NOW); if(!h) return -1;\n";
  o << "  auto cuInit = (CUresult(*)(unsigned))dlsym(h, \"cuInit\"); if(!cuInit) return -2; if(cuInit(0)!=0) return -3;\n";
  o << "  auto cuModuleLoadData = (CUresult(*)(CUmodule*, const void*))dlsym(h, \"cuModuleLoadData\");\n";
  o << "  auto cuModuleGetFunction = (CUresult(*)(CUfunction*, CUmodule, const char*))dlsym(h, \"cuModuleGetFunction\");\n";
  o << "  auto cuLaunchKernel = (CUresult(*)(CUfunction, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, CUstream, void**, void**))dlsym(h, \"cuLaunchKernel\");\n";
  o << "  if(!cuModuleLoadData||!cuModuleGetFunction||!cuLaunchKernel) return -4;\n";
  o << "  CUmodule mod=nullptr; if(cuModuleLoadData(&mod, ptx)!=0) return -5;\n";
  o << "  CUfunction fn=nullptr; if(cuModuleGetFunction(&fn, mod, entry)!=0) return -6;\n";
  o << "  void* args[3]; void* in_p=(void*)in; void* out_p=out; void* n_p=&n; args[0]=&in_p; args[1]=&out_p; args[2]=&n_p;\n";
  o << "  unsigned blocks=(n+255)/256; if(blocks==0) blocks=1;\n";
  o << "  return (int)cuLaunchKernel(fn, blocks, 1, 1, 256, 1, 1, 0, nullptr, args, nullptr);\n}\n";
  (void)m; return o.str();
}
} // namespace ax
