#include "compiler/backends/hal/hal.hpp"
namespace ax {
void register_builtin_backends(){
  auto &r = BackendRegistry::instance();
  r.reg({BackendKind::CPU, "cpu", [](const std::string& s){ return s; }});
  r.reg({BackendKind::Neon, "neon", [](const std::string& s){ return s; }});
  r.reg({BackendKind::Cuda, "cuda", [](const std::string& s){ return s; }});
  r.reg({BackendKind::Rocm, "rocm", [](const std::string& s){ return s; }});
  r.reg({BackendKind::Tpu, "tpu", [](const std::string& s){ return s; }});
  r.reg({BackendKind::Vulkan, "vulkan", [](const std::string& s){ return s; }});
  r.reg({BackendKind::Metal, "metal", [](const std::string& s){ return s; }});
}
} // namespace ax
