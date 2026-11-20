#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <cstdint>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <stdexcept>
#include <vector>

namespace ax {

template<typename T, std::size_t Capacity>
class SpscRingBuffer {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "SPSC capacity must be a nonzero power of two");
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "SPSC elements must be nothrow move constructible");
  static_assert(std::is_nothrow_destructible_v<T>, "SPSC elements must be nothrow destructible");

  struct alignas(64) Cursor { std::atomic<std::size_t> value{0}; };

 public:
  bool try_push(T&& value) {
    const std::size_t tail = tail_.value.load(std::memory_order_relaxed);
    const std::size_t head = head_.value.load(std::memory_order_acquire);
    if (tail - head == Capacity) return false;
    slots_[tail & (Capacity - 1)].emplace(std::move(value));
    tail_.value.store(tail + 1, std::memory_order_release);
    return true;
  }

  bool try_push(const T& value) requires std::is_copy_constructible_v<T> {
    const std::size_t tail = tail_.value.load(std::memory_order_relaxed);
    const std::size_t head = head_.value.load(std::memory_order_acquire);
    if (tail - head == Capacity) return false;
    slots_[tail & (Capacity - 1)].emplace(value);
    tail_.value.store(tail + 1, std::memory_order_release);
    return true;
  }

  std::optional<T> try_pop() noexcept {
    const std::size_t head = head_.value.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.value.load(std::memory_order_acquire);
    if (head == tail) return std::nullopt;
    auto& slot = slots_[head & (Capacity - 1)];
    std::optional<T> value(std::in_place, std::move(*slot));
    slot.reset();
    head_.value.store(head + 1, std::memory_order_release);
    return value;
  }

  std::size_t size_approx() const noexcept {
    const auto tail = tail_.value.load(std::memory_order_acquire);
    const auto head = head_.value.load(std::memory_order_acquire);
    return tail - head;
  }

 private:
  Cursor head_;
  Cursor tail_;
  alignas(64) std::array<std::optional<T>, Capacity> slots_{};
};

// Bounded Vyukov sequence-number queue. Producers and consumers never allocate
// after construction; the sequence counters publish each cell's object lifetime.
template<typename T, std::size_t Capacity>
class MpmcRingBuffer {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "MPMC capacity must be a nonzero power of two");
  static_assert(std::is_nothrow_move_constructible_v<T>,
                "MPMC elements must be nothrow move constructible");
  static_assert(std::is_nothrow_destructible_v<T>, "MPMC elements must be nothrow destructible");

  struct alignas(64) Cell {
    std::atomic<std::size_t> sequence{0};
    alignas(T) std::byte storage[sizeof(T)];
  };

 public:
  MpmcRingBuffer() noexcept {
    for (std::size_t i = 0; i < Capacity; ++i) cells_[i].sequence.store(i, std::memory_order_relaxed);
  }
  MpmcRingBuffer(const MpmcRingBuffer&) = delete;
  MpmcRingBuffer& operator=(const MpmcRingBuffer&) = delete;

  ~MpmcRingBuffer() {
    while (try_pop().has_value()) {}
  }

  bool try_push(T&& value) noexcept {
    std::size_t position = enqueue_.load(std::memory_order_relaxed);
    Cell* cell = nullptr;
    for (;;) {
      cell = &cells_[position & (Capacity - 1)];
      const std::size_t sequence = cell->sequence.load(std::memory_order_acquire);
      const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                       static_cast<std::intptr_t>(position);
      if (difference == 0) {
        if (enqueue_.compare_exchange_weak(position, position + 1,
                                           std::memory_order_relaxed,
                                           std::memory_order_relaxed)) break;
      } else if (difference < 0) {
        return false;
      } else {
        position = enqueue_.load(std::memory_order_relaxed);
      }
    }
    std::construct_at(reinterpret_cast<T*>(cell->storage), std::move(value));
    cell->sequence.store(position + 1, std::memory_order_release);
    return true;
  }

  std::optional<T> try_pop() noexcept {
    std::size_t position = dequeue_.load(std::memory_order_relaxed);
    Cell* cell = nullptr;
    for (;;) {
      cell = &cells_[position & (Capacity - 1)];
      const std::size_t sequence = cell->sequence.load(std::memory_order_acquire);
      const std::intptr_t difference = static_cast<std::intptr_t>(sequence) -
                                       static_cast<std::intptr_t>(position + 1);
      if (difference == 0) {
        if (dequeue_.compare_exchange_weak(position, position + 1,
                                           std::memory_order_relaxed,
                                           std::memory_order_relaxed)) break;
      } else if (difference < 0) {
        return std::nullopt;
      } else {
        position = dequeue_.load(std::memory_order_relaxed);
      }
    }
    T* object = std::launder(reinterpret_cast<T*>(cell->storage));
    std::optional<T> value(std::in_place, std::move(*object));
    std::destroy_at(object);
    cell->sequence.store(position + Capacity, std::memory_order_release);
    return value;
  }

 private:
  alignas(64) std::atomic<std::size_t> enqueue_{0};
  alignas(64) std::atomic<std::size_t> dequeue_{0};
  std::array<Cell, Capacity> cells_{};
};

template<typename T, std::size_t Capacity, std::size_t Count>
std::optional<std::pair<std::size_t, T>> select_recv(
    const std::array<SpscRingBuffer<T, Capacity>*, Count>& channels,
    std::size_t& next_channel) {
  static_assert(Count > 0, "select_recv requires at least one channel");
  for (std::size_t offset = 0; offset < Count; ++offset) {
    const std::size_t index = (next_channel + offset) % Count;
    auto value = channels[index]->try_pop();
    if (value) {
      next_channel = (index + 1) % Count;
      return std::pair<std::size_t, T>{index, std::move(*value)};
    }
  }
  return std::nullopt;
}

template<typename T>
class Chan {
 public:
  explicit Chan(std::size_t cap=0): cap_(cap){}
  void send(T v){ std::unique_lock<std::mutex> lk(mu_); if(cap_>0) cv_push_.wait(lk, [&]{ return q_.size()<cap_ || closed_; }); if(closed_) return; q_.push(std::move(v)); cv_pop_.notify_one(); }
  std::optional<T> recv(){ std::unique_lock<std::mutex> lk(mu_); cv_pop_.wait(lk, [&]{ return !q_.empty() || closed_; }); if(q_.empty()) return std::nullopt; T v=std::move(q_.front()); q_.pop(); cv_push_.notify_one(); return v; }
  void close(){ std::lock_guard<std::mutex> lk(mu_); closed_=true; cv_pop_.notify_all(); cv_push_.notify_all(); }
 private: std::queue<T> q_; std::mutex mu_; std::condition_variable cv_pop_, cv_push_; std::size_t cap_; bool closed_=false;
};
} // namespace ax
