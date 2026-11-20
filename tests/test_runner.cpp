#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "compiler/common/ax_common.hpp"
#include "compiler/common/diagnostics.hpp"
#include "compiler/lexer/lexer.hpp"
#include "compiler/parser/parser.hpp"
#include "compiler/sema/sema.hpp"
#include "compiler/ir/mir.hpp"
#include "compiler/backends/vulkan/spirv_backend.hpp"
#include "compiler/backends/cuda/ptx_backend.hpp"
#include "compiler/backends/tpu/stablehlo_backend.hpp"
#include "compiler/backends/arm64/arm64_backend.hpp"
#include "runtime/coro/coro.hpp"
#include "runtime/dist/dist.hpp"
using namespace ax;
static int passed=0, failed=0;
static void check(bool cond, const std::string& name){ if(cond){ passed++; std::cout<<"PASS "<<name<<"\n"; } else { failed++; std::cout<<"FAIL "<<name<<"\n"; } }
int main(){
  // lexer
  { Lexer lx; std::vector<Diag> d; auto toks=lx.tokenize("fn main() -> i32:\n    print(1)\n", "t.ax", d); check(toks.size()>5, "lexer token count"); check(d.empty(), "lexer no diags"); }
  // parser fn
  { Lexer lx; std::vector<Diag> d; auto toks=lx.tokenize("fn main() -> i32:\n    x: f32 = 2.0\n    print(x)\n    return 0\n", "t.ax", d); Parser p; auto m=p.parse(toks, "t.ax"); check(m.fns.size()==1 && m.fns[0].name=="main", "parser fn main"); }
  // parser struct/enum/trait
  { Lexer lx; std::vector<Diag> d; auto toks=lx.tokenize("struct Vec:\n    x: f32\n", "t.ax", d); Parser p; auto m=p.parse(toks, "t.ax"); check(m.fns.size()==1 && m.fns[0].kind=="struct", "parser struct"); }
  // shape checker rejects mismatch
  { std::string src="fn main() -> i32:\n    a: Tensor[f32, 2, 3] = tensor([1.0, 2.0])\n    b: Tensor[f32, 4, 5] = tensor([1.0, 2.0])\n    c = matmul(a, b)\n    print(c)\n    return 0\n"; Lexer lx; std::vector<Diag> d; auto toks=lx.tokenize(src, "bad.ax", d); Parser p; auto m=p.parse(toks, "bad.ax"); Sema s; m=s.check(std::move(m)); check(has_error(m.diags), "shape checker rejects matmul mismatch"); }
  // shape checker accepts matching
  { std::string src="fn main() -> i32:\n    a: Tensor[f32, 2, 3] = tensor([1.0, 2.0])\n    b: Tensor[f32, 3, 4] = tensor([1.0, 2.0])\n    c = matmul(a, b)\n    print(c)\n    return 0\n"; Lexer lx; std::vector<Diag> d; auto toks=lx.tokenize(src, "good.ax", d); Parser p; auto m=p.parse(toks, "good.ax"); Sema s; m=s.check(std::move(m)); check(!has_error(m.diags), "shape checker accepts matching matmul"); }
  // MIR
  { Lexer lx; std::vector<Diag> d; auto toks=lx.tokenize("fn main() -> i32:\n    x = 1.0 + 2.0\n    return 0\n", "t.ax", d); Parser p; auto m=p.parse(toks, "t.ax"); MirBuilder b; auto mir=b.build(m); check(!mir.funcs.empty() && !mir.funcs[0].insts.empty(), "mir builder"); }
  // ARM64 asm
  { Lexer lx; std::vector<Diag> d; auto toks=lx.tokenize("fn main() -> i32:\n    print(\"hi\")\n    return 0\n", "t.ax", d); Parser p; auto m=p.parse(toks, "t.ax"); MirBuilder b; auto mir=b.build(m); auto asm_code=emit_arm64_asm(m, mir); check(asm_code.find("ax_print")!=std::string::npos && asm_code.find("stp x29")!=std::string::npos, "arm64 asm prologue+print"); }
  // PTX
  { Lexer lx; std::vector<Diag> d; auto toks=lx.tokenize("@cuda\nfn k(x: Tensor[f32, N]) -> Tensor[f32, N]:\n    return x + 1.0\n", "t.ax", d); Parser p; auto m=p.parse(toks, "t.ax"); auto ptx=emit_ptx(m); check(ptx.find(".version 8")!=std::string::npos && ptx.find("mma.sync")!=std::string::npos, "ptx format"); }
  // SPIR-V
  { auto words=emit_spirv_binary("main"); std::string err; check(validate_spirv(words, err) && words[0]==0x07230203u, "spirv magic+validate"); }
  // StableHLO
  { Lexer lx; std::vector<Diag> d; auto toks=lx.tokenize("@tpu\nfn k(x: Tensor[f32, N]) -> Tensor[f32, N]:\n    return x + 1.0\n", "t.ax", d); Parser p; auto m=p.parse(toks, "t.ax"); auto mlir=emit_stablehlo(m); check(mlir.find("stablehlo.dot_general")!=std::string::npos, "stablehlo dot_general"); }
  // Coro 100k
  { CoroScheduler sched(4); for(int i=0;i<100000;i++) sched.spawn([i]{ volatile int x=i; (void)x; }); sched.run(); check(sched.completed()==100000, "coro 100k"); }
  // Dist checkpoint
  { bool ok=ax_dist_train_localhost(4, "/tmp/ax_test.axckpt"); check(ok, "dist localhost checkpoint"); std::ifstream in("/tmp/ax_test.axckpt", std::ios::binary); check(in.good(), "ckpt file exists"); }
  std::cout<<"RESULT: "<<passed<<" passed, "<<failed<<" failed\n";
  return failed==0?0:1;
}
