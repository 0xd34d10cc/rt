#include "runtime.hpp"

namespace rt {

Result<Runtime> Runtime::create(std::size_t n_threads) {
  if (n_threads == 0) {
    n_threads = std::thread::hardware_concurrency();
  }

  auto io = IoEngine::create();
  if (auto e = io.err()) {
    return e;
  }

  Runtime runtime;
  runtime.m_workers.reserve(n_threads);
  runtime.m_workers.emplace_back(std::move(*io));

  for (std::size_t i = 1; i < n_threads; ++i) {
    auto io = runtime.m_workers.front().worker.io()->share();
    if (auto e = io.err()) {
      return e;
    }
    runtime.m_workers.emplace_back(std::move(*io));
  }

  return runtime;
}

std::vector<Worker*> Runtime::workers_for(std::size_t id) {
  std::vector<Worker*> workers;
  for (std::size_t i = 0; i < m_workers.size(); ++i) {
    if (i == id) {
      continue;
    }

    workers.emplace_back(&m_workers[i].worker);
  }
  return workers;
}

void Runtime::run() noexcept {
  for (std::size_t i = 1; i < m_workers.size(); ++i) {
    auto* state = &m_workers[i];
    assert(!state->thread.joinable());
    state->thread = std::thread([state, w{workers_for(i)}]() mutable {
      state->worker.run(w.data(), w.size());
    });
  }

  auto w = workers_for(0);
  m_workers.front().worker.run(w.data(), w.size());
}

} // namespace rt