#pragma once
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include "../vendor/json.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <winsock2.h>
#include <windows.h>
using json = nlohmann::json;
inline std::wstring wide(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring r(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n);
  return r;
}
inline std::string utf8(const std::wstring &s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
  std::string r(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), r.data(), n, nullptr, nullptr);
  return r;
}
inline std::wstring lower(std::wstring s) {
  std::transform(s.begin(), s.end(), s.begin(), ::towlower);
  return s;
}
inline double seconds() {
  LARGE_INTEGER a, b;
  QueryPerformanceCounter(&a);
  QueryPerformanceFrequency(&b);
  return double(a.QuadPart) / b.QuadPart;
}
inline std::wstring num(double v, int digits = 0) {
  if (!std::isfinite(v) || v < 0)
    return L"—";
  std::wostringstream s;
  s << std::fixed << std::setprecision(digits) << v;
  return s.str();
}
inline std::wstring mem(double b) {
  if (!std::isfinite(b) || b < 0) return L"—";
  return b >= 1073741824 ? num(b / 1073741824, 1) + L"G" : num(b / 1048576) + L"M";
}
inline std::wstring rate(double b) {
  if (b < 0)
    return L"—";
  return b >= 999500  ? num(b / 1e6, 1) + L" MB/s"
         : b >= 999.5 ? num(b / 1000) + L" KB/s"
                      : num(b) + L" B/s";
}
template <class T> inline T bounded(T x, T lo, T hi) { return std::max(lo, std::min(hi, x)); }
inline std::wstring appdata() {
  wchar_t b[32768];
  DWORD n = GetEnvironmentVariableW(L"APPDATA", b, 32768);
  return n && n < 32768 ? std::wstring(b) : L".";
}
inline std::string readFile(const std::wstring &path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Cannot read file");
  LARGE_INTEGER size{};
  GetFileSizeEx(h, &size);
  if (size.QuadPart < 0 || size.QuadPart > 16 * 1024 * 1024) {
    CloseHandle(h);
    throw std::runtime_error("File too large");
  }
  std::string data(size_t(size.QuadPart), 0);
  DWORD read = 0;
  bool ok = ReadFile(h, data.data(), DWORD(data.size()), &read, nullptr) != FALSE;
  CloseHandle(h);
  if (!ok || read != data.size())
    throw std::runtime_error("Incomplete read");
  return data;
}
inline bool writeFile(const std::wstring &path, const std::string &data) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  DWORD written = 0;
  bool ok =
      WriteFile(h, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size();
  FlushFileBuffers(h);
  CloseHandle(h);
  return ok;
}
inline json defaults() {
  return {{"modules", {"cpu", "memory", "network", "processes"}},
          {"hidden", json::array()},
          {"palette", "sea"},
          {"language", "auto"},
          {"interval", 1.0},
          {"motion", true},
          {"estimatePower", false},
          {"memoryReferenceWatts", -1.0},
          {"memoryReferenceSource", ""},
          {"platformBaseWatts", -1.0},
          {"platformBaseSource", ""},
          {"powerProfileHardware", ""},
          {"maxProcesses", 24},
          {"processSort", "cpu"},
          {"floating", false},
          {"menuBar", true},
          {"menuBarShows", "cpu"},
          {"appearance", "dark"},
          {"opacity", 0.93},
          {"width", 527},
          {"height", 265}};
}
struct Config {
  json data = defaults();
  std::wstring path;
  FILETIME stamp{};
  bool valid = true;
  template <class T> T get(const char *k, T fallback) const {
    try {
      return data.at(k).get<T>();
    } catch (...) {
      return fallback;
    }
  }
  bool has(const std::string &m) const {
    auto k = (m == "gpu" || m == "power") ? "hidden" : "modules";
    bool exists = false;
    try {
      for (auto &v : data.at(k))
        if (v == m)
          exists = true;
    } catch (...) {
    }
    return std::string(k) == "hidden" ? !exists : exists;
  }
  std::string trayMetric() const {
    auto m = get<std::string>("menuBarShows", "cpu");
    for (auto id : {"cpu", "memory", "gpu", "download", "upload", "power"})
      if (m == id) return m;
    return "cpu";
  }
  void normalize() {
    data["menuBarShows"] = trayMetric();
    for (auto key : {"motion", "floating", "menuBar", "estimatePower"})
      if (!data.contains(key) || !data[key].is_boolean())
        data[key] = defaults()[key];
    for (auto key : {"modules", "hidden"})
      if (!data.contains(key) || !data[key].is_array())
        data[key] = defaults()[key];
    auto clamp = [&](const char *k, double lo, double hi) {
      double v = get<double>(k, defaults()[k].get<double>());
      data[k] = std::isfinite(v) ? bounded(v, lo, hi) : defaults()[k].get<double>();
    };
    clamp("interval", .5, 10);
    clamp("maxProcesses", 3, 24);
    clamp("width", 40, 4000);
    clamp("height", 16, 2400);
    clamp("opacity", .6, 1);
    for (auto key : {"memoryReferenceWatts", "platformBaseWatts"}) {
      double v = get<double>(key, -1);
      data[key] = std::isfinite(v) && v >= 0 && v <= 1000 ? v : -1.;
    }
  }
  bool load(bool force = false) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a))
      return false;
    if (!force && CompareFileTime(&a.ftLastWriteTime, &stamp) == 0)
      return false;
    try {
      auto j = json::parse(readFile(path), nullptr, true, true);
      if (!j.is_object())
        throw std::runtime_error("object expected");
      data = defaults();
      data.update(j);
      normalize();
      stamp = a.ftLastWriteTime;
      valid = true;
      return true;
    } catch (...) {
      valid = false;
      return false;
    }
  }
  bool save() {
    load();
    if (!valid)
      return false;
    normalize();
    auto tmp = path + L".tmp-" + std::to_wstring(GetCurrentProcessId());
    if (!writeFile(tmp, data.dump(2)))
      return false;
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
      return false;
    WIN32_FILE_ATTRIBUTE_DATA a{};
    GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a);
    stamp = a.ftLastWriteTime;
    return true;
  }
  bool zh() const {
    auto l = get<std::string>("language", "auto");
    return l == "zh" || (l == "auto" && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE);
  }
  std::wstring tr(const wchar_t *a, const wchar_t *b) const { return zh() ? a : b; }
};
struct Candidate {
  std::string tier = "micro", strip;
  int cols = 1, count = 0;
  bool wide = false;
};
struct Block {
  std::string kind;
  float x = 0, y = 0, w = 0, h = 0;
};
struct Layout {
  Candidate c;
  float w = 0, h = 0, scale = 1;
  std::vector<Block> blocks;
};
struct CpuChart {
  bool grid = false;
  int rows = 1, cols = 1;
  std::vector<Block> slots;
};
inline CpuChart cpuChart(int n, Block area) {
  CpuChart c;
  if (n <= 0 || area.w <= 0 || area.h <= 0) return c;
  float gap = 4, rowGap = 10;
  c.cols = n;
  if (n * 10 + (n - 1) * gap > area.w) {
    c.rows = 2; c.cols = (n + 1) / 2;
    c.grid = c.cols * 10 + (c.cols - 1) * gap > area.w || area.h < 105;
  }
  float bw, bh;
  if (c.grid) {
    c.cols = bounded(int(std::ceil(std::sqrt(n * area.w / area.h))), 1, n);
    c.rows = (n + c.cols - 1) / c.cols;
    // Shrink the gaps too for extreme aspect ratios, never discard a thread.
    gap = std::min(4.f, std::min(area.w / (2 * c.cols), area.h / (2 * c.rows)));
    rowGap = gap;
    bw = bh = std::min(27.f, std::min((area.w - (c.cols - 1) * gap) / c.cols,
                                    (area.h - (c.rows - 1) * gap) / c.rows));
  } else {
    bw = std::min(24.f, (area.w - (c.cols - 1) * gap) / c.cols);
    bh = (area.h - (c.rows - 1) * rowGap) / c.rows;
  }
  float x = area.x + (area.w - c.cols * bw - (c.cols - 1) * gap) / 2;
  float y = area.y + (area.h - c.rows * bh - (c.rows - 1) * rowGap) / 2;
  for (int i = 0; i < n; ++i)
    c.slots.push_back({"core", x + (i % c.cols) * (bw + gap), y + (i / c.cols) * (bh + rowGap), bw, bh});
  return c;
}
struct LayoutEngine {
  int n;
  const Config &cfg;
  Layout choose(float w, float h) const {
    Layout l;
    l.w = w; l.h = h;
    auto add = [&](std::string k, float x, float y, float bw, float bh) {
      if (bw > 0 && bh > 0) l.blocks.push_back({k, x, y, bw, bh});
    };
    if (w < 260 || h < 46) {
      l.c.tier = "micro";
      l.w = 84; l.h = 30;
      l.scale = std::min(1.f, std::min(w / l.w, h / l.h));
      add("micro", 4, 5, 76, 20);
      return l;
    }
    if (h < 165) {
      l.c.tier = "strip";
      std::vector<std::string> kinds{"cpu"};
      if (cfg.has("memory")) kinds.push_back("memory");
      if (cfg.has("gpu") && w >= 360) kinds.push_back("gpu");
      if (cfg.has("power")) kinds.push_back("systemPower");
      float bw = (w - 24) / kinds.size();
      for (size_t i = 0; i < kinds.size(); ++i)
        add("fig:" + kinds[i], 12 + i * bw, (h - 20) / 2, bw - 5, 20);
      return l;
    }
    bool tall = w < 480 && h >= 345, small = (w < 480 && !tall) || h < 220;
    l.c.tier = small ? "small" : tall ? "tall" : "full";
    float pad = small ? 12.f : 15.f, top = small ? 10.f : 13.f;
    float inner = w - 2 * pad, contentH = h - 2 * top;
    float computeW = tall || small ? inner : (inner - 21) * .48f;
    float computeH = small ? contentH - 65 : tall ? contentH - 183 : contentH;
    float ramW = cfg.has("memory") ? (small ? 48.f : 57.f) : 0;
    add("cpu", pad, top, computeW - (ramW ? ramW + 12 : 0), computeH);
    if (ramW) add("ram", pad + computeW - ramW, top, ramW, computeH);
    float sx = tall || small ? pad : pad + computeW + 21;
    float sy = tall || small ? top + computeH + (small ? 7 : 10) : top;
    float sw = tall || small ? inner : inner - computeW - 21;
    if (!small) {
      if (cfg.has("gpu")) add("gpuSummary", sx, sy, (sw - 18) / 2, 43);
      add("diskSummary", cfg.has("gpu") ? sx + (sw + 18) / 2 : sx, sy,
          cfg.has("gpu") ? (sw - 18) / 2 : sw, 43);
      sy += tall ? 50 : 53;
    }
    if (cfg.has("network")) { add("network", sx, sy, sw, 20); sy += small ? 28 : 29; }
    if (!small && !tall) { add("diskRates", sx, sy, sw, 20); sy += 29; }
    if (cfg.has("power")) { add("powers", sx, sy, sw, 23); sy += 30; }
    if (!small && cfg.has("processes")) {
      l.c.count = std::max(0, std::min(cfg.get<int>("maxProcesses", 24), int((h - top - sy + 8) / 28)));
      if (l.c.count) add("processes", sx, sy, sw, l.c.count * 28 - 8);
    }
    return l;
  }
};
struct Member {
  DWORD pid = 0, parent = 0;
  long long created = 0, cpuTime = 0;
  double cpu = -1, bytes = 0;
  std::wstring name, exe, display;
};
struct Group {
  std::wstring key, name, exe;
  double cpu = -1, bytes = 0;
  std::vector<Member> members;
  HICON icon = nullptr;
  double gpu = -1, disk = -1, net = -1;
};
struct FanReading {
  std::vector<double> rpm;
  double reference = -1, when = 0, ms = 0;
  std::string state = "not_sampled", source;
  DWORD error = 0;
  bool fresh(double now, double interval) const {
    return state == "ready" && when > 0 && now - when <= std::max(6., interval * 2 + 1);
  }
  double fastest() const {
    if (rpm.empty() || state != "ready") return -1;
    double value = 0;
    for (double r : rpm) { if (r < 0) return -1; value = std::max(value, r); }
    return value;
  }
  json report() const {
    return {{"rpm", rpm}, {"referenceRPM", reference}, {"state", state}, {"source", source},
            {"error", error}, {"when", when}, {"readMs", ms}};
  }
};
struct FanMotion {
  double angle = 0, speed = 0, last = 0, colorRatio = -1;
  void step(double now, double rpm, double reference, bool enabled) {
    double dt = last > 0 ? bounded(now - last, 0., .1) : 0;
    last = now;
    if (rpm < 0 || reference <= 0) colorRatio = -1;
    else {
      double targetRatio = bounded(rpm / reference, 0., 1.);
      if (colorRatio < 0 || !enabled) colorRatio = targetRatio;
      else colorRatio += (targetRatio - colorRatio) * (1 - exp(-dt / .65));
    }
    if (!enabled || rpm <= 0) { speed = 0; return; }
    double ratio = reference > 0 ? bounded(rpm / reference, 0., 1.) : rpm / (rpm + 1800);
    // Perceptual speed, not literal rotor speed; keep the blades legible.
    double target = .15 + 2.35 * ratio * ratio;
    speed += (target - speed) * (1 - exp(-dt / (target > speed ? .25 : .8)));
    angle = fmod(angle + speed * dt * 360, 360.);
  }
};
struct MemoryModule {
  std::string part;
  double gib = 0;
  unsigned speed = 0, type = 0;
};
struct PlatformHardware {
  int kind = 0; // 0 unknown, 1 desktop/workstation, 2 mobile
  int ssd = 0, hdd = 0, otherDisks = 0;
  bool disksKnown = false;
  double brightness = -1; // Actual WMI percentage, -1 on failure.
};
struct Snapshot {
  double appGpuTotal = -1, appNetTotal = -1;
  json appIoState;
  PlatformHardware platform;
  std::vector<MemoryModule> memoryModules;
  std::string powerHardware;
  unsigned discreteGpuCount = 0;
  FanReading fans;
  unsigned seq = 0;
  double when = 0, cpu = -1, used = -1, total = -1, swap = -1, down = -1, up = -1, gpu = -1,
         gpuMem = -1, gpuTotal = -1, gpuReserved = -1, gpuTemp = -1, gpuPower = -1, gpuClock = -1,
         gpuMaxClock = -1,
         cpuTemp = -1, cpuPower = -1, memoryPower = -1, systemPower = -1,
         diskBusy = -1, diskRead = -1, diskWrite = -1, sensorMs = 0;
  std::string sensorState = "not_sampled";
  std::string memoryPowerState = "not_sampled";
  DWORD memoryPowerError = 0;
  double memoryRead = -1, memoryWrite = -1;
  std::string memoryBandwidthState = "not_sampled";
  DWORD memoryBandwidthError = 0;
  DWORD sensorError = 0;
  int pressure = -1;
  std::vector<double> cores, ghz;
  std::vector<Group> groups;
  double cost = 0;
};

