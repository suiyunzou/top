#pragma once
// Situation overview: a compact, real-data snapshot of the whole machine.
// The ring buffer feeds the large "situation" window; drawing lives in main.cpp.
#include "model.hpp"
#include <deque>

// Reflow content rather than scaling the whole scene or its hit coordinates.
struct SituationLayout {
  static constexpr int MinWidth = 560, MinHeight = 360;
  float pad, leftW, rightW, tabY, tabW, ruleY, labelY;
  float top, bottom, gap, cardH, laneH, hubSize;
  float nameSize, noteSize, valueSize;
  int slots;
  bool stackedApp, compact;

  static SituationLayout choose(float w, float h) {
    SituationLayout l{};
    l.compact = w < 760 || h < 460;
    l.pad = bounded(w * .016f, 10.f, 20.f);
    l.leftW = bounded(w * .28f, 216.f, 240.f);
    l.rightW = bounded(w * .25f, 204.f, 224.f);
    l.tabY = w < 760 ? 50.f : 16.f;
    l.tabW = std::min(350.f, w - 2 * l.pad);
    l.ruleY = l.tabY + 38;
    l.labelY = l.ruleY;
    float start = l.labelY + 20, end = h - 76;
    l.gap = h < 460 ? 4.f : w < 1000 ? 6.f : 8.f;
    float bodyH = std::min(end - start, 5 * 68.f + 4 * l.gap);
    l.top = start + (end - start - bodyH) / 2;
    l.bottom = l.top + bodyH;
    l.labelY = l.top - 22;
    l.slots = 5;
    l.cardH = std::min(68.f,
                      (bodyH - (l.slots - 1) * l.gap) / l.slots);
    l.laneH = (bodyH - 4 * l.gap) / 5;
    l.hubSize = bounded((std::min(w - 2 * l.pad, 860.f) - l.leftW - l.rightW) * .28f, 84.f, 104.f);
    l.stackedApp = l.leftW < 220 || l.cardH < 56;
    l.nameSize = w >= 1000 ? 13.f : 12.5f;
    l.noteSize = w >= 1000 ? 12.5f : 11.5f;
    l.valueSize = w >= 1000 ? 16.f : 15.f;
    if (l.compact) {
      // Small windows have a different information budget, not smaller text.
      l.leftW = bounded(w * .34f, 200.f, 320.f);
      l.rightW = bounded(w * .28f, 184.f, 260.f);
      l.tabY = 16; l.tabW = std::min(350.f, w - 2 * l.pad - 88);
      l.ruleY = 54; l.gap = 8;
      float body = std::min(h - 88, 5 * 48.f + 4 * l.gap);
      l.top = 78 + (h - 88 - body) / 2; l.bottom = l.top + body;
      l.labelY = l.top - 24;
      l.slots = 3; l.cardH = 44; l.laneH = (body - 4 * l.gap) / 5;
      l.hubSize = 28; l.stackedApp = false;
      l.nameSize = 13; l.valueSize = 15;
    }
    return l;
  }
};

struct SitEntity {
  std::wstring key;
  double cpu = -1;
  double bytes = 0;
  std::wstring name;
  size_t processes = 0;
  bool available = true;
  double gpu = -1, disk = -1, net = -1;
};

struct SitSample {
  double when = 0;
  double cpu = -1;   // whole machine %
  double mem = -1;   // %
  double gpu = -1;   // %
  double disk = -1;   // bytes/s, read + write
  double net = -1;    // bytes/s, down + up
  double used = -1, total = -1, gpuMem = -1, gpuTotal = -1;
  std::vector<double> cores;
  std::vector<SitEntity> entities;
  size_t processCount = 0, appCount = 0;
};

struct SituationHistory {
  static constexpr size_t Capacity = 60;
  std::deque<SitSample> samples;

  void push(SitSample s) {
    samples.push_back(std::move(s));
    while (samples.size() > Capacity) samples.pop_front();
  }
  void clear() { samples.clear(); }
  const SitSample *latest() const { return samples.empty() ? nullptr : &samples.back(); }
  size_t size() const { return samples.size(); }
};

