#include "worker.hpp"


extern "C" {
void rt_task_trampoline();
}

namespace rt {

thread_local Task* CURRENT_TASK{nullptr};

Task* current_task() { return CURRENT_TASK; }

void Task::finalize() {
  owner->release_task(this);
  // NOTE: this code relies on fact that release_task() doesn't
  //       release stack memory
  CpuContext ignore;
  owner->run(&ignore);
}

void Task::yield() {
  owner->m_ready.push_back(this);
  owner->run(&this->context);
}

std::error_code Task::register_io(Handle h) {
  return owner->m_queue.add(h, this);
}

void Task::block_on_io() {
  // NOTE: task ptr should be already saved in m_queue
  ++owner->m_io_blocked;
  owner->run(&this->context);
}

void yield() {
  auto* task = CURRENT_TASK;
  task->yield();
}

void task_main(Task* task) {
  task->call();
  task->finalize();
}

Worker::Worker(IoQueue queue) : m_queue(std::move(queue)) {}

Worker::~Worker() {
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

Task* Worker::next_task() {
  auto* task = m_ready.pop_front();
  // TODO: steal from other threads if task == nullptr
  return task;
}

bool Worker::wait_io() {
  if (m_io_blocked == 0) {
    return false;
  }

  constexpr std::size_t n_events = 64;
  constexpr std::size_t wait_ms = static_cast<std::size_t>(-1);  // infinite

  rt::CompletionEvent events[n_events];
  std::size_t n = m_queue.wait(events, n_events, wait_ms);
  assert(n != 0);
  for (std::size_t i = 0; i < n; ++i) {
    --m_io_blocked;
    auto* task = reinterpret_cast<Task*>(events[i].context);
    m_ready.push_back(task);
  }

  return true;
}

void Worker::run() { run(&m_main); }

void Worker::run(CpuContext* current) {
  auto* task = next_task();
  if (!task) {
    if (!wait_io()) {
      // FIXME: make sure we are in main context
      return;
    }

    task = next_task();
  }

  run_task(task, current);
}

void Worker::release_task(Task* task) {
  task->reset();
  m_freelist.push_front(task);
}

void Worker::run_task(Task* task, CpuContext* current) {
  CURRENT_TASK = task;
  rt_cpu_context_swap(current, &task->context);
}

void Worker::init_task(Task* task) {
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

Task* Worker::allocate_task() {
  auto* task = m_freelist.pop_front();
  if (!task) {
    task = new Task{};
    task->stack = reinterpret_cast<char*>(std::malloc(Task::STACK_SIZE));
  }

  return task;
}


}  // namespace rt