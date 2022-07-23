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

class Executor {
 public:
  Executor(IoQueue queue);
  Executor(const Executor&) = delete;
  Executor(Executor&&) = delete;
  Executor& operator=(const Executor&) = delete;
  Executor& operator=(Executor&&) = delete;
  ~Executor();

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
  CpuContext* main();

  void run_task(Task* task);
  void init_task(Task* task);

  Task* allocate_task();
  void release_task(Task* task);

  IoQueue m_queue;
  std::size_t m_io_blocked{0};
  CpuContext m_main{};
  TaskList m_freelist{}; // cached free tasks
                         // TODO: add a limit to how many tasks can be cached
  TaskList m_ready{};    // ready tasks
};

void yield();

template <typename F>
void spawn(F&& fn) {
  auto* task = current_task();
  task->owner->spawn(std::forward<F>(fn));
}

}  // namespace rt