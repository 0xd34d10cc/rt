#pragma once

#include <cstddef>
#include <cstdint>

#include "handle.hpp"
#include "result.hpp"
#include "socket.hpp"


namespace rt {

struct CompletionEvent {
  std::int64_t result{-1};
  void* context{nullptr};
};

// TODO: rename to IoEngine
class IoEngine {
 public:
  IoEngine() noexcept = default;
  IoEngine(const IoEngine&) = delete;
  IoEngine(IoEngine&&) noexcept = default;
  IoEngine& operator=(const IoEngine&) = delete;
  IoEngine& operator=(IoEngine&&) noexcept = default;
  ~IoEngine() noexcept = default;

  static Result<IoEngine> create() noexcept;

  Result<IoEngine> share() noexcept;


  Result<Socket> accept(Task* task, Socket* s) noexcept;
  Result<std::size_t> send(Task* task, Socket* s, const char* data, std::size_t n) noexcept;
  Result<std::size_t> recv(Task* task, Socket* s, char* data, std::size_t n) noexcept;
  std::error_code shutdown(Task* task, Socket* s) noexcept;

  // send()
  // recv()

  // returns 0 on timeout
  std::size_t wait(CompletionEvent* events, std::size_t n, std::size_t timeout_ms) noexcept;
private:
  IoEngine(Handle h) noexcept;

  std::error_code lazy_register(Task* task, Socket* s) noexcept;
  std::error_code add(Handle h, void* context) noexcept;
  std::error_code remove(Handle h) noexcept;

  HandleOwner m_iocp;
};


} // namespace rt