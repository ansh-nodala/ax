#pragma once
#include <functional>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <stop_token>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>
#include <memory>
namespace ax {
extern "C" void ax_coro_switch(void** from, void** to);
class WorkStealingDeque {
 public:
  void push(std::function<void()> fn);
  bool pop(std::function<void()>& fn);
  bool steal(std::function<void()>& fn);
 private:
  std::mutex mutex_;
  std::deque<std::function<void()>> tasks_;
};
class CoroScheduler {
 public:
  explicit CoroScheduler(int workers=0);
  void spawn(std::function<void()> fn);
  void run();
  std::size_t completed() const { return completed_; }
 private:
  std::vector<std::unique_ptr<WorkStealingDeque>> deques_;
  std::atomic<std::size_t> completed_{0};
  std::atomic<std::size_t> outstanding_{0};
  std::mutex wake_mutex_;
  std::condition_variable wake_;
  int workers_=0;
};

// Structured task lifetime over the work-stealing scheduler. join() waits for
// every child; cancel() propagates a cooperative stop token to all children.
class TaskScope {
 public:
  explicit TaskScope(int workers = 0);
  TaskScope(const TaskScope&) = delete;
  TaskScope& operator=(const TaskScope&) = delete;
  ~TaskScope();

  void cancel() noexcept;
  void join();

  template <typename Function>
  void spawn(Function&& function) {
    using Fn = std::decay_t<Function>;
    static_assert(std::is_invocable_v<Fn&, std::stop_token> || std::is_invocable_v<Fn&>,
                  "TaskScope tasks must accept a stop_token or no arguments");
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (joined_) throw std::logic_error("cannot spawn into a joined task scope");
    const std::stop_token token = stop_source_.get_token();
    scheduler_.spawn([this, token, task = Fn(std::forward<Function>(function))]() mutable {
      if (token.stop_requested()) return;
      try {
        if constexpr (std::is_invocable_v<Fn&, std::stop_token>) std::invoke(task, token);
        else std::invoke(task);
      } catch (...) {
        record_failure(std::current_exception());
        stop_source_.request_stop();
      }
    });
  }

 private:
  void record_failure(std::exception_ptr failure) noexcept;

  CoroScheduler scheduler_;
  std::stop_source stop_source_;
  std::mutex lifecycle_mutex_;
  std::mutex failure_mutex_;
  std::exception_ptr failure_;
  bool joined_ = false;
};
} // namespace ax
