#ifndef MAIN_THREAD_SYNC_H
#define MAIN_THREAD_SYNC_H

#include <mutex>
#include <condition_variable>
#include <deque>
#include <memory>
#include "src/heap/safepoint.h"
#include "log.h"

namespace mmtk {

class MainThreadSynchronizer;

class TaskAwaiter {
  std::mutex m_ {};
  std::condition_variable cv_ {};
  bool task_completed_ = false;

  void Complete() {
    std::unique_lock<std::mutex> lock(m_);
    task_completed_ = true;
    cv_.notify_all();
  }

  friend class MainThreadSynchronizer;

 public:
  void Wait() {
    std::unique_lock<std::mutex> lock(m_);
    while (!task_completed_) cv_.wait(lock);
  }
};

class MainThreadSynchronizer {
  std::mutex m_ {};
  std::condition_variable cv_ {};
  bool gc_in_progress_ = false;
  std::deque<std::function<void()>> main_thread_tasks_ {};
  std::unique_ptr<v8::internal::SafepointScope> safepoint_scope_ = nullptr;

  void DrainTasks() {
    while (!main_thread_tasks_.empty()) {
      MMTK_LOG("[main-thread] Run One Task\n");
      auto task = main_thread_tasks_.front();
      main_thread_tasks_.pop_front();
      task();
    }
  }

 public:
  void Block() {
    MMTK_LOG("[main-thread] Blocked\n");
    gc_in_progress_ = true;
    std::unique_lock<std::mutex> lock(m_);
    while (gc_in_progress_) {
      DrainTasks();
      MMTK_LOG("[main-thread] Sleep\n");
      cv_.wait(lock);
      MMTK_LOG("[main-thread] Wake\n");
    }
    DrainTasks();
    MMTK_LOG("[main-thread] Resumed\n");
  }

  void WakeUp() {
    std::unique_lock<std::mutex> lock(m_);
    gc_in_progress_ = false;
    cv_.notify_all();
  }

  void RunMainThreadTask(std::function<void()> task) {
    auto awaiter = std::make_unique<TaskAwaiter>();
    {
      std::unique_lock<std::mutex> lock(m_);
      main_thread_tasks_.push_back([task, &awaiter]() {
        task();
        awaiter->Complete();
      });
      cv_.notify_all();
    }
    awaiter->Wait();
  }

  void EnterSafepoint(v8::internal::Heap* heap) {
    safepoint_scope_.reset(new v8::internal::SafepointScope(heap));
  }

  void ExitSafepoint() {
    safepoint_scope_ = nullptr;
  }
};

}

#endif // MAIN_THREAD_SYNC_H