struct TrayReading {
  std::wstring text, label, value;
};
inline TrayReading trayReading(const Config &cfg, const Snapshot &s) {
  const auto metric = cfg.trayMetric();
  if (metric == "download" || metric == "upload") {
    double v = metric == "download" ? s.down : s.up;
    std::wstring shortText = L"—";
    if (std::isfinite(v) && v >= 0) {
      const wchar_t *suffix[] = {L"", L"K", L"M", L"G", L"T"};
      int unit = 0;
      while (v >= 999.5 && unit < 4) { v /= 1000.; ++unit; }
      shortText = num(v, unit && v < 9.95 ? 1 : 0) + suffix[unit];
    }
    return {shortText, metric == "download" ? cfg.tr(L"下载", L"Download") : cfg.tr(L"上传", L"Upload"),
            rate(metric == "download" ? s.down : s.up)};
  }
  if (metric == "memory") {
    double v = s.total > 0 && s.used >= 0 ? 100 * s.used / s.total : -1;
    return {num(v), cfg.tr(L"内存", L"Memory"), num(v, 1) + L"% · " + mem(s.used) + L" / " + mem(s.total)};
  }
  if (metric == "power")
    return {num(s.cpuPower), cfg.tr(L"CPU 功耗", L"CPU power"), num(s.cpuPower, 1) + L" W"};
  double v = metric == "gpu" ? s.gpu : s.cpu;
  return {num(v), metric == "gpu" ? L"GPU" : L"CPU", num(v, 1) + L"%"};
}
