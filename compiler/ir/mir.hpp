#pragma once
#include "compiler/common/ax_common.hpp"
namespace ax {
class MirBuilder {
 public:
  MirModule build(const Module& m);
};
} // namespace ax
