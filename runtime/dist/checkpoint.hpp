#pragma once
#include "runtime/dist/dist.hpp"
#include <string>
#include <vector>
namespace ax {
inline bool write_ckpt(const std::string& path, const std::vector<double>& values) {
  return ax_dist_checkpoint_write(path, values);
}
}
