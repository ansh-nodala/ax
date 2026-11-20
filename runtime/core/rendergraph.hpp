#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ax::game {

using RenderResourceId = std::uint32_t;
using RenderPassId = std::uint32_t;

enum class RenderAccess : std::uint8_t {
  ShaderRead,
  TransferRead,
  StorageWrite,
  ColorWrite,
  DepthWrite,
};

struct RenderResourceDesc {
  std::uint32_t width = 1;
  std::uint32_t height = 1;
  std::uint32_t format = 0;
  std::uint32_t samples = 1;
  bool transient = true;
  friend bool operator==(const RenderResourceDesc&, const RenderResourceDesc&) = default;
};

struct RenderUse {
  RenderResourceId resource;
  RenderAccess access;
};

struct RenderBarrier {
  RenderResourceId resource;
  RenderPassId before_pass;
  RenderAccess from;
  RenderAccess to;
};

struct RenderAllocation {
  RenderResourceId resource;
  std::size_t first_use;
  std::size_t last_use;
  std::uint32_t alias_slot;
};

struct RenderPlan {
  std::vector<RenderPassId> pass_order;
  std::vector<RenderBarrier> barriers;
  std::vector<RenderAllocation> allocations;
  std::uint32_t transient_slot_count = 0;
};

class RenderGraph {
 public:
  RenderResourceId create_resource(RenderResourceDesc desc);
  RenderPassId add_pass(std::string name, std::vector<RenderUse> uses);
  void depends_on(RenderPassId pass, RenderPassId prerequisite);
  RenderPlan compile() const;

 private:
  struct Resource { RenderResourceDesc desc; };
  struct Pass {
    std::string name;
    std::vector<RenderUse> uses;
    std::vector<RenderPassId> explicit_dependencies;
  };

  std::vector<Resource> resources_;
  std::vector<Pass> passes_;
};

} // namespace ax::game
