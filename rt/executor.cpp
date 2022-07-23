#include "executor.hpp"


extern "C" {
void rt_task_trampoline();
}

namespace rt {

thread_local Task* CURRENT_TASK{nullptr};

Task* current_task() { return CURRENT_TASK; }

void Task::finalize() {
  auto* main = owner->main();
  owner->release_task(this);
  // TODO: resume scheduler within the same coroutine to save on context
  //       switches i.e. make it task=>next_task instead of task=>main=>next_task
  rt_cpu_context_switch(main);
}

void Task::yield() {
  owner->m_ready.push_back(this);
  // TODO: resume scheduler within the same coroutine to save on context
  //       switches i.e. make it task=>next_task instead of
  //       task=>main=>next_task
  rt_cpu_context_swap(&context, owner->main());
}

std::error_code Task::register_io(Handle h) {
  return owner->m_queue.add(h, this);
}

void Task::block_on_io() {
  // NOTE: task ptr should be already saved in m_queue
  ++owner->m_io_blocked;
  // TODO: resume scheduler within the same coroutine to save on context
  //       switches i.e. make it task=>next_task instead of task=>main=>next_task
  rt_cpu_context_swap(&context, owner->main());
}

void yield() {
  auto* task = CURRENT_TASK;
  task->yield();
}

void task_main(Task* task) {
  task->call();
  task->finalize();
}

Executor::Executor(IoQueue queue) : m_queue(std::move(queue)) {}

Executor::~Executor() {
  const auto free_task = [](Task* task) {
    if (task->stack) {
      std::free(task->stack);
    }

    delete task;
  };

  while (auto* task = m_ready.pop_front()) {
    free_task(task);
  }

  while (auto* task = m_freelist.pop_front()) {
    free_task(task);
  }
}

void Executor::run() {
  while (true) {
    while (auto* task = m_ready.pop_front()) {
      run_task(task);
    }

    if (m_io_blocked == 0) {
      break;
    }

    constexpr std::size_t n_events = 64;
    constexpr std::size_t wait_ms = static_cast<std::size_t>(-1); // infinite

    rt::CompletionEvent events[n_events];
    std::size_t n = m_queue.wait(events, n_events, wait_ms);
    assert(n != 0);
    for (std::size_t i = 0; i < n; ++i) {
      --m_io_blocked;
      auto* task = reinterpret_cast<Task*>(events[i].context);
      run_task(task);
    }
  }
}

CpuContext* Executor::main() { return &m_main; }

void Executor::release_task(Task* task) {
  task->reset();
  m_freelist.push_front(task);
}

void Executor::run_task(Task* task) {
  CURRENT_TASK = task;
  rt_cpu_context_swap(&m_main, &task->context);
}

void Executor::init_task(Task* task) {
  task->owner = this;
  auto stack_base = reinterpret_cast<std::uint64_t>(task->fn_ptr(task->fn_size));
  // align down by 16
  stack_base &= 0xfffffffffffffff0ull;
  // shadow space
  stack_base -= 32;

  *reinterpret_cast<std::uint64_t*>(stack_base - 8) =
      reinterpret_cast<std::uint64_t>(&task_main);
  *reinterpret_cast<std::uint64_t*>(stack_base - 16) =
      reinterpret_cast<std::uint64_t>(task);

  task->context.rsp = stack_base - 16;
  task->context.rip = reinterpret_cast<std::uint64_t>(&rt_task_trampoline);
}

Task* Executor::allocate_task() {
  auto* task = m_freelist.pop_front();
  if (!task) {
    task = new Task{};
    task->stack = reinterpret_cast<char*>(std::malloc(Task::STACK_SIZE));
  }

  return task;
}


}  // namespace rt