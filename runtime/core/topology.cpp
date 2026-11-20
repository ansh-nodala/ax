#include "runtime/core/axrt.hpp"
#include <fstream>
#include <string>
#if defined(__linux__)
#include <sys/auxv.h>
#endif
extern "C" int ax_topology_cores(void){
  std::ifstream in("/proc/cpuinfo"); int n=0; std::string line; while(std::getline(in,line)) if(line.rfind("processor",0)==0) n++; return n>0?n:1;
}
extern "C" int ax_topology_has_neon(void){
#if defined(__aarch64__) && defined(__linux__)
  unsigned long hw = getauxval(AT_HWCAP); return (hw & (1UL<<1))!=0; // HWCAP_ASIMD
#else
  return 0;
#endif
}
extern "C" int ax_topology_has_sve(void){
#if defined(__aarch64__) && defined(__linux__)
  unsigned long hw = getauxval(AT_HWCAP); return (hw & (1UL<<22))!=0;
#else
  return 0;
#endif
}
extern "C" int ax_topology_has_fp16(void){
#if defined(__aarch64__) && defined(__linux__)
  unsigned long hw = getauxval(AT_HWCAP); return (hw & (1UL<<9))!=0;
#else
  return 0;
#endif
}
extern "C" int ax_topology_has_bf16(void){ return 0; }
extern "C" int ax_topology_has_dotprod(void){
#if defined(__aarch64__) && defined(__linux__)
  unsigned long hw = getauxval(AT_HWCAP); return (hw & (1UL<<20))!=0;
#else
  return 0;
#endif
}
namespace ax { int cores(){ return ax_topology_cores(); } bool has_neon(){ return ax_topology_has_neon()!=0; } bool has_sve(){ return ax_topology_has_sve()!=0; } }
