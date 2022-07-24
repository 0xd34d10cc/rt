#pragma once

#include "result.hpp"
#include "handle.hpp"

#include <winsock2.h>

#include <array>
#include <cstdint>


namespace rt {

class IoEngine;
struct Task;

using IpAddr = std::array<std::uint8_t, 4>;
using Port = std::uint16_t;

class Socket {
 public:
  friend class IoEngine;

  Socket() = default;
  Socket(SOCKET s) noexcept : m_socket(s) {}
  Socket(const Socket&) = delete;
  Socket(Socket&& other) noexcept
      : m_bound{other.m_bound}, m_socket{other.m_socket} {
    other.m_bound = nullptr;
    other.m_socket = INVALID_SOCKET;
  }
  Socket& operator=(const Socket&) = delete;
  Socket& operator=(Socket&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    close();
    std::swap(m_bound, other.m_bound);
    std::swap(m_socket, other.m_socket);
    return *this;
  }
  ~Socket() noexcept { close(); }

  static Result<Socket> bind(IpAddr ip, Port port) noexcept;

  bool valid() const noexcept { return m_socket != INVALID_SOCKET; }
  void close() noexcept;
  Handle handle() const noexcept { return reinterpret_cast<Handle>(m_socket); }

  Result<Socket> accept() noexcept;
  Result<std::size_t> send(const char* data, std::size_t n) noexcept;
  std::error_code send_all(const char* data, std::size_t n) noexcept;
  Result<std::size_t> recv(char* data, std::size_t n) noexcept;
  std::error_code shutdown() noexcept;

 private:
  static Result<Socket> create() noexcept;

  Task* m_bound{nullptr};
  SOCKET m_socket{INVALID_SOCKET};
};


} // namespace rt