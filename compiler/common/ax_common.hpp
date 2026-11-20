#pragma once
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include <cstdint>
namespace ax {
struct Span { std::string file; int line = 1; int col = 1; };
struct Diag { std::string message; Span span; bool is_error = true; };
enum class TokKind { Eof, Ident, Number, Str, Fn, Let, Var, Const, Struct, Soa, Enum, Trait, Macro, Load, Coro, Spawn, Await, Yield, Select, Chan, Parallel, For, If, Else, Elif, While, Return, Distribute, Mesh, Shard, Pipeline, Replicate, AllReduce, Unleash, At, Colon, Comma, LParen, RParen, LBracket, RBracket, LBrace, RBrace, Assign, Plus, Minus, Star, Slash, Percent, EqEq, NotEq, Lt, Gt, Le, Ge, Bang, Arrow, Dot, Newline, Indent, Dedent, Semi };
struct Token { TokKind kind = TokKind::Eof; std::string text; double number = 0; Span span; };
struct Expr {
  std::string kind; // num,str,var,call,binary,list,tensor,vec
  std::string name; double number = 0; std::string str; std::string op;
  std::vector<Expr> args; std::shared_ptr<Expr> left; std::shared_ptr<Expr> right;
};
struct Stmt {
  std::string kind; // fn,assign,print,for,if,while,return,expr,load,decorator,struct,enum,trait,macro,coro,spawn,await,chan,select
  std::string name; std::string type_ann; Expr expr; Expr cond; std::string loop_var; Expr range_expr;
  bool is_const = false;
  bool has_initializer = true;
  std::vector<Stmt> body; std::vector<Stmt> else_body;
  std::vector<std::pair<std::string,std::string>> params; std::string ret_type;
  std::vector<std::string> decorators; Span span; std::string extra;
};
struct Module { std::vector<Stmt> fns; std::vector<Diag> diags; std::string source_path; };
struct MirInst { std::string op; std::string dst; std::string a; std::string b; double imm = 0; std::string str; std::string type; std::vector<int> shape; };
struct MirFunc { std::string name; std::vector<MirInst> insts; std::vector<std::string> decorators; };
struct MirModule { std::vector<MirFunc> funcs; };
inline std::string tok_kind_name(TokKind k) {
  switch(k){case TokKind::Eof:return "eof";case TokKind::Ident:return "ident";case TokKind::Number:return "number";case TokKind::Str:return "string";default:return "token";}
  return "token";
}
} // namespace ax
