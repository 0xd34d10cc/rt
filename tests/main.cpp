#include <iostream>
#include <system_error>

#include "rt/runtime.hpp"
#include "rt/socket.hpp"


// returns number of milliseconds since unix epoch, ~3.7ns per call
static std::uint64_t win_now() {
  FILETIME ft{};
  GetSystemTimeAsFileTime(&ft);
  const auto now = static_cast<std::uint64_t>(ft.dwLowDateTime) |
                   static_cast<std::uint64_t>(ft.dwHighDateTime) << 32ull;
  constexpr std::uint64_t unix_offset = 0x019DB1DED53E8000ull;
  // NOTE: division here can throw away up to 1ms of time
  return (now - unix_offset) / 10000;
}

static std::size_t receive_request(rt::Socket& c, char* request, std::size_t& received, std::size_t request_size) {
  bool closed = false;
  while (true) {
    for (std::size_t i = 3; i < received; ++i) {
      if (request[i - 3] == '\r' && request[i - 2] == '\n' &&
          request[i - 1] == '\r' && request[i - 0] == '\n') {
        return i+1;
      }
    }

    if (closed || received >= request_size) {
      return 0;
    }

    auto n = c.recv(request + received, request_size - received);
    if (auto e = n.err()) {
      std::cout << "recv() failed: " << e.message() << std::endl;
      return 0;
    }

    if (*n == 0) {
      closed = true;
    }

    received += *n;
  }
}

struct HelloWorldServer {
  void operator()() const {
    auto server = rt::Socket::bind(ip, port);
    if (auto e = server.err()) {
      std::cout << "bind() failed: " << e.message() << std::endl;
      return;
    }

    while (true) {
      auto client = server->accept();
      if (auto e = client.err()) {
        std::cout << "accept() failed: " << e.message() << std::endl;
        return;
      }

      rt::spawn([c = std::move(*client)]() mutable {
        char request[1024];
        std::size_t received{0};
        while (true) {
          std::size_t request_end = receive_request(c, request, received, sizeof(request));
          if (request_end == 0) {
            break;
          }

          std::size_t data_left = received - request_end;
          std::memmove(request, request + request_end, data_left);
          received = data_left;

          const char response[] =
              "HTTP/1.1 200 OK\r\n"
              "Content-Length: 13\r\n"
              "Connection: keep-alive\r\n"
              "\r\n"
              "Hello, world!";

          if (auto e = c.send_all(response, sizeof(response) - 1)) {
            std::cout << "send() failed: " << e.message() << std::endl;
            break;
          }
        }

        if (auto e = c.shutdown()) {
          std::cout << "shutdown() failed: " << e.message() << std::endl;
        }
      });
    }
  }

  rt::IpAddr ip;
  rt::Port port;
};

int main() {
  auto runtime = rt::Runtime::create();
  if (auto e = runtime.err()) {
    std::cout << "Failed to initialize runtime: " << e.message() << std::endl;
    return EXIT_FAILURE;
  }

  runtime->spawn(HelloWorldServer{{0, 0, 0, 0}, 8080});
  runtime->run();
  return EXIT_SUCCESS;
}