#pragma once

#include "runtime/coro/channel.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace ax {

// A serially-polled actor mailbox. Multiple senders are safe; poll() has one
// active consumer so actor-owned state is only touched by its handler thread.
template <typename Message, std::size_t Capacity>
class Actor {
 public:
  using Handler = std::function<void(Message&&)>;

  explicit Actor(Handler handler) : handler_(std::move(handler)) {
    static_assert(std::is_nothrow_move_constructible_v<Message>);
    if (!handler_) throw std::invalid_argument("actor requires a message handler");
  }
  Actor(const Actor&) = delete;
  Actor& operator=(const Actor&) = delete;

  bool try_send(Message&& message) noexcept { return mailbox_.try_push(std::move(message)); }

  std::size_t poll(std::size_t maximum = Capacity) {
    if (polling_.test_and_set(std::memory_order_acquire)) return 0;
    struct ResetFlag {
      std::atomic_flag& flag;
      ~ResetFlag() { flag.clear(std::memory_order_release); }
    } reset{polling_};
    std::size_t handled = 0;
    while (handled < maximum) {
      auto message = mailbox_.try_pop();
      if (!message) break;
      handler_(std::move(*message));
      ++handled;
    }
    return handled;
  }

 private:
  MpmcRingBuffer<Message, Capacity> mailbox_;
  Handler handler_;
  std::atomic_flag polling_ = ATOMIC_FLAG_INIT;
};

} // namespace ax
