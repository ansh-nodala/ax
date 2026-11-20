#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <cstdlib>
namespace ax {
class Arena {
 public:
  explicit Arena(std::size_t cap = 1<<20) { buf_ = (char*)aligned_alloc(64, ((cap+63)/64)*64); cap_=((cap+63)/64)*64; }
  ~Arena(){ free(buf_); }
  void* alloc(std::size_t n, std::size_t align=64){ std::size_t off=(offset_+align-1)&~(align-1); if(off+n>cap_){ grow(n+align); off=(offset_+align-1)&~(align-1);} void*p=buf_+off; offset_=off+n; return p; }
  void reset(){ offset_=0; }
  std::size_t used() const { return offset_; }
 private:
  void grow(std::size_t need){ std::size_t ncap=cap_*2+need+4096; char*nb=(char*)aligned_alloc(64, ((ncap+63)/64)*64); if(buf_){ __builtin_memcpy(nb,buf_,offset_); free(buf_);} buf_=nb; cap_=((ncap+63)/64)*64; }
  char* buf_=nullptr; std::size_t cap_=0; std::size_t offset_=0;
};
} // namespace ax
