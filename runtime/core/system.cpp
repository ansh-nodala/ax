#include "runtime/core/system.hpp"
#include "runtime/core/axrt.hpp"
#include <string>
namespace ax {
std::string system_info(){ return "cores="+std::to_string(ax_topology_cores())+" neon="+std::to_string(ax_topology_has_neon())+" sve="+std::to_string(ax_topology_has_sve()); }
} // namespace ax
