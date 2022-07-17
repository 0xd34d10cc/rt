#pragma once

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>
#include <system_error>

#include "handle.hpp"
#include "cpu_context.hpp"
#include "io_queue.hpp"


namespace rt {

class Executor;

namespace detail {

struct BaseTaskFn {
  virtual void call() = 0;
  virtual ~BaseTaskFn() {};
};

template <typename F>
struct TaskFnImpl : BaseTaskFn, F {
  template <typename Param>
  TaskFnImpl(Param&& f) : F(std::forward<Param>(f)) {}
  virtual void call() override { F::operator()(); }
};

struct Task {
  static constexpr std::uint64_t STACK_SIZE = 1024 * 32;

  CpuContext context{};
  std::size_t fn_size{0};
  char* stack{nullptr};

  Executor* owner{nullptr};
  Task* next{nullptr};

  ~Task() { clear(); }

  BaseTaskFn* fn_ptr(std::size_t off) const {
    auto* p = stack + STACK_SIZE - off;
    return reinterpret_cast<BaseTaskFn*>(p);
  }

  void call() {
    assert(fn_size > 0);
    auto* p = fn_ptr(fn_size);
    p->call();
  }

  void clear() {
    if (fn_size > 0) {
      auto* p = fn_ptr(fn_size);
      std::destroy_at(p);
      fn_size = 0;
    }
  }

  template <typename F>
  void set(F&& fn) {
    using FnValue = TaskFnImpl<std::decay_t<F>>;
    constexpr std::size_t off = sizeof(FnValue);
    static_assert(off <= STACK_SIZE, "Too big to fit");
    assert(stack);
    clear();
    // FIXME: align p according to alignof(FnValue)
    auto* p = fn_ptr(off);
    std::construct_at(reinterpret_cast<FnValue*>(p), std::forward<F>(fn));
    fn_size = off;
  }

  void finalize();
  void yield();
  std::error_code register_io(Handle h);
  void block_on_io();
};

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

}  // namespace detail


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

  friend struct detail::Task;

 private:
  CpuContext* main();

  void run_task(detail::Task* task);
  void init_task(detail::Task* task);

  detail::Task* allocate_task();
  void release_task(detail::Task* task);

  IoQueue m_queue;
  std::size_t m_io_blocked{0};
  CpuContext m_main{};
  detail::TaskList m_freelist{}; // cached free tasks
                                 // TODO: add a limit to how many tasks can be cached
  detail::TaskList m_ready{};    // ready tasks
};

void yield();

template <typename F>
void spawn(F&& fn) {
  auto* task = detail::current_task();
  task->owner->spawn(std::forward<F>(fn));
}

}  // namespace rt