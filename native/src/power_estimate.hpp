#pragma once
#include "model.hpp"
#include <array>

// Estimates are presentation values. Never write them into raw sensor fields.
namespace power {
inline bool valid(double v) { return std::isfinite(v) && v >= 0; }
struct Reading {
  double watts = -1;
  bool estimated = false;
  std::string model;
};
inline bool profileMatches(const Config &cfg, const Snapshot &s) {
  return !s.powerHardware.empty() && cfg.get<std::string>("powerProfileHardware", "") == s.powerHardware;
}
inline double manual(const Config &cfg, const Snapshot &s, const char *key, const char *source) {
  double v = cfg.get<double>(key, -1);
  return profileMatches(cfg, s) && valid(v) && v <= 1000 &&
         cfg.get<std::string>(source, "").find_first_not_of(" \t\r\n") != std::string::npos ? v : -1;
}
inline double samsungReference(const std::vector<MemoryModule> &modules) {
  if (modules.empty()) return -1;
  double watts = 0;
  for (const auto &m : modules) {
    if (m.part.rfind("M471A1K43DB1-", 0) != 0 || m.type != 26 || m.gib != 8 ||
        m.speed < 2133 || m.speed > 3200) return -1;
    // Samsung DDR4 8Gb D-die SODIMM Rev1.1, table 48 (2666 MT/s).
    // Reference workload only: 76% active standby, 10% read, 10% write,
    // 4% refresh, using the table's maximum test-pattern currents.
    // Linear frequency scaling is an explicit approximation, not calibration.
    const double standby = .216 * 1.2 + .032 * 2.5;
    const double read = .720 * 1.2 + .032 * 2.5;
    const double write = .664 * 1.2 + .024 * 2.5;
    const double refresh = 1.480 * 1.2 + .144 * 2.5;
    watts += (.76 * standby + .10 * read + .10 * write + .04 * refresh) * m.speed / 2666.;
  }
  return watts;
}
struct Baseline {
  double watts = -1, board = 0, display = 0, storage = 0, fans = 0, conversion = 0;
  bool assumedBrightness = false, assumedDisks = false, assumedFans = false, assumedActivity = false;
};
inline Baseline automaticBaseline(const Snapshot &s, double memoryWatts) {
  Baseline b;
  if ((s.platform.kind != 1 && s.platform.kind != 2) || !valid(s.cpuPower) || !valid(memoryWatts)) return b;
  const bool laptop = s.platform.kind == 2;
  // Engineering reference scenarios, NOT measurements or model-specific ratings.
  // Board includes chipset, controllers, radios and memory termination.
  b.board = laptop ? 5 : 12;
  b.assumedBrightness = laptop && !valid(s.platform.brightness);
  if (laptop) b.display = 2 + 4 * (b.assumedBrightness ? .7 : bounded(s.platform.brightness / 100., 0., 1.));
  b.assumedActivity = !valid(s.diskBusy);
  double busy = b.assumedActivity ? .1 : bounded(s.diskBusy / 100., 0., 1.);
  b.assumedDisks = !s.platform.disksKnown;
  int ssd = b.assumedDisks ? 1 : s.platform.ssd;
  b.storage = ssd * (.5 + 4.5 * busy) + s.platform.hdd * (laptop ? 2.5 + 3.5 * busy : 4 + 5 * busy) + s.platform.otherDisks * 2.5;
  // RPM is not electric power. Use it only to distinguish stopped/running fans.
  b.assumedFans = !s.fans.fresh(s.when, 1) || s.fans.rpm.empty();
  if (!b.assumedFans) for (double rpm : s.fans.rpm) if (!valid(rpm)) b.assumedFans = true;
  if (b.assumedFans) b.fans = laptop ? 2 : 3;
  else for (double rpm : s.fans.rpm) if (rpm > 0) b.fans += 1;
  // Assume 90% CPU/DRAM regulator efficiency. NVML GPU board power already
  // includes its board losses, so do not apply this correction to the GPU.
  b.conversion = (s.cpuPower + memoryWatts) * (1 / .9 - 1);
  b.watts = b.board + b.display + b.storage + b.fans + b.conversion;
  return b;
}
inline std::array<Reading, 4> resolve(const Config &cfg, const Snapshot &s) {
  std::array<Reading, 4> r{{{s.cpuPower, false, "sensor"}, {s.gpuPower, false, "sensor"},
                           {s.memoryPower, false, "sensor"}, {s.systemPower, false, "sensor"}}};
  for (auto &v : r) if (!valid(v.watts)) v = {};
  if (valid(r[2].watts) && s.memoryPowerState == "ready") r[2].model = "intel-dram-rapl";
  if (!cfg.get<bool>("estimatePower", false)) return r;
  if (!valid(r[2].watts)) {
    double v = manual(cfg, s, "memoryReferenceWatts", "memoryReferenceSource");
    if (valid(v)) r[2] = {v, true, "manual"};
    else if (valid(v = samsungReference(s.memoryModules))) r[2] = {v, true, "samsung-ddr4-reference-v1"};
  }
  if (!valid(r[3].watts)) {
    double base = manual(cfg, s, "platformBaseWatts", "platformBaseSource");
    bool automatic = !valid(base);
    if (automatic) base = automaticBaseline(s, r[2].watts).watts;
    // This first model supports a single NVML discrete GPU. Other topologies
    // remain unknown instead of dropping a GPU or counting integrated graphics twice.
    if (valid(base) && s.discreteGpuCount == 1 && valid(r[0].watts) && valid(r[1].watts) && valid(r[2].watts))
      r[3] = {r[0].watts + r[1].watts + r[2].watts + base, true, automatic ? "hardware-reference-v1" : "component-sum-v1"};
  }
  return r;
}
inline std::wstring label(const Reading &r, double value) {
  if (!valid(value)) return L"—";
  auto prefix = r.estimated ? L"≈" : L"";
  if (value >= 1000) return prefix + num(value / 1000, 1) + L" kW";
  return prefix + num(value, r.estimated ? (value < 10 ? 1 : 0) : (value < 100 ? 1 : 0)) + L" W";
}
struct Motion {
  double phase = 0, last = -1;
  void step(double now, double activity, bool enabled) {
    double dt = last < 0 ? 0 : bounded(now - last, 0., .1);
    last = now;
    if (enabled && valid(activity) && activity > 0)
      phase = fmod(phase + dt * (.45 + .65 * activity / (activity + 30)), 1.);
  }
};
}
