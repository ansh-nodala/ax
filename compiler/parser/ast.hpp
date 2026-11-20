#pragma once
#include "compiler/common/ax_common.hpp"
namespace ax {
inline Expr make_num(double v){ Expr e; e.kind="num"; e.number=v; return e; }
inline Expr make_str(const std::string& s){ Expr e; e.kind="str"; e.str=s; return e; }
inline Expr make_var(const std::string& n){ Expr e; e.kind="var"; e.name=n; return e; }
inline Expr make_call(const std::string& n, std::vector<Expr> a){ Expr e; e.kind="call"; e.name=n; e.args=std::move(a); return e; }
inline Expr make_binary(const std::string& op, Expr l, Expr r){ Expr e; e.kind="binary"; e.op=op; e.left=std::make_shared<Expr>(std::move(l)); e.right=std::make_shared<Expr>(std::move(r)); return e; }
inline Expr make_list(std::vector<Expr> a){ Expr e; e.kind="list"; e.args=std::move(a); return e; }
inline Stmt make_stmt(const std::string& kind){ Stmt s; s.kind=kind; return s; }
} // namespace ax
