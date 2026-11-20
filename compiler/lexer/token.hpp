#pragma once
#include "compiler/common/ax_common.hpp"
namespace ax {
inline bool is_keyword(const std::string& s, TokKind& out) {
  if(s=="fn"||s=="function"){out=TokKind::Fn;return true;}
  if(s=="let"){out=TokKind::Let;return true;} if(s=="var"){out=TokKind::Var;return true;}
  if(s=="const"){out=TokKind::Const;return true;}
  if(s=="struct"||s=="record"){out=TokKind::Struct;return true;}
  if(s=="soa"){out=TokKind::Soa;return true;} if(s=="enum"){out=TokKind::Enum;return true;}
  if(s=="trait"){out=TokKind::Trait;return true;} if(s=="macro"){out=TokKind::Macro;return true;}
  if(s=="load"){out=TokKind::Load;return true;} if(s=="coro"){out=TokKind::Coro;return true;}
  if(s=="spawn"){out=TokKind::Spawn;return true;} if(s=="await"){out=TokKind::Await;return true;}
  if(s=="yield"){out=TokKind::Yield;return true;} if(s=="select"){out=TokKind::Select;return true;}
  if(s=="chan"){out=TokKind::Chan;return true;} if(s=="for"){out=TokKind::For;return true;}
  if(s=="if"||s=="when"){out=TokKind::If;return true;} if(s=="else"||s=="otherwise"){out=TokKind::Else;return true;}
  if(s=="elif"||s=="otherwise_if"){out=TokKind::Elif;return true;}
  if(s=="while"||s=="repeat_while"){out=TokKind::While;return true;}
  if(s=="for"||s=="iterate"){out=TokKind::For;return true;}
  if(s=="return"||s=="give_back"){out=TokKind::Return;return true;} if(s=="distribute"){out=TokKind::Distribute;return true;}
  if(s=="mesh"){out=TokKind::Mesh;return true;} if(s=="shard"){out=TokKind::Shard;return true;}
  if(s=="pipeline"){out=TokKind::Pipeline;return true;} if(s=="replicate"){out=TokKind::Replicate;return true;}
  if(s=="allreduce"){out=TokKind::AllReduce;return true;} if(s=="unleash"){out=TokKind::Unleash;return true;}
  return false;
}
} // namespace ax
