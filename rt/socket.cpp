#include "socket.hpp"
#include "worker.hpp"
#include "task.hpp"


namespace rt {

// to_bytes
static std::uint32_t to_be(std::array<std::uint8_t, 4> bytes) {
  return std::uint32_t{bytes[0]} << 0 | std::uint32_t{bytes[1]} << 8 |
         std::uint32_t{bytes[2]} << 16 | std::uint32_t{bytes[3]} << 24;
}

// bswap
static std::uint16_t to_be(std::uint16_t val) {
  return (val & 0xff00) >> 8 | (val & 0xff) << 8;
}

static std::error_code socket_error(DWORD value) {
  return {static_cast<int>(value), std::system_category()};
}

static std::error_code last_socket_error() {
  return socket_error(::WSAGetLastError());
}


Result<Socket> Socket::create() noexcept {
  // TODO: support ipv6
  // TODO: support udp
  Socket s{::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                       WSA_FLAG_OVERLAPPED)};
  if (!s.valid()) {
    return last_socket_error();
  }

  return s;
}

Result<Socket> Socket::bind(IpAddr ip, Port port) noexcept {
  auto s = Socket::create();
  if (!s) {
    return s;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = to_be(ip);
  addr.sin_port = to_be(port);

  int status = ::bind(s->m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (status) {
    return last_socket_error();
  }

  status = ::listen(s->m_socket, 5);
  if (status) {
    return last_socket_error();
  }

  return s;
}

void Socket::close() noexcept {
  if (valid()) {
    closesocket(m_socket);
    m_bound = nullptr;
    m_socket = INVALID_SOCKET;
  }
}

Result<Socket> Socket::accept() noexcept {
  auto* task = current_task();
  return task->owner->io()->accept(task, this);
}

Result<std::size_t> Socket::send(const char* data, std::size_t n) noexcept {
  auto* task = current_task();
  return task->owner->io()->send(task, this, data, n);
}

std::error_code Socket::send_all(const char* data, std::size_t n) noexcept {
  auto* task = current_task();
  auto* io = task->owner->io();

  std::size_t sent = 0;
  while (sent < n) {
    const auto s = io->send(task, this, data + sent, n - sent);
    if (auto e = s.err()) {
      return e;
    }

    if (*s == 0) {
      return std::error_code(WSAECONNRESET, std::system_category());
    }

    sent += *s;
  }

  return {};
}

Result<std::size_t> Socket::recv(char* data, std::size_t n) noexcept {
  auto* task = current_task();
  return task->owner->io()->recv(task, this, data, n);
}

std::error_code Socket::shutdown() noexcept {
  auto* task = current_task();
  return task->owner->io()->shutdown(task, this);
}

} // namespace rt