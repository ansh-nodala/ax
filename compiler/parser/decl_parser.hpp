#pragma once
#include "compiler/common/ax_common.hpp"
#include <string>
namespace ax {
// Parse a type annotation string from tokens: Tensor[f32, B, T, C], f32, vec4, i32, etc.
// Consumes tokens until a delimiter at bracket depth 0.
inline std::string parse_type_string(const std::vector<Token>& toks, std::size_t& pos){
  std::string out; int depth=0;
  while(pos < toks.size()){
    TokKind k = toks[pos].kind;
    if(depth==0 && (k==TokKind::Colon||k==TokKind::Comma||k==TokKind::RParen||k==TokKind::Assign||k==TokKind::Newline||k==TokKind::Eof)) break;
    if(k==TokKind::LBracket) depth++;
    if(k==TokKind::RBracket) depth--;
    if(!out.empty() && k!=TokKind::LBracket && k!=TokKind::RBracket && k!=TokKind::Comma) out.push_back(' ');
    out += toks[pos].text;
    if(k==TokKind::Comma) out.push_back(' ');
    pos++;
  }
  return out;
}
} // namespace ax
