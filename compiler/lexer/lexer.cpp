#include "compiler/lexer/lexer.hpp"
#include "compiler/lexer/token.hpp"
#include "compiler/lexer/utf8.hpp"
#include "compiler/lexer/indent.hpp"
namespace ax {
std::vector<Token> Lexer::tokenize(const std::string& src, const std::string& file, std::vector<Diag>& diags) {
  std::vector<Token> out;
  IndentStack ind;
  std::size_t i=0; int line=1; int line_start=0; int grouping_depth=0;
  auto span_at = [&](int col){ Span s; s.file=file; s.line=line; s.col=col; return s; };
  bool at_line_start=true;
  while(i < src.size()){
    char c = src[i];
    if(c=='\n'){ Token t; t.kind=TokKind::Newline; t.span=span_at((int)(i-line_start+1)); out.push_back(t); i++; line++; line_start=(int)i; at_line_start=true; continue; }
    if(c==' '||c=='\t'||c=='\r'){ i++; continue; }
    // comment //
    if(c=='/' && i+1<src.size() && src[i+1]=='/'){ while(i<src.size() && src[i]!='\n') i++; continue; }
    if(c=='#' ){ while(i<src.size() && src[i]!='\n') i++; continue; }
    if(at_line_start){
      int col=(int)(i-line_start);
      // blank line: skip indent handling
      if(i<src.size() && src[i]!='\n' && grouping_depth==0) ind.feed(col, out, span_at(col+1));
      at_line_start=false;
    }
    int col=(int)(i-line_start+1);
    if(c=='"'){
      std::size_t j=i+1; std::string val; val.push_back('"');
      while(j<src.size() && src[j]!='"'){ if(src[j]=='\\' && j+1<src.size()){ val.push_back(src[j]); val.push_back(src[j+1]); j+=2; continue; } val.push_back(src[j]); j++; }
      if(j<src.size()){ val.push_back('"'); j++; }
      Token t; t.kind=TokKind::Str; t.text=val; t.span=span_at(col); out.push_back(t); i=j; continue;
    }
    if(c=='\''){
      std::size_t j=i+1; std::string val; val.push_back('\'');
      while(j<src.size() && src[j]!='\''){ val.push_back(src[j]); j++; }
      if(j<src.size()){ val.push_back('\''); j++; }
      Token t; t.kind=TokKind::Str; t.text=val; t.span=span_at(col); out.push_back(t); i=j; continue;
    }
    if((c>='0'&&c<='9') || (c=='.' && i+1<src.size() && src[i+1]>='0' && src[i+1]<='9')){
      std::size_t j=i; while(j<src.size() && ((src[j]>='0'&&src[j]<='9')||src[j]=='.'||src[j]=='e'||src[j]=='E'||src[j]=='+'||src[j]=='-')){ if((src[j]=='+'||src[j]=='-') && j>i && src[j-1]!='e' && src[j-1]!='E') break; j++; }
      std::string num=src.substr(i,j-i); Token t; t.kind=TokKind::Number; t.text=num; try{t.number=std::stod(num);}catch(...){t.number=0;} t.span=span_at(col); out.push_back(t); i=j; continue;
    }
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='_'){
      std::size_t j=i; while(j<src.size() && ((src[j]>='A'&&src[j]<='Z')||(src[j]>='a'&&src[j]<='z')||(src[j]>='0'&&src[j]<='9')||src[j]=='_')) j++;
      std::string word=src.substr(i,j-i); Token t; TokKind kw; if(is_keyword(word,kw)) t.kind=kw; else t.kind=TokKind::Ident; t.text=word; t.span=span_at(col); out.push_back(t); i=j; continue;
    }
    if((unsigned char)c >= 0x80){
      std::size_t pos=i; uint32_t cp=0; utf8_decode(src,pos,cp); std::string op;
      if(math_cp_to_op(cp,op)){
        Token t; if(op=="*"||op=="/"){ t.kind=(op=="*"?TokKind::Star:TokKind::Slash); } else if(op=="<="){t.kind=TokKind::Le;} else if(op==">="){t.kind=TokKind::Ge;} else if(op=="!="){t.kind=TokKind::NotEq;} else { t.kind=TokKind::Number; t.number=3.141592653589793; }
        t.text=op; t.span=span_at(col); out.push_back(t); i=pos; continue;
      }
      Diag d; d.message="unexpected UTF-8 character in source"; d.span=span_at(col); diags.push_back(d); i=pos; continue;
    }
    // operators / punctuation
    auto emit = [&](TokKind k, std::string txt, std::size_t adv){
      Token t; t.kind=k; t.text=txt; t.span=span_at(col); out.push_back(t);
      if(k==TokKind::LParen||k==TokKind::LBracket||k==TokKind::LBrace) ++grouping_depth;
      else if((k==TokKind::RParen||k==TokKind::RBracket||k==TokKind::RBrace)&&grouping_depth>0) --grouping_depth;
      i+=adv;
    };
    if(c=='@'){ emit(TokKind::At,"@",1); continue; }
    if(c==':'){ emit(TokKind::Colon,":",1); continue; }
    if(c==','){ emit(TokKind::Comma,",",1); continue; }
    if(c=='('){ emit(TokKind::LParen,"(",1); continue; }
    if(c==')'){ emit(TokKind::RParen,")",1); continue; }
    if(c=='['){ emit(TokKind::LBracket,"[",1); continue; }
    if(c==']'){ emit(TokKind::RBracket,"]",1); continue; }
    if(c=='{'){ emit(TokKind::LBrace,"{",1); continue; }
    if(c=='}'){ emit(TokKind::RBrace,"}",1); continue; }
    if(c==';'){ emit(TokKind::Semi,";",1); continue; }
    if(c=='.'){ emit(TokKind::Dot,".",1); continue; }
    if(c=='=' && i+1<src.size() && src[i+1]=='='){ emit(TokKind::EqEq,"==",2); continue; }
    if(c=='!' && i+1<src.size() && src[i+1]=='='){ emit(TokKind::NotEq,"!=",2); continue; }
    if(c=='<' && i+1<src.size() && src[i+1]=='='){ emit(TokKind::Le,"<=",2); continue; }
    if(c=='>' && i+1<src.size() && src[i+1]=='='){ emit(TokKind::Ge,">=",2); continue; }
    if(c=='-' && i+1<src.size() && src[i+1]=='>'){ emit(TokKind::Arrow,"->",2); continue; }
    if(c=='='){ emit(TokKind::Assign,"=",1); continue; }
    if(c=='+'){ emit(TokKind::Plus,"+",1); continue; }
    if(c=='-'){ emit(TokKind::Minus,"-",1); continue; }
    if(c=='*'){ emit(TokKind::Star,"*",1); continue; }
    if(c=='/'){ emit(TokKind::Slash,"/",1); continue; }
    if(c=='%'){ emit(TokKind::Percent,"%",1); continue; }
    if(c=='<'){ emit(TokKind::Lt,"<",1); continue; }
    if(c=='>'){ emit(TokKind::Gt,">",1); continue; }
    if(c=='!'){ emit(TokKind::Bang,"!",1); continue; }
    Diag d; d.message=std::string("unexpected character '")+c+"'"; d.span=span_at(col); diags.push_back(d); i++;
  }
  Span sp; sp.file=file; sp.line=line; sp.col=1; ind.finish(out, sp);
  Token eof; eof.kind=TokKind::Eof; eof.span=sp; out.push_back(eof);
  return out;
}
} // namespace ax
