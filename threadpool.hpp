#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

namespace threadpool {
class ThreadPool {
private:
  std::queue<std::pair<std::int64_t, std::future<void>>> tasks_;
  std::mutex queue_mtx_;
  std::condition_variable queue_var_;
  size_t threads_count_;
  std::unordered_set<std::int64_t> submitted_tasks_;
  std::mutex submitted_tasks_mtx_;
  std::condition_variable submitted_tasks_var_;
  std::vector<std::thread> threads_;
  std::atomic_int64_t next_task_id_{0};
  std::atomic_bool still_working_{false};
  void Run();

public:
  ThreadPool(size_t threads_count);
  void Wait(std::int64_t task_id);
  void WaitAll();
  bool IsSubmitted(std::int64_t task_id);
  template <typename Func, typename... Args>
  std::int64_t AddTask(const Func &task_func, Args &&...args) {
    const std::int64_t task_idx = next_task_id_++;

    std::lock_guard<std::mutex> q_lock(queue_mtx_);
    tasks_.emplace(std::async(std::launch::deferred, task_func, args...),
                   task_idx);
    queue_var_.notify_one();
    return task_idx;
  }
  ~ThreadPool();
};
} // namespace threadpool