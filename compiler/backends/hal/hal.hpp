#pragma once
#include <string>
#include <map>
#include <functional>
#include <memory>
namespace ax {
enum class BackendKind { CPU, Neon, Cuda, Rocm, Tpu, Vulkan, Metal, Custom };
struct Backend { BackendKind kind; std::string name; std::function<std::string(const std::string&)> emit; };
class BackendRegistry {
 public:
  static BackendRegistry& instance(){ static BackendRegistry r; return r; }
  void reg(Backend b){ map_[b.name]=std::move(b); }
  const Backend* find(const std::string& n) const { auto it=map_.find(n); return it==map_.end()?nullptr:&it->second; }
  std::vector<std::string> list() const { std::vector<std::string> o; for(auto &kv: map_) o.push_back(kv.first); return o; }
 private: std::map<std::string,Backend> map_;
};
void register_builtin_backends();
} // namespace ax
