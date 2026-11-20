#include "runtime/coro/coro.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <utility>
#include <exception>
namespace ax {
CoroScheduler::CoroScheduler(int workers): workers_(workers>0?workers:4){
  for(int i=0;i<workers_;i++) deques_.push_back(std::make_unique<WorkStealingDeque>());
}

void WorkStealingDeque::push(std::function<void()> fn){
  std::lock_guard<std::mutex> lock(mutex_);
  tasks_.push_back(std::move(fn));
}

bool WorkStealingDeque::pop(std::function<void()>& fn){
  std::lock_guard<std::mutex> lock(mutex_);
  if(tasks_.empty()) return false;
  fn=std::move(tasks_.back());
  tasks_.pop_back();
  return true;
}

bool WorkStealingDeque::steal(std::function<void()>& fn){
  std::lock_guard<std::mutex> lock(mutex_);
  if(tasks_.empty()) return false;
  fn=std::move(tasks_.front());
  tasks_.pop_front();
  return true;
}

void CoroScheduler::spawn(std::function<void()> fn){
  outstanding_.fetch_add(1, std::memory_order_release);
  try {
    deques_[0]->push(std::move(fn));
  } catch (...) {
    outstanding_.fetch_sub(1, std::memory_order_acq_rel);
    throw;
  }
  wake_.notify_one();
}
void CoroScheduler::run(){
  completed_.store(0, std::memory_order_relaxed);
  std::vector<std::thread> th;
  for(int w=0;w<workers_;w++){
    th.emplace_back([this,w]{
      while(outstanding_.load(std::memory_order_acquire)>0){
        std::function<void()> task;
        bool found=deques_[w]->pop(task);
        if(!found){
          for(int i=0;i<workers_;i++){
            if(i==w) continue;
            if(deques_[i]->steal(task)){ found=true; break; }
          }
        }
        if(found){
          if(task) task();
          completed_.fetch_add(1, std::memory_order_relaxed);
          outstanding_.fetch_sub(1, std::memory_order_acq_rel);
          wake_.notify_all();
        } else {
          std::unique_lock<std::mutex> lock(wake_mutex_);
          wake_.wait_for(lock, std::chrono::milliseconds(1));
        }
      }
    });
  }
  for(auto &t: th) t.join();
}

TaskScope::TaskScope(int workers) : scheduler_(workers) {}

TaskScope::~TaskScope() {
  try {
    join();
  } catch (...) {
    // An exception is observable through explicit join(); destructors must not throw.
  }
}

void TaskScope::cancel() noexcept {
  stop_source_.request_stop();
}

void TaskScope::record_failure(std::exception_ptr failure) noexcept {
  std::lock_guard<std::mutex> lock(failure_mutex_);
  if (!failure_) failure_ = std::move(failure);
}

void TaskScope::join() {
  {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (joined_) return;
    joined_ = true;
  }
  scheduler_.run();
  std::exception_ptr failure;
  {
    std::lock_guard<std::mutex> lock(failure_mutex_);
    failure = failure_;
  }
  if (failure) std::rethrow_exception(failure);
}
} // namespace ax
