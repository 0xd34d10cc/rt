#include "io_engine.hpp"
#include "task.hpp"

#include <mswsock.h>

#include <iostream>

#if 0
#define TRACE_BLOCK                                                    \
  std::cout << __func__ << ": blocking task " << (void*)task << " on " \
            << (void*)&overlapped << std::endl
#else
#define TRACE_BLOCK
#endif

extern "C" {
struct IO_STATUS_BLOCK {
  void* status{nullptr};
  void* information{nullptr};
};

struct FILE_INFORMATION {
  HANDLE h{nullptr};
  void* key{nullptr};
};

std::uint32_t NtSetInformationFile(HANDLE file_handle, IO_STATUS_BLOCK* block,
                                   FILE_INFORMATION* file_info,
                                   std::uint32_t len, unsigned cls);
}


namespace rt {

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
      assert(!status);
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

IoEngine::IoEngine(Handle h) noexcept : m_iocp{h} { }

Result<IoEngine> IoEngine::create() noexcept {
  Handle h = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 1);
  if (!h) {
    return last_os_error();
  }

  init_sockets();

  auto io = IoEngine{h};
  return io;
}

Result<IoEngine> IoEngine::share() noexcept {
  return create();
}

std::error_code IoEngine::add(Handle h, void* context) noexcept {
  const auto key = reinterpret_cast<ULONG_PTR>(context);
  const auto status = ::CreateIoCompletionPort(h, m_iocp.get(), key, 0);
  if (!status) {
    return last_os_error();
  }

  return {};
}

std::error_code IoEngine::remove(Handle h) noexcept {
  IO_STATUS_BLOCK status{};
  FILE_INFORMATION fi {};
  constexpr unsigned FileReplaceCompletionInformation = 61;
  NtSetInformationFile(h, &status, &fi, sizeof(fi),
                       FileReplaceCompletionInformation);
  return {}; // FIXME: check error
}

std::error_code IoEngine::lazy_register(Task* task, Socket* s) noexcept {
  if (s->m_engine == this && s->m_task == task) {
    // should be most common case
    return {};
  }

  if (s->m_engine) {
    s->m_engine->remove(s->handle());
  }

  if (auto e = add(s->handle(), task)) {
    s->m_task = nullptr;
    s->m_engine = nullptr;
    return e;
  }

  s->m_task = task;
  s->m_engine = this;
  return {};
}

Result<Socket> IoEngine::accept(Task* task, Socket* s) noexcept {
  if (auto e = lazy_register(task, s)) {
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
  if (!::AcceptEx(s->m_socket, client->m_socket, &addresses, 0, sizeof(AddressBuf),
                  sizeof(AddressBuf), &received, &overlapped)) {
    auto err = last_socket_error();
    if (err.value() != ERROR_IO_PENDING) {
      return err;
    }
  }

  TRACE_BLOCK;
  task->block_on_io();
  assert(overlapped.Internal != STATUS_PENDING);
  if (overlapped.Internal != 0) {
    // FIXME: this is probably not a valid way to pass an error
    return socket_error(static_cast<DWORD>(overlapped.Internal));
  }

  if (::setsockopt(client->m_socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   reinterpret_cast<char*>(&s->m_socket), sizeof(s->m_socket)) != 0) {
    return last_socket_error();
  }

  return client;
}

Result<std::size_t> IoEngine::send(Task* task, Socket* s, const char* data,
                                   std::size_t n) noexcept {
  if (auto e = lazy_register(task, s)) {
    return e;
  }

  WSABUF buffer;
  buffer.buf = const_cast<char*>(data);
  buffer.len = static_cast<ULONG>(n);

  DWORD sent = 0;
  DWORD flags = 0;
  WSAOVERLAPPED overlapped{};

  if (::WSASend(s->m_socket, &buffer, 1, &sent, flags, &overlapped, nullptr) !=
      0) {
    auto err = last_socket_error();
    if (err.value() != ERROR_IO_PENDING) {
      return err;
    }
  }

  TRACE_BLOCK;
  task->block_on_io();
  assert(overlapped.Internal != STATUS_PENDING);
  if (overlapped.Internal != 0) {
    // FIXME: this is probably not a valid way to pass an error
    return socket_error(static_cast<DWORD>(overlapped.Internal));
  }

  return std::size_t{overlapped.InternalHigh};
}

Result<std::size_t> IoEngine::recv(Task* task, Socket* s, char* data,
                                   std::size_t n) noexcept {
  if (auto e = lazy_register(task, s)) {
    return e;
  }

  WSABUF buffer;
  buffer.buf = data;
  buffer.len = static_cast<ULONG>(n);

  DWORD received{0};
  DWORD flags{0};
  WSAOVERLAPPED overlapped{};

  if (::WSARecv(s->m_socket, &buffer, 1, &received, &flags, &overlapped,
                nullptr) != 0) {
    auto err = last_socket_error();
    if (err.value() != ERROR_IO_PENDING) {
      return err;
    }
  }

  TRACE_BLOCK;
  task->block_on_io();
  assert(overlapped.Internal != STATUS_PENDING);
  if (overlapped.Internal != 0) {
    // FIXME: this is probably not a valid way to pass an error
    return socket_error(static_cast<DWORD>(overlapped.Internal));
  }

  return std::size_t{overlapped.InternalHigh};
}

std::error_code IoEngine::shutdown(Task* task, Socket* s) noexcept {
  if (auto e = lazy_register(task, s)) {
    return e;
  }

  OVERLAPPED overlapped{};
  DWORD flags = 0;
  DWORD reserved = 0;

  if (!DisconnectEx(s->m_socket, &overlapped, flags, reserved)) {
    auto err = last_socket_error();
    if (err.value() != ERROR_IO_PENDING) {
      return err;
    }
  }

  TRACE_BLOCK;
  task->block_on_io();
  assert(overlapped.Internal != STATUS_PENDING);
  if (overlapped.Internal != 0) {
    // FIXME: this is probably not a valid way to pass an error
    return socket_error(static_cast<DWORD>(overlapped.Internal));
  }
  return {};
}

std::size_t IoEngine::wait(CompletionEvent* events, std::size_t n,
                           std::size_t timeout_ms) noexcept {
  constexpr std::size_t max_entries = 64;
  OVERLAPPED_ENTRY entries[max_entries];
  ULONG got_entries{0};
  const bool status =
      ::GetQueuedCompletionStatusEx(
          m_iocp.get(), entries, static_cast<ULONG>((std::min)(max_entries, n)),
          &got_entries, static_cast<DWORD>(timeout_ms), FALSE) != 0;
  if (!status) {
    // timed out
    return 0;
  }

  // std::cout << got_entries << " ops completed: " << std::endl;
  for (std::size_t i = 0; i < got_entries; ++i) {
    // std::cout << "task: " << (void*)entries[i].lpCompletionKey
    //          << " waking up for " << (void*)entries[i].lpOverlapped
    //          << std::endl;
    events[i].context = reinterpret_cast<void*>(entries[i].lpCompletionKey);
    events[i].result = entries[i].dwNumberOfBytesTransferred;
  }
  return got_entries;
}

}  // namespace rt