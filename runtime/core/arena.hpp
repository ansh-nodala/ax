#pragma once
#include <cstddef>
#include <cstdlib>
namespace ax {
inline void* ax_arena_alloc_impl(std::size_t n){
  static thread_local char* buf=nullptr; static thread_local std::size_t cap=0; static thread_local std::size_t off=0;
  if(!buf){ cap=1<<20; buf=(char*)aligned_alloc(64, cap); off=0; }
  std::size_t aligned=(off+63)&~std::size_t(63);
  if(aligned+n>cap){ cap*=2; char* nb=(char*)aligned_alloc(64, cap); for(std::size_t i=0;i<off;i++) nb[i]=buf[i]; free(buf); buf=nb; aligned=(off+63)&~std::size_t(63); }
  void* p=buf+aligned; off=aligned+n; return p;
}
inline void ax_arena_reset_impl(){ /* bump arena: reset is no-op per-thread until explicit */ }
} // namespace ax
