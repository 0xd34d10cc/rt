#include "io_queue.hpp"

#include <iostream>

namespace rt {

IoQueue::IoQueue(Handle h) : m_iocp{h} {}

Result<IoQueue> IoQueue::create() {
  Handle h = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 1);
  if (!h) {
    return last_os_error();
  }

  return IoQueue{h};
}

std::error_code IoQueue::add(Handle h, void* context) {
  const auto key = reinterpret_cast<ULONG_PTR>(context);
  const auto status = ::CreateIoCompletionPort(h, m_iocp.get(), key, 0);
  if (!status) {
    return last_os_error();
  }

  return {};
}

std::size_t IoQueue::wait(CompletionEvent* events, std::size_t n,
                          std::size_t timeout_ms) {
  constexpr std::size_t max_entries = 64;
  OVERLAPPED_ENTRY entries[max_entries];
  ULONG got_entries{0};
  const bool status =
      ::GetQueuedCompletionStatusEx(
          m_iocp.get(), entries, static_cast<ULONG>((std::min)(max_entries, n)), &got_entries,
          static_cast<DWORD>(timeout_ms), FALSE) != 0;
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


} // namespace rt