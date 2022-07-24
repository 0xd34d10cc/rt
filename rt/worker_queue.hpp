#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <vector>


namespace rt {

struct Task;

// Local to worker task queue
//
// see https://www.di.ens.fr/~zappa/readings/ppopp13.pdf
// see https://github.com/taskflow/taskflow/blob/master/taskflow/core/tsq.hpp
class WorkerQueue {
 public:
  WorkerQueue(std::int64_t cap = 1024) : m_array{new Array{cap}} {
    m_garbage.reserve(32);
  }

  WorkerQueue(WorkerQueue&& other)
      : m_top{other.m_top.load()},
        m_bottom{other.m_bottom.load()},
        m_array{other.m_array.load()},
        m_garbage{std::move(other.m_garbage)} {
    other.m_top = 0;
    other.m_bottom = 0;
    other.m_array = nullptr;
  }

  ~WorkerQueue() {
    for (auto* a : m_garbage) {
      delete a;
    }
    delete m_array.load();
  }

  // NOTE: only this method is thread-safe
  Task* steal() noexcept {
    auto t = m_top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto b = m_bottom.load(std::memory_order_acquire);

    Task* task{nullptr};
    if (t < b) {
      Array* a = m_array.load(std::memory_order_consume);
      task = a->pop(t);
      if (!m_top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                         std::memory_order_relaxed)) {
        return nullptr;
      }
    }

    return task;
  }

  // NOTE: these methods are not thread-safe
  bool empty() const noexcept {
    auto b = m_bottom.load(std::memory_order_relaxed);
    auto t = m_top.load(std::memory_order_relaxed);
    return b <= t;
  }

  std::size_t size() const noexcept {
    auto b = m_bottom.load(std::memory_order_relaxed);
    auto t = m_top.load(std::memory_order_relaxed);
    return static_cast<std::size_t>(b >= t ? b - t : 0);
  }

  void push(Task* task) noexcept {
    auto b = m_bottom.load(std::memory_order_relaxed);
    auto t = m_top.load(std::memory_order_acquire);
    Array* a = m_array.load(std::memory_order_relaxed);

    if (a->cap() - 1 < (b - t)) {
      Array* tmp = a->resize(b, t);
      m_garbage.push_back(a);
      std::swap(a, tmp);
      m_array.store(a, std::memory_order_release);
    }

    a->push(b, task);
    std::atomic_thread_fence(std::memory_order_release);
    m_bottom.store(b + 1, std::memory_order_relaxed);
  }

  Task* pop() noexcept {
    auto b = m_bottom.load(std::memory_order_relaxed) - 1;
    Array* a = m_array.load(std::memory_order_relaxed);
    m_bottom.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    auto t = m_top.load(std::memory_order_relaxed);

    Task* task{nullptr};
    if (t <= b) {
      task = a->pop(b);
      if (t == b) {
        // the last item just got stolen
        if (!m_top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst,
                                           std::memory_order_relaxed)) {
          task = nullptr;
        }

        m_bottom.store(b + 1, std::memory_order_relaxed);
      }
    } else {
      m_bottom.store(b + 1, std::memory_order_relaxed);
    }

    return task;
  }

 private:
  struct Array {
    std::int64_t capacity;
    std::int64_t mask;
    std::atomic<Task*>* tasks;

    Array(std::int64_t cap)
        : capacity{cap}, mask{cap - 1}, tasks{new std::atomic<Task*>[cap]} {}

    ~Array() { delete[] tasks; }

    std::int64_t cap() const noexcept { return capacity; }

    void push(std::int64_t i, Task* task) noexcept {
      tasks[i & mask].store(task, std::memory_order_relaxed);
    }

    Task* pop(std::int64_t i) noexcept {
      return tasks[i & mask].load(std::memory_order_relaxed);
    }

    Array* resize(std::int64_t bottom, std::int64_t top) {
      Array* ptr = new Array{capacity * 2};
      for (std::int64_t i = top; i != bottom; ++i) {
        ptr->push(i, pop(i));
      }
      return ptr;
    }
  };

  alignas(128) std::atomic<std::int64_t> m_top{0};
  alignas(128) std::atomic<std::int64_t> m_bottom{0};

  std::atomic<Array*> m_array;
  std::vector<Array*> m_garbage;
};

} // namespace rt