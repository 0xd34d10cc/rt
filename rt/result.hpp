#pragma once

#include <cassert>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace rt {

template <typename T>
class Result {
 public:
  template <typename Param>
  Result(Param&& val) : m_value(std::forward<Param>(val)) {}
  Result(std::error_code ec) : m_err(std::move(ec)) { assert(ec); }

  Result(const Result& other) : m_err(other.m_err) {
    if (!other.m_err) {
      std::construct_at(&m_value, other.m_value);
    }
  }

  Result(Result&& other) : m_err(other.m_err) {
    if (!other.m_err) {
      std::construct_at(&m_value, std::move(other.m_value));
    }
  }

  Result& operator=(const Result& other) {
    if (this == &other) {
      return *this;
    }

    if (!other.m_err) {
      if (!m_err) {
        m_value = other.m_value;
      } else {
        std::construct_at(&m_value, other.m_value);
      }
    }

    m_err = other.m_err;
    return *this;
  }

  Result& operator=(Result&& other) {
    if (this == &other) {
      return *this;
    }

    if (!other.m_err) {
      if (!m_err) {
        m_value = std::move(other.m_value);
      } else {
        std::construct_at(&m_value, std::move(other.m_value));
      }
    }

    m_err = other.m_err;
    return *this;
  }

  ~Result() {
    if (!m_err) {
      std::destroy_at(&m_value);
    }
  }

  T& operator*() noexcept {
    assert(!m_err);
    return m_value;
  }

  const T& operator*() const noexcept {
    assert(!m_err);
    return m_value;
  }

  T* operator->() noexcept {
    assert(!m_err);
    return &m_value;
  }

  const T* operator->() const noexcept {
    assert(!m_err);
    return &m_value;
  }

  std::error_code err() const noexcept { return m_err; }
  operator bool() const noexcept { return !m_err; }

 private:
  std::error_code m_err;
  union {
    // initialized only if !m_err
    T m_value;
  };
};

inline std::error_code last_os_error() {
#if defined(_WIN32)
  return {static_cast<int>(GetLastError()), std::system_category()};
#else
#error "Unsupported"
#endif
}

} // namespace rt