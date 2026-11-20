#include "runtime/core/os.hpp"
#include <unistd.h>
#include <limits.h>
namespace ax {
std::string os_name(){ return "Linux"; }
std::string os_cwd(){ char buf[4096]; if(getcwd(buf, sizeof(buf))) return std::string(buf); return "."; }
} // namespace ax