// Snapshot -> one history frame. Groups are ranked by CPU, then working set.
// Values stay -1 when the sensor did not provide a reading; the UI must show
// "—" rather than invent a number.
inline SitSample situationSample(const Snapshot &s, double now, size_t maxEntities = 6,
                                const std::wstring &pinned = L"") {
  SitSample frame;
  frame.when = now;
  frame.cpu = s.cpu;
  frame.mem = s.total > 0 && s.used >= 0 ? 100 * s.used / s.total : -1;
  frame.gpu = s.appGpuTotal;
  frame.disk = s.diskRead >= 0 && s.diskWrite >= 0 ? s.diskRead + s.diskWrite : -1;
  frame.net = s.appNetTotal;
  frame.used = s.used; frame.total = s.total;
  frame.gpuMem = s.gpuMem; frame.gpuTotal = s.gpuTotal;
  frame.cores = s.cores;
  frame.appCount = s.groups.size();
  for (const auto &g : s.groups) frame.processCount += g.members.size();
  std::vector<const Group *> groups;
  for (const auto &g : s.groups) groups.push_back(&g);
  std::stable_sort(groups.begin(), groups.end(), [](const Group *a, const Group *b) {
    double ac = a->cpu >= 0 ? a->cpu : -1, bc = b->cpu >= 0 ? b->cpu : -1;
    if (ac != bc) return ac > bc;
    return a->bytes > b->bytes;
  });
  auto add = [&](const Group &g) {
    frame.entities.push_back({g.key, g.cpu, g.bytes, g.name, g.members.size(), true,g.gpu,g.disk,g.net});
  };
  if (!pinned.empty() && maxEntities > 0)
    for (const Group *g : groups) if (g->key == pinned) { add(*g); break; }
  for (const Group *g : groups) {
    if (frame.entities.size() >= maxEntities) break;
    if (g->key != pinned) add(*g);
  }
  return frame;
}

inline bool situationLaneFocused(int mode, int lane) {
  return mode == 0 || (mode == 4 ? lane >= 3 : mode == lane + 1);
}

// Missing is not zero: an IO total is available only when both inputs exist.
inline double sitIoValue(double disk, double net) { return disk >= 0 && net >= 0 ? disk + net : -1; }
inline double sitMetricValue(const SitSample &s, int mode) {
  return mode == 2 ? s.gpu : mode == 3 ? s.mem : mode == 4 ? sitIoValue(s.disk, s.net) : s.cpu;
}
inline double sitAppValue(const SitEntity &e, int mode) {
  if (!e.available) return -1;
  return mode == 2 ? e.gpu : mode == 3 ? e.bytes : mode == 4 ? sitIoValue(e.disk, e.net) : e.cpu;
}
inline double sitLaneValue(const SitSample &s, int lane) {
  return lane == 0 ? s.cpu : lane == 1 ? s.gpu : lane == 2 ? s.mem : lane == 3 ? s.disk : s.net;
}

struct SitChange { std::wstring name; double delta = 0, seconds = 0; };
inline SitChange sitRecentChange(const SituationHistory &history, const SitSample &current) {
  const SitSample *previous = nullptr;
  for (const auto &s : history.samples) {
    if (s.when >= current.when) break;
    if (s.when >= current.when - 20) { previous = &s; break; }
  }
  SitChange result;
  if (!previous) return result;
  result.seconds = current.when - previous->when;
  for (const auto &e : current.entities) if (e.available && e.cpu >= 0)
    for (const auto &old : previous->entities)
      if (old.key == e.key && old.available && old.cpu >= 0 &&
          (result.name.empty() || std::abs(e.cpu-old.cpu) > std::abs(result.delta))) {
        result.name = e.name; result.delta = e.cpu-old.cpu;
      }
  return result;
}

struct SituationLink {
  size_t entity;
  int lane;
  double value;
};

