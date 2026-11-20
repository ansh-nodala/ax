#include "runtime/core/rendergraph.hpp"

#include <algorithm>
#include <limits>
#include <queue>
#include <set>
#include <stdexcept>

namespace ax::game {
namespace {

bool writes(RenderAccess access) {
  return access == RenderAccess::StorageWrite || access == RenderAccess::ColorWrite ||
         access == RenderAccess::DepthWrite;
}

} // namespace

RenderResourceId RenderGraph::create_resource(RenderResourceDesc desc) {
  if (desc.width == 0 || desc.height == 0 || desc.samples == 0) {
    throw std::invalid_argument("render resource dimensions and sample count must be positive");
  }
  if (resources_.size() >= std::numeric_limits<RenderResourceId>::max()) {
    throw std::length_error("render graph has too many resources");
  }
  resources_.push_back({desc});
  return static_cast<RenderResourceId>(resources_.size() - 1);
}

RenderPassId RenderGraph::add_pass(std::string name, std::vector<RenderUse> uses) {
  if (name.empty()) throw std::invalid_argument("render pass name cannot be empty");
  if (passes_.size() >= std::numeric_limits<RenderPassId>::max()) {
    throw std::length_error("render graph has too many passes");
  }
  std::set<RenderResourceId> seen;
  for (const RenderUse& use : uses) {
    if (use.resource >= resources_.size()) throw std::out_of_range("render pass uses an unknown resource");
    if (!seen.insert(use.resource).second) throw std::invalid_argument("render pass declares a resource more than once");
  }
  passes_.push_back({std::move(name), std::move(uses), {}});
  return static_cast<RenderPassId>(passes_.size() - 1);
}

void RenderGraph::depends_on(RenderPassId pass, RenderPassId prerequisite) {
  if (pass >= passes_.size() || prerequisite >= passes_.size()) {
    throw std::out_of_range("render graph dependency references an unknown pass");
  }
  if (pass == prerequisite) throw std::invalid_argument("render pass cannot depend on itself");
  auto& dependencies = passes_[pass].explicit_dependencies;
  if (std::find(dependencies.begin(), dependencies.end(), prerequisite) == dependencies.end()) {
    dependencies.push_back(prerequisite);
  }
}

RenderPlan RenderGraph::compile() const {
  const std::size_t pass_count = passes_.size();
  const std::size_t resource_count = resources_.size();
  std::vector<std::set<RenderPassId>> dependencies(pass_count);
  std::vector<std::vector<RenderPassId>> readers(resource_count);
  std::vector<RenderPassId> last_writer(resource_count, std::numeric_limits<RenderPassId>::max());

  for (std::size_t p = 0; p < pass_count; ++p) {
    for (RenderPassId prerequisite : passes_[p].explicit_dependencies) dependencies[p].insert(prerequisite);
    for (const RenderUse& use : passes_[p].uses) {
      const RenderPassId writer = last_writer[use.resource];
      if (writer != std::numeric_limits<RenderPassId>::max()) dependencies[p].insert(writer);
      if (writes(use.access)) {
        for (RenderPassId reader : readers[use.resource]) dependencies[p].insert(reader);
        readers[use.resource].clear();
        last_writer[use.resource] = static_cast<RenderPassId>(p);
      } else {
        readers[use.resource].push_back(static_cast<RenderPassId>(p));
      }
    }
  }

  std::vector<std::vector<RenderPassId>> outgoing(pass_count);
  std::vector<std::size_t> indegree(pass_count, 0);
  for (std::size_t p = 0; p < pass_count; ++p) {
    indegree[p] = dependencies[p].size();
    for (RenderPassId dependency : dependencies[p]) outgoing[dependency].push_back(static_cast<RenderPassId>(p));
  }
  std::priority_queue<RenderPassId, std::vector<RenderPassId>, std::greater<>> ready;
  for (std::size_t p = 0; p < pass_count; ++p) {
    if (indegree[p] == 0) ready.push(static_cast<RenderPassId>(p));
  }
  RenderPlan plan;
  plan.pass_order.reserve(pass_count);
  while (!ready.empty()) {
    const RenderPassId pass = ready.top();
    ready.pop();
    plan.pass_order.push_back(pass);
    for (RenderPassId next : outgoing[pass]) {
      if (--indegree[next] == 0) ready.push(next);
    }
  }
  if (plan.pass_order.size() != pass_count) throw std::logic_error("render graph contains a dependency cycle");

  std::vector<std::size_t> scheduled_index(pass_count);
  for (std::size_t i = 0; i < plan.pass_order.size(); ++i) scheduled_index[plan.pass_order[i]] = i;
  struct LastAccess { bool valid = false; RenderAccess access = RenderAccess::ShaderRead; };
  std::vector<LastAccess> last_access(resource_count);
  std::vector<std::size_t> first_use(resource_count, std::numeric_limits<std::size_t>::max());
  std::vector<std::size_t> last_use(resource_count, 0);
  for (std::size_t position = 0; position < plan.pass_order.size(); ++position) {
    const RenderPassId pass = plan.pass_order[position];
    for (const RenderUse& use : passes_[pass].uses) {
      first_use[use.resource] = std::min(first_use[use.resource], position);
      last_use[use.resource] = position;
      LastAccess& previous = last_access[use.resource];
      if (previous.valid && (writes(previous.access) || writes(use.access))) {
        plan.barriers.push_back({use.resource, pass, previous.access, use.access});
      }
      previous = {true, use.access};
    }
  }

  struct Slot { RenderResourceDesc desc; std::size_t last_use; };
  std::vector<Slot> slots;
  for (std::size_t resource = 0; resource < resource_count; ++resource) {
    if (first_use[resource] == std::numeric_limits<std::size_t>::max()) continue;
    const RenderResourceDesc desc = resources_[resource].desc;
    std::uint32_t slot_index = std::numeric_limits<std::uint32_t>::max();
    if (desc.transient) {
      for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (slots[slot].desc == desc && slots[slot].last_use < first_use[resource]) {
          slot_index = static_cast<std::uint32_t>(slot);
          slots[slot].last_use = last_use[resource];
          break;
        }
      }
      if (slot_index == std::numeric_limits<std::uint32_t>::max()) {
        slot_index = static_cast<std::uint32_t>(slots.size());
        slots.push_back({desc, last_use[resource]});
      }
    }
    plan.allocations.push_back({static_cast<RenderResourceId>(resource),
                                first_use[resource], last_use[resource], slot_index});
  }
  plan.transient_slot_count = static_cast<std::uint32_t>(slots.size());
  return plan;
}

} // namespace ax::game
