#include "compiler/backends/arm64/arm64_backend.hpp"
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
namespace ax {
struct AsmGen {
  std::ostringstream out;
  std::vector<std::pair<std::string,double>> dconsts;
  std::vector<std::pair<std::string,std::string>> sconsts;
  std::vector<std::pair<std::string,std::vector<double>>> arrconsts;
  std::map<std::string,int> var_off;
  int label_ctr=0;
  std::string fresh(const std::string& p){ return ".L"+p+"_"+std::to_string(label_ctr++); }
  std::string dlabel(double v){ for(auto &p: dconsts) if(p.second==v) return p.first; std::string l=".Ld_"+std::to_string(dconsts.size()); dconsts.push_back({l,v}); return l; }
  std::string slabel(const std::string& s){ for(auto &p: sconsts) if(p.second==s) return p.first; std::string l=".Ls_"+std::to_string(sconsts.size()); sconsts.push_back({l,s}); return l; }
  std::string arrlabel(const std::vector<double>& a){ std::string l=".La_"+std::to_string(arrconsts.size()); arrconsts.push_back({l,a}); return l; }
  int var_offset(const std::string& n){ auto it=var_off.find(n); if(it!=var_off.end()) return it->second; int off = -16 - (int)var_off.size()*8; var_off[n]=off; return off; }
  void collect(const std::vector<Stmt>& stmts){
    for(auto &s: stmts){
      if(s.kind=="assign") var_offset(s.name);
      if(s.kind=="for") var_offset(s.loop_var);
      if(s.kind=="fn") for(auto &p: s.params) var_offset(p.first);
      collect(s.body); collect(s.else_body);
    }
  }
  void emit_epilogue(int stack, bool is_main){
    (void)is_main;
    out << "    add sp, sp, #" << stack << "\n";
    out << "    ldp x29, x30, [sp], #16\n";
    out << "    ret\n";
  }
  void emit_expr(const Expr& e, const std::set<std::string>& int_vars, const std::string& cur_fn);
  void emit_stmt(const Stmt& s, const std::set<std::string>& int_vars_in, const std::string& cur_fn, bool is_main, int stack);
};
void AsmGen::emit_expr(const Expr& e, const std::set<std::string>& int_vars, const std::string& cur_fn){
  (void)cur_fn;
  if(e.kind=="num"){
    std::string L=dlabel(e.number);
    out << "    adrp x0, " << L << "\n    add x0, x0, :lo12:" << L << "\n    ldr d0, [x0]\n    bl ax_scalar\n";
    return;
  }
  if(e.kind=="str"){
    std::string L=slabel(e.str);
    out << "    adrp x0, " << L << "\n    add x0, x0, :lo12:" << L << "\n    bl ax_string\n";
    return;
  }
  if(e.kind=="var"){
    auto it=var_off.find(e.name);
    int off = (it==var_off.end()? -16 : it->second);
    if(int_vars.count(e.name)){
      out << "    ldr x0, [x29, #" << off << "]\n    scvtf d0, x0\n    bl ax_scalar\n";
    } else {
      out << "    ldr x0, [x29, #" << off << "]\n";
    }
    return;
  }
  if(e.kind=="list"){
    bool all_num=true; std::vector<double> vals;
    for(auto &a: e.args){ if(a.kind!="num") all_num=false; else vals.push_back(a.number); }
    if(all_num){
      std::string L=arrlabel(vals);
      out << "    adrp x0, " << L << "\n    add x0, x0, :lo12:" << L << "\n    mov x1, #" << vals.size() << "\n    bl ax_tensor_vals\n";
      return;
    }
    // generic: build on stack
    out << "    sub sp, sp, #64\n";
    for(std::size_t i=0;i<e.args.size();++i){
      emit_expr(e.args[i], int_vars, cur_fn);
      out << "    bl ax_to_double\n    str d0, [sp, #" << (i*8) << "]\n";
    }
    out << "    mov x0, sp\n    mov x1, #" << e.args.size() << "\n    bl ax_tensor_vals\n    add sp, sp, #64\n";
    return;
  }
  if(e.kind=="call"){
    if(e.name=="print" || e.name=="println"){
      if(!e.args.empty()) emit_expr(e.args[0], int_vars, cur_fn);
      else out << "    adrp x0, .Ls_empty\n    add x0, x0, :lo12:.Ls_empty\n    bl ax_string\n";
      out << "    bl ax_print\n";
      // return dummy scalar 0 for expression context
      out << "    fmov d0, #0.0\n    bl ax_scalar\n";
      return;
    }
    if(e.name=="tensor" && !e.args.empty()){
      emit_expr(e.args[0], int_vars, cur_fn); return;
    }
    if(e.name=="vec4"){
      // evaluate 4 args, push each
      for(int i=0;i<4;++i){
        if(i < (int)e.args.size()) emit_expr(e.args[i], int_vars, cur_fn);
        else out << "    fmov d0, #0.0\n    bl ax_scalar\n";
        out << "    str x0, [sp, #-16]!\n";
      }
      out << "    ldr x3, [sp], #16\n    ldr x2, [sp], #16\n    ldr x1, [sp], #16\n    ldr x0, [sp], #16\n    bl ax_vec4\n";
      return;
    }
    if(e.name=="len"){
      if(!e.args.empty()) emit_expr(e.args[0], int_vars, cur_fn);
      out << "    bl ax_len\n"; return;
    }
    if(e.name=="matmul" || e.name=="gemm" || e.name=="softmax" || e.name=="relu" || e.name=="layernorm" || e.name=="rmsnorm" || e.name=="rope" || e.name=="swiglu" || e.name=="geglu" || e.name=="sigmoid" || e.name=="tanh" || e.name=="gelu" || e.name=="silu" || e.name=="mse_loss" || e.name=="cross_entropy"){
      std::string fn = "ax_" + e.name;
      if(e.name=="rope"&&e.args.size()==2) fn="ax_rope_offset";
      if(e.name=="swiglu"&&e.args.size()==2) fn="ax_swiglu2";
      if(e.name=="geglu") fn=e.args.size()==2?"ax_geglu":"ax_geglu_self";
      if(e.name=="rope"&&e.args.size()==4){
        for(auto &arg:e.args){ emit_expr(arg,int_vars,cur_fn); out << "    str x0, [sp, #-16]!\n"; }
        out << "    ldr x3, [sp], #16\n    ldr x2, [sp], #16\n    ldr x1, [sp], #16\n    ldr x0, [sp], #16\n    bl ax_rope_ex\n";
        return;
      }
      if(e.args.size()==2){
        emit_expr(e.args[0], int_vars, cur_fn); out << "    str x0, [sp, #-16]!\n";
        emit_expr(e.args[1], int_vars, cur_fn); out << "    mov x1, x0\n    ldr x0, [sp], #16\n    bl " << fn << "\n"; return;
      } else if(e.args.size()==1){
        emit_expr(e.args[0], int_vars, cur_fn); out << "    bl " << fn << "\n"; return;
      } else if(e.args.size()==0){
        out << "    mov x0, #0\n    bl " << fn << "\n"; return;
      }
    }
    // generic user function: up to 4 args
    if(e.args.empty()){
      out << "    bl " << e.name << "\n"; return;
    }
    if(e.args.size()==1){ emit_expr(e.args[0], int_vars, cur_fn); out << "    bl " << e.name << "\n"; return; }
    // N args: evaluate left to right pushing, then pop in reverse
    for(auto &a: e.args){ emit_expr(a, int_vars, cur_fn); out << "    str x0, [sp, #-16]!\n"; }
    if(e.args.size()==2){ out << "    ldr x1, [sp], #16\n    ldr x0, [sp], #16\n"; }
    else if(e.args.size()==3){ out << "    ldr x2, [sp], #16\n    ldr x1, [sp], #16\n    ldr x0, [sp], #16\n"; }
    else if(e.args.size()>=4){ out << "    ldr x3, [sp], #16\n    ldr x2, [sp], #16\n    ldr x1, [sp], #16\n    ldr x0, [sp], #16\n"; }
    out << "    bl " << e.name << "\n"; return;
  }
  if(e.kind=="binary" && e.left && e.right){
    emit_expr(*e.left, int_vars, cur_fn);
    out << "    str x0, [sp, #-16]!\n";
    emit_expr(*e.right, int_vars, cur_fn);
    out << "    mov x1, x0\n    ldr x0, [sp], #16\n";
    std::string op="ax_add";
    if(e.op=="-") op="ax_sub"; else if(e.op=="*") op="ax_mul"; else if(e.op=="/") op="ax_div"; else if(e.op=="%") op="ax_mod";
    else if(e.op=="==") op="ax_eq"; else if(e.op=="!=") op="ax_ne"; else if(e.op=="<") op="ax_lt"; else if(e.op==">") op="ax_gt"; else if(e.op=="<=") op="ax_le"; else if(e.op==">=") op="ax_ge";
    out << "    bl " << op << "\n"; return;
  }
  out << "    fmov d0, #0.0\n    bl ax_scalar\n";
}
void AsmGen::emit_stmt(const Stmt& s, const std::set<std::string>& int_vars_in, const std::string& cur_fn, bool is_main, int stack){
  std::set<std::string> int_vars = int_vars_in;
  // collect loop vars in this stmt subtree for int_vars
  if(s.kind=="for") int_vars.insert(s.loop_var);
  if(s.kind=="assign"){
    int off = var_offset(s.name);
    // print statement is expr, not assign
    emit_expr(s.expr, int_vars, cur_fn);
    out << "    str x0, [x29, #" << off << "]\n";
    return;
  }
  if(s.kind=="expr"){
    if(s.expr.kind=="call" && (s.expr.name=="print" || s.expr.name=="println")){
      if(!s.expr.args.empty()) emit_expr(s.expr.args[0], int_vars, cur_fn);
      else { std::string L=slabel(""); out << "    adrp x0, " << L << "\n    add x0, x0, :lo12:" << L << "\n    bl ax_string\n"; }
      out << "    bl ax_print\n";
    } else {
      emit_expr(s.expr, int_vars, cur_fn);
    }
    return;
  }
  if(s.kind=="for"){
    int loop_off = var_offset(s.loop_var);
    // end slot: use a temp var
    std::string end_var = "__end_"+s.loop_var;
    int end_off = var_offset(end_var);
    // range: call range(a) or range(a,b) or var/num
    Expr start_e; start_e.kind="num"; start_e.number=0;
    Expr end_e; end_e.kind="num"; end_e.number=0;
    if(s.range_expr.kind=="call" && s.range_expr.name=="range"){
      if(s.range_expr.args.size()==1){ end_e=s.range_expr.args[0]; }
      else if(s.range_expr.args.size()>=2){ start_e=s.range_expr.args[0]; end_e=s.range_expr.args[1]; }
    } else { end_e=s.range_expr; }
    // start
    emit_expr(start_e, int_vars, cur_fn);
    out << "    bl ax_to_double\n    fcvtzs x0, d0\n    str x0, [x29, #" << loop_off << "]\n";
    // end
    emit_expr(end_e, int_vars, cur_fn);
    out << "    bl ax_to_double\n    fcvtzs x0, d0\n    str x0, [x29, #" << end_off << "]\n";
    std::string Ltop=fresh("for"); std::string Lend=fresh("forend");
    out << Ltop << ":\n";
    out << "    ldr x0, [x29, #" << loop_off << "]\n    ldr x1, [x29, #" << end_off << "]\n    cmp x0, x1\n    bge " << Lend << "\n";
    std::set<std::string> child_int = int_vars; child_int.insert(s.loop_var);
    for(auto &b: s.body) emit_stmt(b, child_int, cur_fn, is_main, stack);
    out << "    ldr x0, [x29, #" << loop_off << "]\n    add x0, x0, #1\n    str x0, [x29, #" << loop_off << "]\n    b " << Ltop << "\n";
    out << Lend << ":\n";
    return;
  }
  if(s.kind=="parallel_for"){
    // same as for, with comment for maxpower thread dispatch
    out << "    // parallel_for: dispatched via axrt thread pool\n";
    Stmt f2=s; f2.kind="for"; emit_stmt(f2, int_vars, cur_fn, is_main, stack); return;
  }
  if(s.kind=="if"){
    emit_expr(s.cond, int_vars, cur_fn);
    out << "    bl ax_truthy\n    cmp w0, #0\n";
    std::string Lelse=fresh("else"); std::string Lend=fresh("endif");
    out << "    beq " << Lelse << "\n";
    for(auto &b: s.body) emit_stmt(b, int_vars, cur_fn, is_main, stack);
    out << "    b " << Lend << "\n";
    out << Lelse << ":\n";
    for(auto &b: s.else_body) emit_stmt(b, int_vars, cur_fn, is_main, stack);
    out << Lend << ":\n";
    return;
  }
  if(s.kind=="while"){
    std::string Ltop=fresh("while"); std::string Lend=fresh("whileend");
    out << Ltop << ":\n";
    emit_expr(s.cond, int_vars, cur_fn);
    out << "    bl ax_truthy\n    cmp w0, #0\n    beq " << Lend << "\n";
    for(auto &b: s.body) emit_stmt(b, int_vars, cur_fn, is_main, stack);
    out << "    b " << Ltop << "\n" << Lend << ":\n";
    return;
  }
  if(s.kind=="return"){
    if(is_main){
      if(s.expr.kind!=""){ emit_expr(s.expr, int_vars, cur_fn); out << "    bl ax_to_double\n    fcvtzs w0, d0\n"; }
      else out << "    mov w0, #0\n";
      emit_epilogue(stack, is_main); return;
    } else {
      if(s.expr.kind!="") emit_expr(s.expr, int_vars, cur_fn);
      else { out << "    fmov d0, #0.0\n    bl ax_scalar\n"; }
      emit_epilogue(stack, is_main); return;
    }
  }
  if(s.kind=="load"){ out << "    // load " << s.extra << "\n"; return; }
  // generic block: distribute, shard, coro, spawn, etc.
  if(!s.body.empty() || !s.else_body.empty()){
    out << "    // " << s.kind << " block\n";
    for(auto &b: s.body) emit_stmt(b, int_vars, cur_fn, is_main, stack);
    for(auto &b: s.else_body) emit_stmt(b, int_vars, cur_fn, is_main, stack);
    return;
  }
  if(s.expr.kind!=""){ emit_expr(s.expr, int_vars, cur_fn); return; }
}
std::string emit_arm64_asm(const Module& m, const MirModule& mir,
                           const GpuKernelMap& gpu_kernels){
  (void)mir;
  AsmGen g;
  // pre-collect rodata for empty string
  g.slabel("");
  std::ostringstream head;
  head << "    .text\n    .global main\n";
  std::string body;
  for(auto &fn: m.fns){
    if(fn.kind!="fn") continue;
    AsmGen fg;
    fg.slabel("");
    // copy global consts? each function gets its own gen, merged later is complex - use single gen for all
    (void)fg;
  }
  // Use single gen for whole module
  AsmGen& gen = g;
  std::ostringstream out;
  out << "    .text\n";
  // emit each function
  for(auto &fn: m.fns){
    if(fn.kind!="fn") continue;
    gen.var_off.clear();
    gen.collect(fn.body);
    // params also
    for(auto &p: fn.params) gen.var_offset(p.first);
    int nvars = (int)gen.var_off.size();
    int stack = ((nvars*8 + 64 + 15)/16)*16;
    bool is_main = (fn.name=="main");
    out << "    .global " << fn.name << "\n" << fn.name << ":\n";
    out << "    stp x29, x30, [sp, #-16]!\n    mov x29, sp\n    sub sp, sp, #" << stack << "\n";
    // store params: x0..x3
    for(std::size_t pi=0; pi<fn.params.size() && pi<4; ++pi){
      int off = gen.var_off[fn.params[pi].first];
      out << "    str x" << pi << ", [x29, #" << off << "]\n";
    }
    const auto gpu_kernel = gpu_kernels.find(fn.name);
    if(gpu_kernel != gpu_kernels.end()){
      const std::size_t input_area = ((fn.params.size() * sizeof(void*) + 15) / 16) * 16;
      out << "    sub sp, sp, #" << input_area << "\n";
      for(std::size_t index=0;index<fn.params.size();++index){
        out << "    ldr x9, [x29, #" << gen.var_off[fn.params[index].first] << "]\n"
            << "    str x9, [sp, #" << index*sizeof(void*) << "]\n";
      }
      out << "    adrp x0, .Lax_gpu_kernel_" << fn.name << "\n"
          << "    add x0, x0, :lo12:.Lax_gpu_kernel_" << fn.name << "\n"
          << "    ldr x1, =" << gpu_kernel->second.spirv.size() << "\n"
          << "    mov x2, sp\n"
          << "    mov x3, #" << fn.params.size() << "\n"
          << "    adrp x4, .Lax_gpu_config_" << fn.name << "\n"
          << "    add x4, x4, :lo12:.Lax_gpu_config_" << fn.name << "\n"
          << "    bl ax_vulkan_run_spirv\n"
          << "    add sp, sp, #" << input_area << "\n"
          << "    add sp, sp, #" << stack << "\n"
          << "    ldp x29, x30, [sp], #16\n"
          << "    ret\n";
      continue;
    }
    // emit body using gen's out buffer: redirect
    // We use gen.out as scratch; capture its size
    std::size_t before = gen.out.str().size();
    (void)before;
    // Temporarily swap: emit into gen.out then append
    std::ostringstream saved;
    // Instead, emit directly via gen helper by using gen.out
    // Clear gen.out for this function and emit, then append
    gen.out.str(""); gen.out.clear();
    std::set<std::string> int_vars;
    for(auto &st: fn.body) gen.emit_stmt(st, int_vars, fn.name, is_main, stack);
    std::string fbody = gen.out.str();
    out << fbody;
    // fallthrough epilogue if no return emitted (heuristic: if body does not end with return, emit)
    bool has_ret = false; for(auto &st: fn.body) if(st.kind=="return") has_ret=true;
    if(!has_ret){
      if(is_main) out << "    mov w0, #0\n";
      else { out << "    fmov d0, #0.0\n    bl ax_scalar\n"; }
      out << "    add sp, sp, #" << stack << "\n    ldp x29, x30, [sp], #16\n    ret\n";
    }
    // merge rodata from gen into global gen? gen is the same object, so dconsts/sconsts/arrconsts accumulate
  }
  std::string text = out.str();
  std::ostringstream ro;
  ro << "    .section .rodata\n";
  for(auto &p: gen.dconsts){ ro << p.first << ":\n    .double " << p.second << "\n"; }
  for(auto &p: gen.sconsts){
    ro << p.first << ":\n    .asciz \"";
    for(char c: p.second){ if(c=='"') ro << "\\\""; else if(c=='\\') ro << "\\\\"; else if(c=='\n') ro << "\\n"; else ro << c; }
    ro << "\"\n";
  }
  for(auto &p: gen.arrconsts){
    ro << p.first << ":\n";
    for(double v: p.second) ro << "    .double " << v << "\n";
  }
  for(const auto &entry: gpu_kernels){
    const VulkanKernelConfig& config=entry.second.config;
    ro << "    .p2align 2\n.Lax_gpu_config_" << entry.first << ":\n"
       << "    .long " << static_cast<std::uint32_t>(config.kind) << "\n"
       << "    .long " << config.input_count << "\n"
       << "    .long " << config.output_count << "\n"
       << "    .long " << config.rows << "\n"
       << "    .long " << config.columns << "\n"
       << "    .long " << config.inner << "\n"
       << "    .long " << config.local_x << "\n"
       << "    .long " << config.local_y << "\n"
       << "    .p2align 2\n.Lax_gpu_kernel_" << entry.first << ":\n";
    for(std::uint32_t word: entry.second.spirv){
      ro << "    .long 0x" << std::hex << std::setw(8) << std::setfill('0') << word
         << std::dec << std::setfill(' ') << "\n";
    }
  }
  return text + ro.str();
}
} // namespace ax