// Only measured per-app values can establish an app-to-resource relationship.
inline std::vector<SituationLink> situationLinks(const SitSample &frame, int mode) {
  std::vector<SituationLink> links;
  for (size_t i = 0; i < frame.entities.size() && i < 6; ++i) {
    const auto &e = frame.entities[i];
    if (!e.available) continue;
    if ((mode == 0 || mode == 1) && e.cpu >= 0) links.push_back({i, 0, e.cpu});
    if ((mode == 0 || mode == 3) && e.bytes >= 0) links.push_back({i, 2, e.bytes});
    if ((mode == 0 || mode == 2) && e.gpu >= 0) links.push_back({i, 1, e.gpu});
    if ((mode == 0 || mode == 4) && e.disk >= 0) links.push_back({i, 3, e.disk});
    if ((mode == 0 || mode == 4) && e.net >= 0) links.push_back({i, 4, e.net});
  }
  return links;
}

// Stable across sorting, samples and localized display names.
inline unsigned long long situationIdentity(const std::wstring &key) {
  unsigned long long hash = 1469598103934665603ull;
  for (auto ch : lower(key)) { hash ^= ch; hash *= 1099511628211ull; }
  return hash;
}
inline std::wstring situationInitials(const SitEntity &e) {
  auto slash = e.key.find_last_of(L"\\/");
  std::wstring name = slash == std::wstring::npos ? e.name : e.key.substr(slash + 1);
  if (name.size() > 4 && lower(name.substr(name.size()-4)) == L".exe") name.resize(name.size()-4);
  std::wstring letters, starts;
  bool word = true;
  for (wchar_t ch : name) {
    bool letter = (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
                  (ch >= L'0' && ch <= L'9') || (ch >= 0x3400 && ch <= 0x9fff);
    if (letter) { letters += wchar_t(towupper(ch)); if (word) starts += wchar_t(towupper(ch)); }
    word = !letter;
  }
  auto result = starts.size() >= 2 ? starts : letters;
  return result.empty() ? L"?" : result.substr(0, 2);
}

// A steady focus state, not a repeating pulse. Sampling never restarts it.
struct SituationFocus {
  float value = 0;
  float step(float target, double elapsed, bool animated) {
    if (!animated) value = target;
    else value += (target-value)*float(1-std::exp(-std::max(0.,elapsed)/.22));
    if (std::abs(value-target)<.001f) value=target;
    return value;
  }
};

struct SituationToken {
  int lane = -1;
  double progress = 0, alpha = 0, envelope = 0;
  double speed = 38;
  double followSpeed(int resource, double reading, double elapsed) {
    // Visual activity scale, never an estimate of bytes or resource capacity.
    double intensity = resource == 0 || resource == 1
      ? std::sqrt(bounded(reading / 100., 0., 1.))
      : std::log1p(std::max(0., reading) / 65536.) / std::log1p(1024.);
    double target = reading > 0 && resource != 2 ? 38 + 112 * bounded(intensity, 0., 1.) : 0;
    speed += (target-speed)*(1-std::exp(-std::max(0.,elapsed)/.65));
    return std::max(1.,speed);
  }
  bool started = false;
  double opacity() const { return std::min(alpha, envelope); }
  void step(const std::vector<int> &active, double elapsed, double offset, double duration = 7) {
    elapsed = std::max(0., elapsed);
    duration = std::max(1., duration);
    auto current = std::find(active.begin(), active.end(), lane);
    if (lane >= 0 && current == active.end()) {
      // Stop immediately on zero/unknown activity; retire in place before rerouting.
      alpha = std::max(0., alpha-elapsed/.35);
      if (alpha == 0) lane = -1;
      return;
    }
    if (lane < 0) {
      if (active.empty()) return;
      lane = active.front(); progress = started ? 0 : offset; started = true;
      current = active.begin();
    }
    alpha = std::min(1., alpha+elapsed/.35);
    progress += elapsed / duration;
    if (progress >= 1) {
      lane = active[(size_t(current-active.begin())+1)%active.size()];
      progress = 0; alpha = 0;
    }
    double edge = bounded(std::min(progress,1-progress)*duration/.5,0.,1.);
    envelope = edge*edge*(3-2*edge);
  }
};
