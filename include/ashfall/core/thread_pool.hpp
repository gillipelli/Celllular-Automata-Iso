#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
namespace ashfall {
// Persistent static row bands. Callback and context are borrowed until run returns.
class ThreadPool {
public:
  using Job = void (*)(void *, int, int);
  ThreadPool(int rows, int count) : rows_(rows), count_(count) {
    for (int i = 1; i < count_; ++i)
      workers_.emplace_back([this, i] { worker(i); });
  }
  ~ThreadPool() {
    {
      std::lock_guard lock(mutex_);
      stop_ = true;
    }
    start_.notify_all();
    for (auto &worker : workers_)
      worker.join();
  }
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  void run(Job job, void *context) {
    if (count_ == 1) {
      job(context, 0, rows_);
      return;
    }
    {
      std::lock_guard lock(mutex_);
      job_ = job;
      context_ = context;
      remaining_ = count_ - 1;
      ++generation_;
    }
    start_.notify_all();
    job(context, 0, rows_ / count_);
    std::unique_lock lock(mutex_);
    done_.wait(lock, [this] { return remaining_ == 0; });
  }

private:
  void worker(int index) {
    uint64_t seen = 0;
    std::unique_lock lock(mutex_);
    for (;;) {
      start_.wait(lock, [&] { return stop_ || generation_ != seen; });
      if (stop_)
        return;
      seen = generation_;
      auto job = job_;
      void *context = context_;
      lock.unlock();
      job(context, index * rows_ / count_, (index + 1) * rows_ / count_);
      lock.lock();
      if (--remaining_ == 0)
        done_.notify_one();
    }
  }
  int rows_, count_, remaining_ = 0;
  uint64_t generation_ = 0;
  bool stop_ = false;
  Job job_ = nullptr;
  void *context_ = nullptr;
  std::mutex mutex_;
  std::condition_variable start_, done_;
  std::vector<std::thread> workers_;
};
} // namespace ashfall
