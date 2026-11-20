#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ax::game {

struct Entity {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;
  friend constexpr bool operator==(Entity, Entity) noexcept = default;
};

template <typename... T> struct Reads {};
template <typename... T> struct Writes {};
template <typename ReadSet, typename WriteSet> struct SystemAccess {};

namespace detail {
template <typename T, typename... Ts>
inline constexpr bool contains_type_v = (std::is_same_v<T, Ts> || ...);

template <typename ReadSet, typename WriteSet> struct access_conflict;
template <typename... ARead, typename... AWrite, typename... BRead, typename... BWrite>
struct access_conflict<SystemAccess<Reads<ARead...>, Writes<AWrite...>>,
                       SystemAccess<Reads<BRead...>, Writes<BWrite...>>> {
  static constexpr bool value =
      (contains_type_v<AWrite, BRead...> || ...) ||
      (contains_type_v<AWrite, BWrite...> || ...) ||
      (contains_type_v<BWrite, ARead...> || ...);
};
} // namespace detail

template <typename A, typename B>
inline constexpr bool systems_conflict_v = detail::access_conflict<A, B>::value;

// Dense SoA archetype for entities with one fixed component signature.
// Entity handles carry a generation so removed handles cannot alias new rows.
template <typename... Components>
class ArchetypeWorld {
  static_assert(sizeof...(Components) > 0);
  static_assert((std::is_default_constructible_v<Components> && ...));

  struct Slot {
    std::size_t row = 0;
    std::uint32_t generation = 1;
    bool alive = false;
  };

 public:
  Entity create(Components... components) {
    std::uint32_t index;
    if (free_.empty()) {
      if (slots_.size() >= UINT32_MAX) throw std::length_error("entity capacity exceeded");
      index = static_cast<std::uint32_t>(slots_.size());
      slots_.push_back({});
    } else {
      index = free_.back();
      free_.pop_back();
    }
    const std::size_t row = size_++;
    slots_[index].row = row;
    slots_[index].alive = true;
    entities_.push_back(index);
    append_components(row, std::move(components)...);
    return {index, slots_[index].generation};
  }

  bool alive(Entity entity) const noexcept {
    return entity.index < slots_.size() && slots_[entity.index].alive &&
           slots_[entity.index].generation == entity.generation;
  }

  void destroy(Entity entity) {
    if (!alive(entity)) throw std::out_of_range("destroy called with a stale entity");
    Slot& slot = slots_[entity.index];
    const std::size_t row = slot.row;
    const std::size_t last = size_ - 1;
    if (row != last) {
      move_row(row, last);
      const std::uint32_t moved_index = entities_[last];
      slots_[moved_index].row = row;
      entities_[row] = moved_index;
    }
    pop_components();
    entities_.pop_back();
    --size_;
    slot.alive = false;
    ++slot.generation;
    if (slot.generation == 0) ++slot.generation;
    free_.push_back(entity.index);
  }

  template <typename... Query>
  auto query() {
    static_assert((detail::contains_type_v<Query, Components...> && ...),
                  "query requested a component outside this archetype");
    return std::tuple<std::span<Query>...>{std::span<Query>(component<Query>().data(), size_)...};
  }

  template <typename Component>
  Component& get(Entity entity) {
    static_assert(detail::contains_type_v<Component, Components...>);
    if (!alive(entity)) throw std::out_of_range("component access uses a stale entity");
    return component<Component>()[slots_[entity.index].row];
  }

  std::size_t size() const noexcept { return size_; }

 private:
  template <typename Component>
  std::vector<Component>& component() {
    constexpr std::size_t index = component_index<Component>();
    return std::get<index>(components_);
  }

  template <typename Component, std::size_t I = 0>
  static consteval std::size_t component_index() {
    if constexpr (I >= sizeof...(Components)) {
      static_assert(I < sizeof...(Components), "component is not part of this archetype");
      return 0;
    } else if constexpr (std::is_same_v<Component, std::tuple_element_t<I, std::tuple<Components...>>>) {
      return I;
    } else {
      return component_index<Component, I + 1>();
    }
  }

  template <typename... Values>
  void append_components(std::size_t, Values&&... values) {
    append_component_values(std::index_sequence_for<Components...>{}, std::forward<Values>(values)...);
  }

  template <std::size_t... I, typename... Values>
  void append_component_values(std::index_sequence<I...>, Values&&... values) {
    (std::get<I>(components_).push_back(std::forward<Values>(values)), ...);
  }

  void pop_components() { (std::get<std::vector<Components>>(components_).pop_back(), ...); }

  template <std::size_t... I>
  void move_row_impl(std::size_t row, std::size_t last, std::index_sequence<I...>) {
    ((std::get<I>(components_)[row] = std::move(std::get<I>(components_)[last])), ...);
  }

  void move_row(std::size_t row, std::size_t last) {
    move_row_impl(row, last, std::index_sequence_for<Components...>{});
  }

  std::tuple<std::vector<Components>...> components_;
  std::vector<Slot> slots_;
  std::vector<std::uint32_t> entities_;
  std::vector<std::uint32_t> free_;
  std::size_t size_ = 0;
};

template <typename Function>
void parallel_for(std::size_t count, std::size_t workers, Function&& function) {
  if (workers == 0) throw std::invalid_argument("parallel_for requires at least one worker");
  workers = std::min(workers, std::max<std::size_t>(count, 1));
  std::vector<std::thread> threads;
  threads.reserve(workers > 0 ? workers - 1 : 0);
  const std::size_t chunk = (count + workers - 1) / workers;
  for (std::size_t worker = 0; worker + 1 < workers; ++worker) {
    const std::size_t begin = worker * chunk;
    const std::size_t end = std::min(count, begin + chunk);
    threads.emplace_back([begin, end, &function] { function(begin, end); });
  }
  const std::size_t begin = (workers - 1) * chunk;
  function(begin, count);
  for (std::thread& thread : threads) thread.join();
}

} // namespace ax::game
