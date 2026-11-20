#include "runtime/core/secure_memory.hpp"
#include "runtime/coro/actor.hpp"
#include "runtime/coro/channel.hpp"
#include "runtime/coro/coro.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace {
int passed = 0;
int failed = 0;

void check(bool condition, const char* name) {
  if (condition) {
    ++passed;
    std::cout << "PASS " << name << '\n';
  } else {
    ++failed;
    std::cout << "FAIL " << name << '\n';
  }
}
} // namespace

int main() {
  {
    constexpr std::size_t count = 100000;
    ax::SpscRingBuffer<std::uint64_t, 256> channel;
    std::thread producer([&] {
      for (std::uint64_t value = 0; value < count; ++value) {
        while (!channel.try_push(std::uint64_t(value))) std::this_thread::yield();
      }
    });
    bool ordered = true;
    for (std::uint64_t expected = 0; expected < count; ++expected) {
      std::optional<std::uint64_t> value;
      while (!(value = channel.try_pop())) std::this_thread::yield();
      if (*value != expected) ordered = false;
    }
    producer.join();
    check(ordered && channel.size_approx() == 0,
          "cache-line-separated SPSC transfers 100000 values in order");

    ax::SpscRingBuffer<int, 8> left, right;
    (void)left.try_push(11);
    (void)right.try_push(22);
    std::array<ax::SpscRingBuffer<int, 8>*, 2> inputs{&left, &right};
    std::size_t cursor = 0;
    const auto first = ax::select_recv(inputs, cursor);
    const auto second = ax::select_recv(inputs, cursor);
    check(first && second && first->first == 0 && first->second == 11 &&
          second->first == 1 && second->second == 22,
          "select_recv multiplexes queues with rotating fairness");
  }

  {
    constexpr std::size_t producers = 4;
    constexpr std::size_t consumers = 4;
    constexpr std::size_t per_producer = 25000;
    constexpr std::size_t total = producers * per_producer;
    constexpr std::uint32_t stop_value = UINT32_MAX;
    ax::MpmcRingBuffer<std::uint32_t, 1024> queue;
    auto seen = std::make_unique<std::atomic<std::uint8_t>[]>(total);
    for (std::size_t i = 0; i < total; ++i) seen[i].store(0, std::memory_order_relaxed);
    std::atomic<std::size_t> consumed{0};
    std::atomic<bool> invalid{false};
    std::vector<std::thread> consumer_workers;
    std::vector<std::thread> producer_workers;
    consumer_workers.reserve(consumers);
    producer_workers.reserve(producers);
    for (std::size_t consumer = 0; consumer < consumers; ++consumer) {
      consumer_workers.emplace_back([&] {
        for (;;) {
          auto value = queue.try_pop();
          if (!value) { std::this_thread::yield(); continue; }
          if (*value == stop_value) break;
          if (*value >= total || seen[*value].fetch_add(1, std::memory_order_relaxed) != 0) {
            invalid.store(true, std::memory_order_relaxed);
          }
          consumed.fetch_add(1, std::memory_order_release);
        }
      });
    }
    for (std::size_t producer = 0; producer < producers; ++producer) {
      producer_workers.emplace_back([&, producer] {
        for (std::size_t i = 0; i < per_producer; ++i) {
          const auto value = static_cast<std::uint32_t>(producer * per_producer + i);
          while (!queue.try_push(std::uint32_t(value))) std::this_thread::yield();
        }
      });
    }
    for (auto& producer : producer_workers) producer.join();
    for (std::size_t i = 0; i < consumers; ++i) {
      while (!queue.try_push(std::uint32_t(stop_value))) std::this_thread::yield();
    }
    for (auto& consumer : consumer_workers) consumer.join();
    bool all_seen = true;
    for (std::size_t i = 0; i < total; ++i) all_seen = all_seen && seen[i].load() == 1;
    check(!invalid.load() && consumed.load() == total && all_seen,
          "bounded MPMC delivers 100000 values exactly once with 4 producers and consumers");
  }

  {
    constexpr std::size_t producers = 4;
    constexpr std::size_t per_producer = 25000;
    constexpr std::size_t total = producers * per_producer;
    std::atomic<std::size_t> handled{0};
    std::uint64_t sum = 0;
    ax::Actor<std::uint32_t, 1024> actor([&](std::uint32_t&& message) {
      sum += message;
      handled.fetch_add(1, std::memory_order_release);
    });
    const auto started_at = std::chrono::steady_clock::now();
    std::thread consumer([&] {
      while (handled.load(std::memory_order_acquire) < total) {
        if (actor.poll(256) == 0) std::this_thread::yield();
      }
    });
    std::vector<std::thread> senders;
    for (std::size_t producer = 0; producer < producers; ++producer) {
      senders.emplace_back([&, producer] {
        for (std::size_t i = 0; i < per_producer; ++i) {
          const auto message = static_cast<std::uint32_t>(producer * per_producer + i);
          while (!actor.try_send(std::uint32_t(message))) std::this_thread::yield();
        }
      });
    }
    for (auto& sender : senders) sender.join();
    consumer.join();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_at).count();
    std::cout << "BENCH actor mailbox: " << total << " messages, "
              << static_cast<std::uint64_t>(static_cast<double>(total) / seconds) << " msg/s\n";
    check(handled.load() == total && sum == (static_cast<std::uint64_t>(total) * (total - 1)) / 2,
          "single-owner actor processes 100000 messages from concurrent senders");
  }

  {
    ax::TaskScope scope(4);
    std::atomic<int> started{0};
    std::atomic<int> stopped{0};
    for (int i = 0; i < 4; ++i) {
      scope.spawn([&](std::stop_token token) {
        started.fetch_add(1, std::memory_order_release);
        while (!token.stop_requested()) std::this_thread::yield();
        stopped.fetch_add(1, std::memory_order_relaxed);
      });
    }
    std::thread canceller([&] {
      while (started.load(std::memory_order_acquire) != 4) std::this_thread::yield();
      scope.cancel();
    });
    scope.join();
    canceller.join();
    check(started.load() == 4 && stopped.load() == 4,
          "structured task scope cancels and joins every child");
  }

  {
    std::array<std::uint8_t, 64> secret{};
    secret.fill(0xa5);
    ax::runtime::secure_zero(secret.data(), secret.size());
    check(std::all_of(secret.begin(), secret.end(), [](std::uint8_t value) { return value == 0; }),
          "secure_zero clears secret bytes through volatile stores");
  }

  std::cout << "RESULT: " << passed << " passed, " << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}
