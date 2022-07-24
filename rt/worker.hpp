#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>
#include <system_error>

#include "handle.hpp"
#include "cpu_context.hpp"
#include "io_engine.hpp"
#include "worker_queue.hpp"
#include "task.hpp"
#include "random.hpp"

#if 0
#include <iostream>
#define TRACE_TASK(t, msg) \
  std::cout << "Task[" << (void*)(t->owner) << "|" << (void*)(t) << "]: " msg "\n"
#else
#define TRACE_TASK(t, msg) \
  do {                     \
  } while (false)
#endif

namespace rt {

struct TaskList {
  TaskList() noexcept = default;
  TaskList(TaskList&& other) noexcept : first(other.first), last(other.last) {
    other.first = nullptr;
    other.last = nullptr;
  }

  Task* first{nullptr};
  Task* last{nullptr};

  bool empty() const noexcept { return first == nullptr; }

  Task* pop_front() noexcept {
    auto* task = first;
    if (task) {
      first = task->next;
      task->next = nullptr;

      if (task == last) {
        last = nullptr;
      }
    }

    return task;
  }

  void push_front(Task* task) noexcept {
    task->next = first;
    first = task;

    if (!last) {
      last = first;
    }
  }

  void push_back(Task* task) noexcept {
    if (!last) {
      first = task;
      last = task;
      return;
    }

    last->next = task;
    last = task;
  }
};

Task* current_task();

class Worker {
 public:
  Worker(IoEngine io) noexcept;
  Worker(const Worker&) = delete;
  Worker(Worker&&) noexcept = default;
  Worker& operator=(const Worker&) = delete;
  Worker& operator=(Worker&&) = delete;
  ~Worker() noexcept;

  template <typename F>
  void spawn(F&& fn) {
    auto* task = allocate_task();
    task->set(std::forward<F>(fn));
    init_task(task);
    TRACE_TASK(task, "allocated");
    m_ready.push(task);
  }

  void run(Worker** workers, std::size_t n) noexcept;

  friend struct Task;

  Task* steal() noexcept {
    auto* task = m_ready.steal();
    if (task) {
      TRACE_TASK(task, "stolen");
      task->owner = nullptr;
    }
    return task;
  }
  IoEngine* io() noexcept { return &m_io; }

 private:
  void run(CpuContext* current) noexcept;
  bool wait_io() noexcept;

  Task* next_task() noexcept;
  Task* try_steal() noexcept;
  void run_task(Task* task, CpuContext* current) noexcept;
  void init_task(Task* task) noexcept;

  Task* allocate_task() noexcept;
  void release_task(Task* task) noexcept;

  IoEngine m_io;
  std::size_t m_io_blocked{0};
  CpuContext m_main{};
  TaskList m_freelist{};  // cached free tasks
                          // TODO: add a limit on how many tasks can be cached
  WorkerQueue m_ready{};  // ready tasks

  XorShiftRng m_rng{};
  Worker** m_workers{nullptr};
  std::size_t m_n_workers{0};
};

void yield();

template <typename F>
void spawn(F&& fn) {
  auto* task = current_task();
  task->owner->spawn(std::forward<F>(fn));
}

}  // namespace rt