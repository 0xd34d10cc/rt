#include "socket.hpp"
#include "executor.hpp"

#include <mswsock.h>

#if 0
#include <iostream>
#define TRACE_BLOCK std::cout << __func__ << ": blocking task " \
                              << (void*)task << " on " \
                              << (void*)&overlapped << std::endl
#else
#define TRACE_BLOCK
#endif

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

static LPFN_DISCONNECTEX DisconnectEx = nullptr;

static LPFN_DISCONNECTEX get_disconnect_fn(SOCKET s) {
  LPFN_DISCONNECTEX fn = nullptr;
  GUID guid = WSAID_DISCONNECTEX;
  DWORD bytes = 0;
  WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &fn,
           sizeof(fn), &bytes, NULL, NULL);
  return fn;
}

static void init_sockets() {
  struct SocketInitializer {
    SocketInitializer() {
      WSADATA wsaData;
      status = ::WSAStartup(MAKEWORD(2, 2), &wsaData);
      if (!status) {
        SOCKET dummy = ::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                                   WSA_FLAG_OVERLAPPED);
        DisconnectEx = get_disconnect_fn(dummy);
        closesocket(dummy);
      }
    }

    int status{0};
  };

  // rely on magic static for exactly once initialization
  static SocketInitializer init;
  return;
}

Result<Socket> Socket::create() noexcept {
  init_sockets();

  // TODO: support ipv6
  // TODO: support udp
  Socket s{::WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0,
                       WSA_FLAG_OVERLAPPED)};
  if (!s.valid()) {
    return last_socket_error();
  }

  return s;
}

std::error_code Socket::lazy_register() noexcept {
  if (!m_bound) {
    if (auto e = current_task()->register_io(handle())) {
      return e;
    }

    m_bound = true;
  }

  return {};
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
    m_bound = false;
    m_socket = INVALID_SOCKET;
  }
}

Result<Socket> Socket::accept() noexcept {
  if (auto e = lazy_register()) {
    return e;
  }

  auto client = Socket::create();
  if (auto e = client.err()) {
    return e;
  }

  struct AddressBuf {
    sockaddr_in addr;
    char pad[16];
  };
  AddressBuf addresses[2];
  OVERLAPPED overlapped{};
  // overlapped.hEvent = h_event;
  DWORD received{0};
  if (!::AcceptEx(m_socket, client->m_socket, &addresses, 0, sizeof(AddressBuf),
                  sizeof(AddressBuf), &received, &overlapped)) {
    auto err = last_socket_error();
    if (err.value() != ERROR_IO_PENDING) {
      return err;
    }
  }

  auto* task = current_task();
  TRACE_BLOCK;
  task->block_on_io();
  assert(overlapped.Internal != STATUS_PENDING);
  if (overlapped.Internal != 0) {
    // FIXME: this is probably not a valid way to pass an error
    return socket_error(static_cast<DWORD>(overlapped.Internal));
  }

  if (::setsockopt(client->m_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   reinterpret_cast<char*>(&m_socket), sizeof(m_socket)) != 0) {
    return last_socket_error();
  }

  return client;
}

Result<std::size_t> Socket::send(const char* data, std::size_t n) noexcept {
  if (auto e = lazy_register()) {
    return e;
  }

  WSABUF buffer;
  buffer.buf = const_cast<char*>(data);
  buffer.len = static_cast<ULONG>(n);

  DWORD sent = 0;
  DWORD flags = 0;
  WSAOVERLAPPED overlapped{};

  if (::WSASend(m_socket, &buffer, 1, &sent, flags, &overlapped, nullptr) != 0) {
    auto err = last_socket_error();
    if (err.value() != ERROR_IO_PENDING) {
      return err;
    }
  }

  auto* task = current_task();
  TRACE_BLOCK;
  task->block_on_io();
  assert(overlapped.Internal != STATUS_PENDING);
  if (overlapped.Internal != 0) {
    // FIXME: this is probably not a valid way to pass an error
    return socket_error(static_cast<DWORD>(overlapped.Internal));
  }

  return std::size_t{overlapped.InternalHigh};
}

std::error_code Socket::send_all(const char* data, std::size_t n) noexcept {
  std::size_t sent = 0;
  while (sent < n) {
    const auto s = send(data + sent, n - sent);
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
  if (auto e = lazy_register()) {
    return e;
  }

  WSABUF buffer;
  buffer.buf = data;
  buffer.len = static_cast<ULONG>(n);

  DWORD received{0};
  DWORD flags{0};
  WSAOVERLAPPED overlapped{};

  if (::WSARecv(m_socket, &buffer, 1, &received, &flags, &overlapped,
                nullptr) != 0) {
    auto err = last_socket_error();
    if (err.value() != ERROR_IO_PENDING) {
      return err;
    }
  }

  auto* task = current_task();
  TRACE_BLOCK;
  task->block_on_io();
  assert(overlapped.Internal != STATUS_PENDING);
  if (overlapped.Internal != 0) {
    // FIXME: this is probably not a valid way to pass an error
    return socket_error(static_cast<DWORD>(overlapped.Internal));
  }

  return std::size_t{overlapped.InternalHigh};
}

std::error_code Socket::shutdown() noexcept {
  if (auto e = lazy_register()) {
    return e;
  }

  OVERLAPPED overlapped{};
  DWORD flags = 0;
  DWORD reserved = 0;

  if (!DisconnectEx(m_socket, &overlapped, flags, reserved)) {
    auto err = last_socket_error();
    if (err.value() != ERROR_IO_PENDING) {
      return err;
    }
  }

  auto* task = current_task();
  TRACE_BLOCK;
  task->block_on_io();
  assert(overlapped.Internal != STATUS_PENDING);
  if (overlapped.Internal != 0) {
    // FIXME: this is probably not a valid way to pass an error
    return socket_error(static_cast<DWORD>(overlapped.Internal));
  }
  return {};
}

} // namespace rt