#include "worker.hpp"


extern "C" {
void rt_task_trampoline();
}

namespace rt {

thread_local Task* CURRENT_TASK{nullptr};

Task* current_task() { return CURRENT_TASK; }

void Task::finalize() {
  TRACE_TASK(this, "finalize");
  auto* main = &owner->m_main;
  owner->release_task(this);
  // NOTE: this code relies on fact that release_task() doesn't
  //       release stack memory
  rt_cpu_context_switch(main);
  // CpuContext ignore;
  // owner->run(&ignore);
}

void Task::yield() {
  owner->m_ready.push(this);
  rt_cpu_context_swap(&context, &owner->m_main);
  // owner->run(&context);
}

void Task::block_on_io() {
  TRACE_TASK(this, "blocked");
  // NOTE: task ptr should be already saved in m_io
  ++owner->m_io_blocked;
  rt_cpu_context_swap(&context, &owner->m_main);
  // owner->run(&context);
}

void yield() {
  auto* task = CURRENT_TASK;
  task->yield();
}

void task_main(Task* task) {
  TRACE_TASK(task, "main");
  task->call();
  task->finalize();
}

Worker::Worker(IoEngine io) noexcept : m_io(std::move(io)) {}

Worker::~Worker() noexcept {
  const auto free_task = [](Task* task) {
    if (task->stack) {
      std::free(task->stack);
    }

    delete task;
  };

  while (auto* task = m_ready.pop()) {
    free_task(task);
  }

  while (auto* task = m_freelist.pop_front()) {
    free_task(task);
  }
}

Task* Worker::try_steal() noexcept {
  if (m_n_workers == 0) {
    return nullptr;
  }

  std::size_t mid = m_rng.gen() % m_n_workers;
  for (std::size_t i = mid; i < m_n_workers; ++i) {
    if (auto* task = m_workers[i]->steal()) {
      task->owner = this;
      return task;
    }
  }

  for (std::size_t i = 0; i < mid; ++i) {
    if (auto* task = m_workers[i]->steal()) {
      task->owner = this;
      return task;
    }
  }

  return nullptr;
}

Task* Worker::next_task() noexcept {
  auto* task = m_ready.pop();
  if (!task) {
    task = try_steal();
  }
  return task;
}

bool Worker::wait_io() noexcept {
  // NOTE: this doesn't work on windows, since underlying
  //       IOCP queue is shared among all workers
  // if (m_io_blocked == 0) {
  //  return false;
  // }

  constexpr std::size_t n_events = 64;
  // FIXME: it is set to 20 as temporary hack to wake up threads periodically,
  //        use -1 when we'll have a normal thread notification algorithm
  constexpr std::size_t wait_ms = static_cast<std::size_t>(20);

  rt::CompletionEvent events[n_events];
  std::size_t n = m_io.wait(events, n_events, wait_ms);
  // assert(n != 0);
  for (std::size_t i = 0; i < n; ++i) {
    --m_io_blocked;
    auto* task = reinterpret_cast<Task*>(events[i].context);
    task->owner = this;
    m_ready.push(task);
  }

  return true;
}

void Worker::run(Worker** workers, std::size_t n) noexcept {
  m_workers = workers;
  m_n_workers = n;
  while (true) {
    run(&m_main);
  }
}

void Worker::run(CpuContext* current) noexcept {
  Task* task = next_task();
  while (!task) {
    wait_io();
    task = next_task();
  }

  run_task(task, current);
}

void Worker::release_task(Task* task) noexcept {
  task->reset();
  m_freelist.push_front(task);
}

void Worker::run_task(Task* task, CpuContext* current) noexcept {
  CURRENT_TASK = task;
  TRACE_TASK(task, "switching in");
  rt_cpu_context_swap(current, &task->context);
}

void Worker::init_task(Task* task) noexcept {
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

Task* Worker::allocate_task() noexcept {
  auto* task = m_freelist.pop_front();
  if (!task) {
    task = new Task{};
    task->stack = reinterpret_cast<char*>(std::malloc(Task::STACK_SIZE));
  }

  return task;
}


}  // namespace rt