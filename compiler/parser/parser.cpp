#include "compiler/parser/parser.hpp"
#include "compiler/parser/ast.hpp"
#include "compiler/parser/expr_parser.hpp"
#include "compiler/parser/decl_parser.hpp"
namespace ax {
static std::string normalize_decorator(std::string d){
  if(d=="nvidia") return "cuda";
  if(d=="google_tpu") return "tpu";
  if(d=="autograd") return "autodiff";
  if(d=="optimize") return "maxpower";
  return d;
}
const Token& Parser::peek(int off) const {
  std::size_t idx = pos_ + (std::size_t)off;
  if(idx >= toks_.size()) return toks_.back();
  return toks_[idx];
}
bool Parser::eat(TokKind k){ if(at(k)){ pos_++; return true; } return false; }
void Parser::skip_newlines(){ while(at(TokKind::Newline)||at(TokKind::Semi)) pos_++; }
Token Parser::expect(TokKind k, const std::string& what){
  if(at(k)){ Token t=cur(); pos_++; return t; }
  Token t=cur(); return t;
}
Module Parser::parse(const std::vector<Token>& toks, const std::string& file){
  toks_=toks; pos_=0; file_=file; Module m; m.source_path=file; m.fns=parse_top(); return m;
}
std::vector<Stmt> Parser::parse_top(){
  std::vector<Stmt> out; skip_newlines();
  while(!at(TokKind::Eof)){
    // decorators
    std::vector<std::string> decors;
    while(at(TokKind::At)){ pos_++; if(at(TokKind::Ident)){ decors.push_back(normalize_decorator(cur().text)); pos_++; } if(at(TokKind::LParen)){ int d=0; while(!at(TokKind::Eof)){ if(at(TokKind::LParen)) d++; if(at(TokKind::RParen)){ d--; pos_++; if(d<=0) break; continue; } pos_++; } } skip_newlines(); }
    if(at(TokKind::Eof)) break;
    if(at(TokKind::Fn)){ Stmt s=parse_fn(decors); out.push_back(std::move(s)); }
    else if(at(TokKind::Struct)||at(TokKind::Soa)||at(TokKind::Enum)||at(TokKind::Trait)||at(TokKind::Macro)){
      std::string k = at(TokKind::Struct)?"struct":at(TokKind::Soa)?"soa struct":at(TokKind::Enum)?"enum":at(TokKind::Trait)?"trait":"macro";
      Stmt s=parse_struct_like(k); s.decorators=decors; out.push_back(std::move(s));
    }
    else if(at(TokKind::Load)){ Stmt s=parse_load(); out.push_back(std::move(s)); }
    else if(at(TokKind::Newline)||at(TokKind::Semi)||at(TokKind::Dedent)){ pos_++; }
    else { Stmt s=parse_stmt(); out.push_back(std::move(s)); }
    skip_newlines();
  }
  return out;
}
std::vector<Stmt> Parser::parse_block(){
  std::vector<Stmt> out; skip_newlines();
  if(eat(TokKind::Indent)){
    while(!at(TokKind::Dedent) && !at(TokKind::Eof)){
      skip_newlines(); if(at(TokKind::Dedent)||at(TokKind::Eof)) break;
      // decorators inside block
      std::vector<std::string> decors;
      while(at(TokKind::At)){ pos_++; if(at(TokKind::Ident)){ decors.push_back(normalize_decorator(cur().text)); pos_++; } if(at(TokKind::LParen)){ int d=0; while(!at(TokKind::Eof)){ if(at(TokKind::LParen)) d++; if(at(TokKind::RParen)){ d--; pos_++; if(d<=0) break; continue; } pos_++; } } skip_newlines(); }
      if(at(TokKind::Fn)){ Stmt s=parse_fn(decors); out.push_back(std::move(s)); }
      else { Stmt s=parse_stmt(); s.decorators=decors; out.push_back(std::move(s)); }
      skip_newlines();
    }
    eat(TokKind::Dedent);
  } else {
    if(!at(TokKind::Eof) && !at(TokKind::Dedent)){ Stmt s=parse_stmt(); out.push_back(std::move(s)); }
  }
  return out;
}
Stmt Parser::parse_struct_like(const std::string& kind){
  Stmt s=make_stmt(kind); if(at(TokKind::Struct)||at(TokKind::Soa)||at(TokKind::Enum)||at(TokKind::Trait)||at(TokKind::Macro)) pos_++;
  if(at(TokKind::Ident)){ s.name=cur().text; pos_++; }
  // skip until colon / indent, then consume block if present
  while(!at(TokKind::Colon) && !at(TokKind::Newline) && !at(TokKind::Eof)) pos_++;
  if(at(TokKind::Colon)) pos_++;
  skip_newlines(); if(at(TokKind::Indent)){ s.body=parse_block(); }
  return s;
}
Stmt Parser::parse_load(){
  Stmt s=make_stmt("load"); pos_++; // load
  std::string name;
  while(at(TokKind::Ident)||at(TokKind::Dot)||at(TokKind::LBrace)||at(TokKind::RBrace)||at(TokKind::Comma)){
    name += cur().text; if(at(TokKind::Comma)) name.push_back(' '); pos_++;
    if(at(TokKind::Newline)||at(TokKind::Eof)) break;
  }
  s.extra=name; return s;
}
Stmt Parser::parse_fn(const std::vector<std::string>& decorators){
  Stmt s=make_stmt("fn"); s.decorators=decorators; expect(TokKind::Fn,"fn");
  if(at(TokKind::Ident)){ s.name=cur().text; pos_++; }
  expect(TokKind::LParen,"(");
  while(!at(TokKind::RParen) && !at(TokKind::Eof)){
    if(at(TokKind::Ident)){
      std::string pn=cur().text; pos_++; std::string pt;
      if(eat(TokKind::Colon)){ pt=parse_type_ann(); }
      s.params.push_back({pn,pt});
    }
    if(at(TokKind::Comma)) pos_++; else break;
  }
  eat(TokKind::RParen);
  if(eat(TokKind::Arrow)){ s.ret_type=parse_type_ann(); }
  if(at(TokKind::Colon)) pos_++;
  skip_newlines(); s.body=parse_block();
  return s;
}
std::string Parser::parse_type_ann(){
  std::size_t p=pos_; std::string t=parse_type_string(toks_, p); pos_=p; return t;
}
Stmt Parser::parse_if(){
  Stmt s=make_stmt("if"); expect(TokKind::If,"if"); s.cond=parse_expr();
  if(at(TokKind::Colon)) pos_++;
  skip_newlines();
  s.body=parse_block();
  skip_newlines();
  if(at(TokKind::Else)){ pos_++; if(at(TokKind::Colon)) pos_++; skip_newlines();
    if(at(TokKind::If)){ Stmt elif_s=parse_if(); s.else_body.push_back(std::move(elif_s)); }
    else s.else_body=parse_block();
  } else if(at(TokKind::Elif)){ pos_++; Stmt elif_s=make_stmt("if"); elif_s.cond=parse_expr(); if(at(TokKind::Colon)) pos_++; skip_newlines(); elif_s.body=parse_block(); s.else_body.push_back(std::move(elif_s)); }
  return s;
}
Stmt Parser::parse_for(){
  Stmt s=make_stmt("for"); expect(TokKind::For,"for");
  // support "parallel for" already consumed? caller handles parallel prefix
  if(at(TokKind::Ident)){ s.loop_var=cur().text; pos_++; }
  // expect "in"
  if(at(TokKind::Ident) && cur().text=="in") pos_++;
  // range(...) or expr
  s.range_expr=parse_expr();
  if(at(TokKind::Colon)) pos_++;
  skip_newlines();
  s.body=parse_block();
  return s;
}
Stmt Parser::parse_while(){
  Stmt s=make_stmt("while"); expect(TokKind::While,"while"); s.cond=parse_expr();
  if(at(TokKind::Colon)) pos_++;
  skip_newlines();
  s.body=parse_block();
  return s;
}
Stmt Parser::parse_return(){
  Stmt s=make_stmt("return"); expect(TokKind::Return,"return");
  if(!at(TokKind::Newline) && !at(TokKind::Dedent) && !at(TokKind::Eof)) s.expr=parse_expr();
  return s;
}
Stmt Parser::parse_coro_stmt(const std::string& kind){
  Stmt s=make_stmt(kind); pos_++;
  if(!at(TokKind::Newline) && !at(TokKind::Dedent) && !at(TokKind::Eof) && !at(TokKind::Colon)) s.expr=parse_expr();
  if(at(TokKind::Colon)){ pos_++; skip_newlines(); s.body=parse_block(); }
  return s;
}
Stmt Parser::parse_stmt(){
  skip_newlines();
  if(at(TokKind::If)) return parse_if();
  if(at(TokKind::For)) return parse_for();
  if(at(TokKind::While)) return parse_while();
  if(at(TokKind::Return)) return parse_return();
  if(at(TokKind::Coro)||at(TokKind::Spawn)||at(TokKind::Await)||at(TokKind::Yield)||at(TokKind::Chan)||at(TokKind::Select)){
    std::string k=cur().text; return parse_coro_stmt(k);
  }
  if(at(TokKind::Distribute)||at(TokKind::Shard)||at(TokKind::Mesh)||at(TokKind::Pipeline)||at(TokKind::Replicate)||at(TokKind::AllReduce)||at(TokKind::Unleash)){
    std::string k=cur().text; Stmt s=make_stmt(k); pos_++;
    if(at(TokKind::LParen)){ int d=0; std::string ex; while(!at(TokKind::Eof)){ if(at(TokKind::LParen)) d++; if(at(TokKind::RParen)){ d--; pos_++; if(d<=0) break; continue; } ex+=cur().text; ex.push_back(' '); pos_++; } s.extra=ex; }
    if(at(TokKind::Colon)){ pos_++; skip_newlines(); s.body=parse_block(); }
    else if(!at(TokKind::Newline) && !at(TokKind::Eof)){ s.expr=parse_expr(); }
    return s;
  }
  if(at(TokKind::Load)) return parse_load();
  if(at(TokKind::Struct)||at(TokKind::Enum)||at(TokKind::Trait)||at(TokKind::Macro)) return parse_struct_like("struct");
  if(at(TokKind::Let)||at(TokKind::Var)||at(TokKind::Const)){
    TokKind binding=cur().kind; pos_++;
    Stmt s=make_stmt("assign"); s.is_const=(binding==TokKind::Let||binding==TokKind::Const);
    if(at(TokKind::Ident)){ s.name=cur().text; pos_++; }
    if(eat(TokKind::Colon)) s.type_ann=parse_type_ann();
    if(eat(TokKind::Assign)) s.expr=parse_expr();
    else s.has_initializer=false;
    return s;
  }
  // Ident-led: assignment, typed assignment, call/print, parallel for
  if(at(TokKind::Ident) && cur().text=="parallel"){
    // parallel for ...
    pos_++; if(at(TokKind::For)){ Stmt s=parse_for(); s.kind="parallel_for"; return s; }
  }
  if(at(TokKind::Ident)){
    std::string name=cur().text;
    // lookahead
    const Token& nxt = peek(1);
    if(nxt.kind==TokKind::Colon){
      // x: Type = expr  or x: Type (field)
      pos_+=2; std::string ty=parse_type_ann();
      Stmt s=make_stmt("assign"); s.name=name; s.type_ann=ty;
      if(eat(TokKind::Assign)){ s.expr=parse_expr(); }
      return s;
    }
    if(nxt.kind==TokKind::Assign){
      pos_+=2; Stmt s=make_stmt("assign"); s.name=name; s.expr=parse_expr(); return s;
    }
    // otherwise expression statement
    Expr e=parse_expr(); Stmt s=make_stmt("expr"); s.expr=std::move(e); return s;
  }
  // fallback: expression statement
  Expr e=parse_expr(); Stmt s=make_stmt("expr"); s.expr=std::move(e); return s;
}
Expr Parser::parse_expr(){ Expr lhs=parse_unary(); return parse_binary_rhs(0, std::move(lhs)); }
Expr Parser::parse_binary_rhs(int min_prec, Expr lhs){
  while(true){
    int prec=expr_precedence(cur().kind); if(prec==0 || prec < min_prec) break;
    std::string op=tok_op(cur().kind); pos_++;
    Expr rhs=parse_unary(); rhs=parse_binary_rhs(prec+1, std::move(rhs));
    lhs=make_binary(op, std::move(lhs), std::move(rhs));
  }
  return lhs;
}
Expr Parser::parse_unary(){
  if(at(TokKind::Minus)){ pos_++; Expr e=parse_unary(); Expr zero=make_num(0); return make_binary("-", std::move(zero), std::move(e)); }
  if(at(TokKind::Bang)){ pos_++; Expr e=parse_unary(); Expr one=make_num(1); (void)one; return make_call("not", {std::move(e)}); }
  return parse_postfix();
}
Expr Parser::parse_postfix(){ return parse_primary(); }
Expr Parser::parse_primary(){
  if(at(TokKind::Number)){ double v=cur().number; pos_++; return make_num(v); }
  if(at(TokKind::Str)){ std::string s=cur().text; pos_++; // strip quotes
    if(s.size()>=2) s=s.substr(1,s.size()-2);
    return make_str(s); }
  if(at(TokKind::Ident)){
    std::string name=cur().text; pos_++;
    if(at(TokKind::LParen)){
      pos_++; std::vector<Expr> args;
      while(at(TokKind::Newline)) pos_++;
      while(!at(TokKind::RParen) && !at(TokKind::Eof)){
        args.push_back(parse_expr());
        while(at(TokKind::Newline)) pos_++;
        if(at(TokKind::Comma)){
          pos_++;
          while(at(TokKind::Newline)) pos_++;
        } else break;
      }
      while(at(TokKind::Newline)) pos_++;
      eat(TokKind::RParen); return make_call(name, std::move(args));
    }
    if(at(TokKind::LBracket)){
      // index: var[idx] -> call index
      pos_++; Expr idx=parse_expr(); eat(TokKind::RBracket); return make_call("index", {make_var(name), std::move(idx)});
    }
    return make_var(name);
  }
  if(at(TokKind::LParen)){
    pos_++;
    while(at(TokKind::Newline)) pos_++;
    Expr e=parse_expr();
    while(at(TokKind::Newline)) pos_++;
    eat(TokKind::RParen);
    return e;
  }
  if(at(TokKind::LBracket)){
    pos_++; std::vector<Expr> elems;
    while(at(TokKind::Newline)) pos_++;
    while(!at(TokKind::RBracket) && !at(TokKind::Eof)){
      elems.push_back(parse_expr());
      while(at(TokKind::Newline)) pos_++;
      if(at(TokKind::Comma)){
        pos_++;
        while(at(TokKind::Newline)) pos_++;
      } else break;
    }
    while(at(TokKind::Newline)) pos_++;
    eat(TokKind::RBracket);
    return make_list(std::move(elems));
  }
  // unknown: consume and return 0
  if(!at(TokKind::Eof)) pos_++;
  return make_num(0);
}
} // namespace ax
