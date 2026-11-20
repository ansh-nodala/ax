#pragma once
#include "compiler/common/ax_common.hpp"
#include <map>
#include <vector>
namespace ax {
struct Symbol { std::string type; std::vector<int> shape; bool is_tensor=false; bool moved=false; bool is_const=false; };
class SymbolTable {
 public:
  void push_scope(){ scopes_.push_back({}); }
  void pop_scope(){ if(!scopes_.empty()) scopes_.pop_back(); }
  void declare(const std::string& n, Symbol s){ if(scopes_.empty()) push_scope(); scopes_.back()[n]=std::move(s); }
  std::optional<Symbol> lookup(const std::string& n) const { for(int i=(int)scopes_.size()-1;i>=0;--i){ auto it=scopes_[i].find(n); if(it!=scopes_[i].end()) return it->second; } return std::nullopt; }
  bool mark_moved(const std::string& n){ for(int i=(int)scopes_.size()-1;i>=0;--i){ auto it=scopes_[i].find(n); if(it!=scopes_[i].end()){ it->second.moved=true; return true; } } return false; }
 private: std::vector<std::map<std::string,Symbol>> scopes_;
};
} // namespace ax
