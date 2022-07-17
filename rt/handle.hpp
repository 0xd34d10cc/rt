#pragma once

#include <memory>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace rt {

using Handle = HANDLE;

#if defined(_WIN32)
struct HandleDeleter {
  void operator()(HANDLE h) const {
    if (h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
    }
  }
};
#endif

using HandleOwner = std::unique_ptr<void, HandleDeleter>;

}  // namespace rt