#pragma once
#include "compiler/common/ax_common.hpp"
namespace ax {
class Sema {
 public:
  Module check(Module m);
};
} // namespace ax
