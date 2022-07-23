#pragma once

#include <cstddef>
#include <cstdint>

#include "handle.hpp"
#include "result.hpp"


namespace rt {

struct CompletionEvent {
  std::int64_t result{-1};
  void* context{nullptr};
};

// TODO: rename to IoEngine
class IoQueue {
 public:
  IoQueue() = default;
  IoQueue(const IoQueue&) = delete;
  IoQueue(IoQueue&&) = default;
  IoQueue& operator=(const IoQueue&) = delete;
  IoQueue& operator=(IoQueue&&) = default;
  ~IoQueue() = default;

  static Result<IoQueue> create();

  // TODO: implement
  // IoEngineRef share()

  // Register a handle in IO queue
  // TODO: make it private (since it's windows-only)
  std::error_code add(Handle h, void* context);

  // TODO: move implementation of methods from Socket to here
  // accept()
  // send()
  // recv()

  // returns 0 on timeout
  std::size_t wait(CompletionEvent* events, std::size_t n, std::size_t timeout_ms);
private:
  IoQueue(Handle h);

  HandleOwner m_iocp;
};


} // namespace rt