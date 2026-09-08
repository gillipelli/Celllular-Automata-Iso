#pragma once
#include <cassert>
#include <cmath>
#include <compare>
#include <cstdint>
#include <limits>
namespace ashfall {
// Saturating Q16.16; widened arithmetic avoids signed-overflow undefined behavior.
struct fx16 {
  int32_t raw = 0;
  static constexpr int SHIFT = 16;
  static constexpr int32_t ONE = 65536;
  static constexpr fx16 from_raw(int32_t v) { return {v}; }
  static constexpr fx16 narrow(int64_t v) {
    return {static_cast<int32_t>(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v)};
  }
  static constexpr fx16 from_int(int v) { return narrow(static_cast<int64_t>(v) * ONE); }
  static fx16 from_double(double v) {
    assert(std::isfinite(v));
    if (!std::isfinite(v))
      return {};
    if (v >= 32767.9999847412109375)
      return {INT32_MAX};
    if (v <= -32768)
      return {INT32_MIN};
    return narrow(static_cast<int64_t>(std::round(v * ONE)));
  }
  double to_double() const { return static_cast<double>(raw) / ONE; }
  constexpr auto operator<=>(const fx16 &) const = default;
  constexpr fx16 operator+(fx16 b) const { return narrow(static_cast<int64_t>(raw) + b.raw); }
  constexpr fx16 operator-(fx16 b) const { return narrow(static_cast<int64_t>(raw) - b.raw); }
  constexpr fx16 operator-() const { return narrow(-static_cast<int64_t>(raw)); }
  constexpr fx16 operator*(fx16 b) const {
    const int64_t p = static_cast<int64_t>(raw) * b.raw;
    // Explicit floor, matching arithmetic right shift on every platform.
    return narrow(p >= 0 ? p / ONE : -((-p + ONE - 1) / ONE));
  }
  constexpr fx16 operator/(fx16 b) const {
    assert(b.raw != 0);
    return b.raw == 0 ? fx16{} : narrow(static_cast<int64_t>(raw) * ONE / b.raw);
  }
};
inline constexpr fx16 FX_ONE = fx16::from_int(1);
} // namespace ashfall
