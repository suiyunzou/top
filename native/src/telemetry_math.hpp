#pragma once
#include <cmath>
#include <cstdint>

namespace telemetry {
// Free-running counters count 64-byte transactions. A conservative 128 GB/s
// ceiling bounds the window to prevent hidden multiple wraps on dual DDR4.
struct Traffic {
  uint32_t counter = 0;
  double when = 0;
  bool valid = false;
  void reset() { valid = false; }
  double sample(uint32_t next, double now) {
    const double dt = now - when, ceiling = 128e9;
    double rate = -1;
    if (valid && std::isfinite(now) && dt >= .05 && dt < (4294967296. * 64 / ceiling)) {
      rate = double(uint32_t(next - counter)) * 64 / dt;
      if (rate > ceiling) rate = -1;
    }
    counter = next; when = now; valid = std::isfinite(now);
    return rate;
  }
};
// CPU time and capacity are both cumulative 100 ns counters. Capacity includes idle.
inline double cpuPercent(int64_t current, int64_t previous, int64_t capacity) {
  if (capacity <= 0 || current < previous) return -1;
  const double value = 100. * double(current - previous) / double(capacity);
  return value <= 100. ? value : -1; // Reject mismatched windows; do not hide them by clamping.
}
inline double temperature(uint64_t status, double target) {
  if (!(status & (uint64_t(1) << 31)) || target < 50 || target > 125) return -1;
  const double value = target - ((status >> 16) & 127);
  return value >= 0 && value <= 125 ? value : -1;
}
struct Energy {
  uint32_t counter = 0;
  double when = 0;
  bool valid = false;
  void reset() { valid = false; }
  double sample(uint32_t next, double now, double unit) {
    double watts = -1;
    const double dt = now - when;
    // At <= 1000 W, this window permits at most one 32-bit wrap.
    const double wrapSeconds = std::ldexp(unit, 32) / 1000.;
    if (valid && std::isfinite(now) && unit > 0 && dt >= .05 && dt <= 10 && dt < wrapSeconds) {
      watts = double(uint32_t(next - counter)) * unit / dt;
      if (!std::isfinite(watts) || watts > 1000) watts = -1;
    }
    counter = next; when = now; valid = std::isfinite(now) && unit > 0;
    return watts;
  }
};
}
