#pragma once
#include "compiler/common/ax_common.hpp"
namespace ax {
inline int expr_precedence(TokKind k){
  if(k==TokKind::Star||k==TokKind::Slash||k==TokKind::Percent) return 50;
  if(k==TokKind::Plus||k==TokKind::Minus) return 40;
  if(k==TokKind::Lt||k==TokKind::Gt||k==TokKind::Le||k==TokKind::Ge) return 30;
  if(k==TokKind::EqEq||k==TokKind::NotEq) return 25;
  return 0;
}
inline std::string tok_op(TokKind k){
  if(k==TokKind::Plus) return "+";
  if(k==TokKind::Minus) return "-";
  if(k==TokKind::Star) return "*";
  if(k==TokKind::Slash) return "/";
  if(k==TokKind::Percent) return "%";
  if(k==TokKind::Lt) return "<";
  if(k==TokKind::Gt) return ">";
  if(k==TokKind::Le) return "<=";
  if(k==TokKind::Ge) return ">=";
  if(k==TokKind::EqEq) return "==";
  if(k==TokKind::NotEq) return "!=";
  return "";
}
} // namespace ax
