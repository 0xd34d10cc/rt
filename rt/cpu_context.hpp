#pragma once

#include <cstdint>


namespace rt {

using std::uint64_t;
struct uint128_t {
  uint64_t low;
  uint64_t high;
};

// NOTE: contains only non-volatile registers and rip
struct CpuContext {
#if defined(_MSC_VER)
  // https://docs.microsoft.com/en-us/cpp/build/x64-software-conventions?view=msvc-170#x64-register-usage

  uint128_t xmm6;
  uint128_t xmm7;
  uint128_t xmm8;
  uint128_t xmm9;
  uint128_t xmm10;
  uint128_t xmm11;
  uint128_t xmm12;
  uint128_t xmm13;
  uint128_t xmm14;
  uint128_t xmm15;

  uint32_t mxcsr;
  uint32_t fpucw;

  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rbx;
  uint64_t rbp;
  uint64_t rsp;

  uint64_t rip;
#else
#error Unsupported platform
#endif
};

}  // namespace rt

extern "C" {
void rt_cpu_context_switch(const rt::CpuContext* next);
void rt_cpu_context_swap(rt::CpuContext* current, const rt::CpuContext* next);
}