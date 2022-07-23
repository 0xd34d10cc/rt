#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>
#include <system_error>

#include "handle.hpp"
#include "cpu_context.hpp"
#include "io_queue.hpp"
#include "task.hpp"


namespace rt {

struct TaskList {
  Task* first{nullptr};
  Task* last{nullptr};

  bool empty() const { return first == nullptr; }

  Task* pop_front() {
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

  void push_front(Task* task) {
    task->next = first;
    first = task;

    if (!last) {
      last = first;
    }
  }

  void push_back(Task* task) {
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
  Worker(IoQueue queue);
  Worker(const Worker&) = delete;
  Worker(Worker&&) = delete;
  Worker& operator=(const Worker&) = delete;
  Worker& operator=(Worker&&) = delete;
  ~Worker();

  template <typename F>
  void spawn(F&& fn) {
    auto* task = allocate_task();
    task->set(std::forward<F>(fn));
    init_task(task);
    m_ready.push_back(task);
  }
  void run();

  friend struct Task;

 private:
  void run(CpuContext* current);
  bool wait_io();

  Task* next_task();
  void run_task(Task* task, CpuContext* current);
  void init_task(Task* task);

  Task* allocate_task();
  void release_task(Task* task);

  IoQueue m_queue;
  std::size_t m_io_blocked{0};
  CpuContext m_main{};
  TaskList m_freelist{}; // cached free tasks
                         // TODO: add a limit on how many tasks can be cached
  TaskList m_ready{};    // ready tasks
};

void yield();

template <typename F>
void spawn(F&& fn) {
  auto* task = current_task();
  task->owner->spawn(std::forward<F>(fn));
}

}  // namespace rt