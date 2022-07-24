#pragma once

#include <cstddef>
#include <cassert>
#include <memory>
#include <type_traits>
#include <utility>
#include <system_error>

#include "cpu_context.hpp"
#include "handle.hpp"


namespace rt {
class Worker;

namespace detail {

struct BaseTaskFn {
  virtual void call() = 0;
  virtual ~BaseTaskFn(){};
};

template <typename F>
struct TaskFnImpl : BaseTaskFn, F {
  template <typename Param>
  TaskFnImpl(Param&& f) : F(std::forward<Param>(f)) {}
  virtual void call() override { F::operator()(); }
};

}  // namespace detail

struct Task {
  static constexpr std::uint64_t STACK_SIZE = 1024 * 32;

  CpuContext context{};
  std::size_t fn_size{0};
  char* stack{nullptr};

  Worker* owner{nullptr};
  Task* next{nullptr};

  ~Task() { reset(); }

  detail::BaseTaskFn* fn_ptr(std::size_t off) const {
    auto* p = stack + STACK_SIZE - off;
    return reinterpret_cast<detail::BaseTaskFn*>(p);
  }

  void call() {
    assert(fn_size > 0);
    auto* p = fn_ptr(fn_size);
    p->call();
  }

  void reset() {
    if (fn_size > 0) {
      auto* p = fn_ptr(fn_size);
      std::destroy_at(p);
      fn_size = 0;
    }
  }

  template <typename F>
  void set(F&& fn) {
    using FnValue = detail::TaskFnImpl<std::decay_t<F>>;
    constexpr std::size_t off = sizeof(FnValue);
    static_assert(off <= STACK_SIZE, "Too big to fit");
    assert(stack);
    reset();
    // FIXME: align p according to alignof(FnValue)
    auto* p = fn_ptr(off);
    std::construct_at(reinterpret_cast<FnValue*>(p), std::forward<F>(fn));
    fn_size = off;
  }

  void finalize();
  void yield();
  void block_on_io();
};
}  // namespace rt