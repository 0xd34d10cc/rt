#pragma once

#include <cstddef>
#include <thread>
#include <vector>

#include "result.hpp"
#include "random.hpp"
#include "worker.hpp"


namespace rt {

class Runtime {
 public:
  // 0 threads means use number of cores
  static Result<Runtime> create(std::size_t n_threads = 0);

  template <typename F>
  void spawn(F&& fn) {
    const auto idx = m_rng.gen() % m_workers.size();
    auto& state = m_workers[idx];
    state.worker.spawn(std::forward<F>(fn));
    // FIXME: wake up worker if it's sleeping
  }

  void run() noexcept;

 private:
  Runtime() = default;
  std::vector<Worker*> workers_for(std::size_t id);

  struct WorkerState {
    std::thread thread{};
    std::atomic<bool> awake{false};
    Worker worker;

    WorkerState(IoEngine io) : worker(std::move(io)) {}
    WorkerState(WorkerState&& other) noexcept
        : thread(std::move(other.thread)),
          awake(other.awake.load()),
          worker(std::move(other.worker)) {}
  };

  XorShiftRng m_rng;
  std::vector<WorkerState> m_workers;
};


} // namespace rt