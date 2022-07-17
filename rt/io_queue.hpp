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

// on windows:
// 1. Register IO object on creation
// 2. try doing op(), if err != PENDING => return result, err
//    otherwise call scheduler
// on linux:
// 1. send op() to io queue and call scheduler
class IoQueue {
 public:
  IoQueue() = default;
  IoQueue(const IoQueue&) = delete;
  IoQueue(IoQueue&&) = default;
  IoQueue& operator=(const IoQueue&) = delete;
  IoQueue& operator=(IoQueue&&) = default;
  ~IoQueue() = default;

  static Result<IoQueue> create();

  // Add a handle to IO queue
  std::error_code add(Handle h, void* context);

  // returns 0 on timeout
  std::size_t wait(CompletionEvent* events, std::size_t n, std::size_t timeout_ms);
private:
  IoQueue(Handle h);

  HandleOwner m_iocp;
};


} // namespace rt