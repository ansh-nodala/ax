#pragma once
#include "compiler/common/ax_common.hpp"
#include <vector>
namespace ax {
// Indentation stack: tracks OFFSIDE. push returns true if new indent level.
class IndentStack {
 public:
  IndentStack(){ stack_.push_back(0); }
  // Returns 1 for indent, -1 for dedent (may need multiple), 0 same.
  int feed(int col, std::vector<Token>& out, Span sp) {
    if(col > stack_.back()){ stack_.push_back(col); Token t; t.kind=TokKind::Indent; t.span=sp; out.push_back(t); return 1; }
    int n=0; while(col < stack_.back()){ stack_.pop_back(); Token t; t.kind=TokKind::Dedent; t.span=sp; out.push_back(t); n++; }
    return n;
  }
  void finish(std::vector<Token>& out, Span sp){ while(stack_.size()>1){ stack_.pop_back(); Token t; t.kind=TokKind::Dedent; t.span=sp; out.push_back(t);} }
 private: std::vector<int> stack_;
};
} // namespace ax
