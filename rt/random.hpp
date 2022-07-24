#pragma once

#include <cstdint>


namespace rt {

struct XorShiftRng {
  std::uint32_t x{0xbad5eed};
  std::uint32_t y{0xbad5eed};
  std::uint32_t z{0xbad5eed};
  std::uint32_t w{0xbad5eed};

  std::uint32_t gen() {
    std::uint32_t t = x ^ (x << 11);
    x = y;
    y = z;
    z = w;
    std::uint32_t w_ = w;
    w = w_ ^ (w_ >> 19) ^ (t ^ (t >> 8));
    return w;

  }
};

} // namespace rt