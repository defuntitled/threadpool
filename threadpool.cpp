#include "threadpool.hpp"

namespace threadpool {
void ThreadPool::Run() {
  while (still_working_) {
    std::unique_lock<std::mutex> queue_lock{queue_mtx_};
    queue_var_.wait(queue_lock,
                    [this]() { return !tasks_.empty() || !still_working_; });
    if (!tasks_.empty()) {
      auto task = std::move(tasks_.front());
      tasks_.pop();
      queue_lock.unlock();
      task.second.get();
      std::lock_guard<std::mutex> submitted_tasks_lock{submitted_tasks_mtx_};
      submitted_tasks_.insert(task.first);
      submitted_tasks_var_.notify_all();
    }
  }
}
ThreadPool::ThreadPool(size_t threads_count) {
  threads_count_ = threads_count;
  threads_ = std::vector<std::thread>{threads_count_};
  for (size_t i = 0; i < threads_count_; ++i) {
    threads_.emplace_back(&ThreadPool::Run, this);
  }
}

void ThreadPool::Wait(std::int64_t task_id) {
  std::unique_lock<std::mutex> submitted_tasks_lock{submitted_tasks_mtx_};
  submitted_tasks_var_.wait(submitted_tasks_lock, [this, task_id]() {
    return submitted_tasks_.find(task_id) != submitted_tasks_.end();
  });
}

void ThreadPool::WaitAll() {
  std::unique_lock<std::mutex> submitted_tasks_lock{submitted_tasks_mtx_};
  submitted_tasks_var_.wait(submitted_tasks_lock, [this]() {
    std::lock_guard<std::mutex> q_lock{queue_mtx_};
    return tasks_.empty() && next_task_id_ == submitted_tasks_.size();
  });
}

bool ThreadPool::IsSubmitted(std::int64_t task_id) {
  std::lock_guard<std::mutex> submitted_tasks_lock{submitted_tasks_mtx_};
  return submitted_tasks_.find(task_id) != submitted_tasks_.end();
}

ThreadPool::~ThreadPool(){
    still_working_=false;
    for( auto& thread: threads_){
        queue_var_.notify_all();
        thread.join();
    }
}

} // namespace threadpool