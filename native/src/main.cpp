#include "drawing.hpp"
#include "sampler.hpp"
#include "startup.hpp"
#include "sensor_setup.hpp"
#include "power_setup.hpp"
#include "situation.hpp"
#include <cassert>
#include <numeric>
#include <set>
#include <windowsx.h>
constexpr UINT TRAY_MESSAGE = WM_APP + 10;
struct Hit {
  std::string id;
  std::wstring key;
  D2D1_RECT_F r;
};
struct Window {
  HWND hwnd = nullptr;
  int kind = 0;
  float dpi = 1;
  Canvas canvas;
  std::vector<Hit> hits;
  std::string hover, pressed;
  std::wstring hoverItem, pressedItem;
  bool tipReady = false;
  int focus = -1;
};
struct Follow {
  double target = -1;
  struct Move {
    double delta, start, interval;
  };
  std::vector<Move> moves;
  void set(double value, double now, double interval, bool motion, double delay = 0) {
    if (target >= 0 && value >= 0 && motion && std::abs(target - value) > .001)
      moves.push_back({target - value, now + delay, bounded(interval, .25, 1.5)});
    else if (!motion || value < 0)
      moves.clear();
    target = value;
  }
  double value(double now) {
    double v = target;
    for (auto it = moves.begin(); it != moves.end();) {
      double x = std::max(0., now - it->start) * 4.5 / it->interval;
      if (x >= 12)
        it = moves.erase(it);
      else {
        v += it->delta * (1 + x + x * x / 2) * exp(-x);
        ++it;
      }
    }
    return v;
  }
};
struct App {
  HINSTANCE instance;
  Config cfg;
  Sampler sampler;
  Snapshot snap;
  Theme theme;
  Layout layout;
  Window panel, settings, detail, situation;
  SituationHistory sitHistory;
  std::vector<std::wstring> sitKeys;
  double sitRankAt = 0;
  int sitMode = 0;               // 0 overview, 1 CPU, 2 GPU, 3 memory, 4 I/O
  bool sitFrozen = false;
  size_t sitCursor = (size_t)-1; // replay position; -1 follows live samples
  std::wstring sitPinned, sitHover;
  int sitHoverLane = -1;
  std::map<std::wstring, SituationToken> sitTokens;
  std::map<std::wstring, SituationFocus> sitAppFocus;
  std::array<SituationFocus, 5> sitLaneFocus;
  SituationFocus sitHubFocus;
  double sitFlowAt = 0;
  ID2D1Bitmap *sitBase = nullptr;
  bool sitBaseDirty = true, sitFocusMoving = false;
  struct SitBadgeFrame { SitEntity entity; D2D1_POINT_2F point; D2D1_COLOR_F edge; float opacity; };
  std::vector<SitBadgeFrame> sitBadges;
  float sitPorts[4]{};
  void clearSituationBase() { release(sitBase); sitBaseDirty = true; sitBadges.clear(); }
  SitEntity sitPinnedEntity;
  double sitOpenedAt = 0;
  bool sitTimerOn = false;
  bool overviewMode = false;
  std::wstring className;
  bool resizing = false, more = false, trayAdded = false, quitting = false, dirty = true;
  int scroll = 0;
  std::wstring hoverKey, detailKey;
  std::string detailMetric;
  double sampleAt = 0, outsideSince = 0, hoverAt = 0;
  float pointerX = 0, pointerY = 0;
  std::vector<Follow> bars;
  Follow total, gpuBar, diskBar;
  std::vector<Follow> powerNumbers = std::vector<Follow>(4);
  std::array<power::Reading, 4> powers{};
  std::array<power::Motion, 4> powerMotion{};
  std::array<power::Motion, 5> activityMotion{};
  FanMotion fanMotion;
  NOTIFYICONDATAW tray{};
  UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  std::map<std::wstring, double> rankLevels, rowFrom, rowTo;
  std::vector<std::wstring> order;
  double rankTime = 0, transition = 0;
  std::wstring sortMode;
  Canvas staticCanvas;
  ID2D1Bitmap *staticBitmap = nullptr;
  ID2D1DCRenderTarget *staticOwner = nullptr;
  std::vector<Hit> staticHits;
  bool buildingBase = false;
  bool rowsCached = false;
  HICON appIcon = nullptr;
  std::wstring lastTray, lastTrayTip;
  int lastTraySize = 0;
  bool lastTrayLight = false;
  double lastEnv = 0;
  bool occluded = false, sitOccluded = false;
  UINT tickInterval = 60;
  double startupCheckedAt = -10;
  bool startupOn = false;
  double framesMs = 0;
  unsigned frames = 0;
  App(HINSTANCE h) : instance(h) {
    panel.kind = 0;
    settings.kind = 1;
    detail.kind = 2;
    situation.kind = 3;
    SYSTEM_INFO s;
    GetSystemInfo(&s);
    bars.resize(s.dwNumberOfProcessors);
    snap.cores.assign(s.dwNumberOfProcessors, -1);
    snap.ghz.assign(s.dwNumberOfProcessors, -1);
    appIcon = LoadIconW(h, MAKEINTRESOURCEW(1));
  }
  ~App() { release(staticBitmap); release(sitBase); }
  float dpiFor(HWND hwnd) {
    using F = UINT(WINAPI *)(HWND);
    auto f =
        reinterpret_cast<F>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    return f ? f(hwnd) / 96.f : 1.f;
  }
  bool motion() {
    BOOL enabled = TRUE;
    SystemParametersInfoW(0x1042, 0, &enabled, 0);
    return cfg.get<bool>("motion", true) && enabled && !resizing;
  }
  std::wstring tr(const wchar_t *zh, const wchar_t *en) { return cfg.tr(zh, en); }
  void hit(Window &win, std::string id, std::wstring key, float x, float y, float w, float h) {
    win.hits.push_back({id, key, D2D1::RectF(x, y, x + w, y + h)});
  }
  Hit *at(Window &win, float x, float y) {
    // The compact panel's view switch floats over the content in the top-right
    // corner. Give that small target priority when the pointer is over it.
    if (win.kind == 0)
      for (auto i = win.hits.rbegin(); i != win.hits.rend(); ++i)
        if (i->id == "switchView" && x >= i->r.left && x < i->r.right &&
            y >= i->r.top && y < i->r.bottom)
          return &*i;
    for (auto &h : win.hits)
      if (x >= h.r.left && x < h.r.right && y >= h.r.top && y < h.r.bottom)
        return &h;
    return nullptr;
  }
  static LRESULT CALLBACK procedure(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App *a = reinterpret_cast<App *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      auto cs = reinterpret_cast<CREATESTRUCTW *>(lp);
      a = static_cast<App *>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
    }
    return a ? a->event(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
  }
  Window &window(HWND hwnd) {
    if (hwnd == settings.hwnd)
      return settings;
    if (hwnd == detail.hwnd)
      return detail;
    if (hwnd == situation.hwnd)
      return situation;
    return panel;
  }
  void create(const std::wstring &cls) {
    className = cls;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = procedure;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = appIcon;
    wc.lpszClassName = className.c_str();
    RegisterClassExW(&wc);
    panel.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, className.c_str(),
                                 L"top", WS_POPUP | WS_THICKFRAME, 50, 50, 660, 331,
                                 nullptr, nullptr, instance, this);
    panel.dpi = dpiFor(panel.hwnd);
    settings.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, className.c_str(),
                                    L"top settings", WS_POPUP, 0, 0, 380, 320, panel.hwnd,
                                    nullptr, instance, this);
    detail.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
                                  className.c_str(), L"top details", WS_POPUP, 0, 0, 300,
                                  200, panel.hwnd, nullptr, instance, this);
    situation.hwnd = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_LAYERED, className.c_str(),
                                     L"top situation", WS_POPUP | WS_THICKFRAME, 0, 0, 840, 500, nullptr,
                                     nullptr, instance, this);
    situation.dpi = panel.dpi;
    int x = int(cfg.get<double>("x", 80) * panel.dpi),
        y = int(cfg.get<double>("y", 80) * panel.dpi),
        w = int(cfg.get<double>("width", 527) * panel.dpi),
        h = int(cfg.get<double>("height", 265) * panel.dpi);
    RECT proposed{x, y, x + w, y + h};
    auto monitor = MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(monitor, &mi);
    x = bounded(x, int(mi.rcWork.left), std::max(int(mi.rcWork.left), int(mi.rcWork.right - w)));
    y = bounded(y, int(mi.rcWork.top), std::max(int(mi.rcWork.top), int(mi.rcWork.bottom - h)));
    SetWindowPos(panel.hwnd, nullptr, x, y, w, h, SWP_NOACTIVATE | SWP_NOZORDER);
    applyConfig();
    sampler.start();
    tick();
    ShowWindow(panel.hwnd, SW_SHOWNOACTIVATE);
    SetTimer(panel.hwnd, 1, 60, nullptr);
  }
  void applyConfig() {
    theme.update(cfg);
    updatePowers(seconds(), false);
    sampler.settings(IsWindowVisible(panel.hwnd) && !IsIconic(panel.hwnd) && !occluded,
                     cfg.get<double>("interval", 1), cfg);
    SetWindowPos(panel.hwnd, cfg.get<bool>("floating", false) ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
                 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(situation.hwnd, cfg.get<bool>("floating", false) ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0,
                 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    roundWindow(panel.hwnd, panel.dpi);
    backdrop(panel.hwnd, !resizing, theme.dark);
    relayout(false);
    lastTray.clear();
    updateTray();
    dirty = true;
    if (situationVisible()) draw(situation);
  }
  void relayout(bool snapSize) {
    RECT r;
    GetClientRect(panel.hwnd, &r);
    LayoutEngine engine{int(bars.size()), cfg};
    // The compact panel has no permanent title/header strip. Let its content
    // use the full client area; the view switch is an unobtrusive hover action.
    layout = engine.choose(r.right / panel.dpi, std::max(20.f, r.bottom / panel.dpi));
    if (snapSize) {
      SetWindowPos(panel.hwnd, nullptr, 0, 0, int(std::lround(layout.w * layout.scale * panel.dpi)),
                   int(std::lround(layout.h * layout.scale * panel.dpi)),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    dirty = true;
  }
  void showPanel() {
    if (situationVisible()) hideSituation();
    overviewMode = false;
    if (IsIconic(panel.hwnd))
      ShowWindow(panel.hwnd, SW_RESTORE);
    RECT r;
    GetWindowRect(panel.hwnd, &r);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi);
    int w = r.right - r.left, h = r.bottom - r.top;
    int x = bounded(int(r.left), int(mi.rcWork.left), std::max(int(mi.rcWork.left), int(mi.rcWork.right) - w));
    int y = bounded(int(r.top), int(mi.rcWork.top), std::max(int(mi.rcWork.top), int(mi.rcWork.bottom) - h));
    ShowWindow(settings.hwnd, SW_HIDE);
    ShowWindow(panel.hwnd, SW_SHOWNOACTIVATE);
    // This path is an explicit user request (tray, settings, or show command).
    SetForegroundWindow(panel.hwnd);
    SetWindowPos(panel.hwnd, cfg.get<bool>("floating", false) ? HWND_TOPMOST : HWND_TOP,
                 x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    occluded = false;
    sampler.settings(true, cfg.get<double>("interval", 1), cfg);
    dirty = true;
    tick();
  }
  void saveGeometry() {
    RECT r;
    GetWindowRect(panel.hwnd, &r);
    cfg.load();
    cfg.data["x"] = r.left / panel.dpi;
    cfg.data["y"] = r.top / panel.dpi;
    cfg.data["width"] = (r.right - r.left) / panel.dpi;
    cfg.data["height"] = (r.bottom - r.top) / panel.dpi;
    cfg.data["positionUnits"] = "logical";
    cfg.save();
  }
  bool coveredByOtherWindow(bool requireFullCoverage, HWND target = nullptr) {
    if (!target) target = panel.hwnd;
    if (!IsWindowVisible(target) || IsIconic(target))
      return true;
    RECT rect;
    GetWindowRect(target, &rect);
    HRGN region = CreateRectRgn(rect.left + 3, rect.top + 3, rect.right - 3, rect.bottom - 3);
    bool covered = false;
    HWND cur = GetWindow(target, GW_HWNDPREV);
    for (; cur; cur = GetWindow(cur, GW_HWNDPREV)) {
      if (cur == settings.hwnd || cur == detail.hwnd || !IsWindowVisible(cur) || IsIconic(cur))
        continue;
      LONG_PTR ex = GetWindowLongPtrW(cur, GWL_EXSTYLE);
      DWORD cloak = 0;
      DwmGetWindowAttribute(cur, 14, &cloak, 4);
      // Sampling requires proven opaque coverage. A tray click should instead
      // raise a possibly obscured panel, including behind layered windows.
      if (cloak || (ex & WS_EX_TRANSPARENT) || (requireFullCoverage && (ex & WS_EX_LAYERED)))
        continue;
      RECT r, overlap;
      if (GetWindowRect(cur, &r) && IntersectRect(&overlap, &rect, &r)) {
        if (!requireFullCoverage) {
          covered = true;
          break;
        }
        HRGN part = CreateRectRgn(r.left, r.top, r.right, r.bottom);
        int result = CombineRgn(region, region, part, RGN_DIFF);
        DeleteObject(part);
        if (result == NULLREGION) {
          covered = true;
          break;
        }
      }
    }
    DeleteObject(region);
    return covered;
  }
  bool fullyCovered() { return coveredByOtherWindow(true); }
  void updateTray() {
    bool wanted = cfg.get<bool>("menuBar", true);
    if (!wanted) {
      if (trayAdded)
        Shell_NotifyIconW(NIM_DELETE, &tray);
      trayAdded = false;
      return;
    }
    auto reading = trayReading(cfg, snap);
    auto tip = L"top · " + reading.label + L" " + reading.value;
    bool li = light(true);
    // The notification area can be on a different DPI monitor from the panel.
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    int iconSize = std::max(16, int(std::round(16 * dpiFor(taskbar ? taskbar : panel.hwnd))));
    bool sameIcon = trayAdded && reading.text == lastTray && li == lastTrayLight && iconSize == lastTraySize;
    if (sameIcon && tip == lastTrayTip)
      return;
    tray = {};
    tray.cbSize = sizeof(tray);
    tray.hWnd = panel.hwnd;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_TIP | (sameIcon ? 0 : NIF_ICON);
    tray.uCallbackMessage = TRAY_MESSAGE;
    tray.hIcon = sameIcon ? nullptr : trayTextIcon(reading.text, iconSize, li);
    wcsncpy(tray.szTip, tip.c_str(), 127);
    bool added = Shell_NotifyIconW(trayAdded ? NIM_MODIFY : NIM_ADD, &tray) != FALSE;
    if (tray.hIcon) DestroyIcon(tray.hIcon);
    tray.hIcon = nullptr;
    trayAdded = added;
    lastTray = reading.text;
    lastTrayTip = tip;
    lastTraySize = iconSize;
    lastTrayLight = li;
  }
  void rank(double now) {
    auto mode = cfg.get<std::string>("processSort", "cpu");
    if (wide(mode) != sortMode) {
      order.clear();
      rankLevels.clear();
      sortMode = wide(mode);
    }
    std::map<std::wstring, double> values;
    for (auto &g : snap.groups)
      values[g.key] = mode == "cpu" ? g.cpu : g.bytes / 1048576.;
    double elapsed = rankTime ? now - rankTime : 1;
    rankTime = now;
    for (auto &v : values)
      rankLevels[v.first] = std::max(
          v.second, v.second + (rankLevels.count(v.first) ? rankLevels[v.first] - v.second : 0) *
                                   exp(-elapsed / 4));
    order.erase(
        std::remove_if(order.begin(), order.end(), [&](auto &k) { return !values.count(k); }),
        order.end());
    std::set<std::wstring> known(order.begin(), order.end());
    std::vector<std::wstring> added;
    for (auto &v : values)
      if (!known.count(v.first))
        added.push_back(v.first);
    std::sort(added.begin(), added.end(), [&](auto &a, auto &b) { return values[a] > values[b]; });
    order.insert(order.end(), added.begin(), added.end());
    rowFrom = rowTo;
    bool hold = !hoverKey.empty() || IsWindowVisible(detail.hwnd);
    if (!hold)
      for (size_t i = 1; i < order.size(); ++i)
        for (size_t j = 0; j < i; ++j)
          if (rankLevels[order[i]] > rankLevels[order[j]] * 1.12 + 1) {
            auto k = order[i];
            order.erase(order.begin() + i);
            order.insert(order.begin() + j, k);
            break;
          }
    rowTo.clear();
    for (size_t i = 0; i < order.size(); ++i)
      rowTo[order[i]] = double(i);
    transition = now;
    for (auto it = rankLevels.begin(); it != rankLevels.end();)
      if (!values.count(it->first))
        it = rankLevels.erase(it);
      else
        ++it;
  }
  void tick() {
    double now = seconds();
    bool situationChanged = false;
    if (now - lastEnv > 1) {
      lastEnv = now;
      occluded = fullyCovered();
      sitOccluded = coveredByOtherWindow(true, situation.hwnd);
      sampler.situation(situationActive());
      sampler.settings(IsWindowVisible(panel.hwnd) && !IsIconic(panel.hwnd) && !occluded,
                       cfg.get<double>("interval", 1), cfg);
      if (cfg.load())
        applyConfig();
      if (cfg.get<std::string>("appearance", "auto") == "auto" && theme.dark == light()) {
        theme.update(cfg);
        situationChanged = true;
        sitBaseDirty = true;
        backdrop(panel.hwnd, !resizing, theme.dark);
        dirty = true;
        if (IsWindowVisible(settings.hwnd))
          draw(settings);
      }
      updateTray();
    }
    if (sampler.revision() != snap.seq && !resizing) {
      snap = sampler.copy();
      sitBaseDirty = true;
      double interval = cfg.get<double>("interval", 1);
      for (size_t i = 0; i < bars.size(); ++i)
        bars[i].set(i < snap.cores.size() ? snap.cores[i] : -1, now, interval, motion(), i * .018);
      total.set(snap.cpu, now, interval, motion());
      gpuBar.set(snap.gpu, now, interval, motion());
      diskBar.set(snap.diskBusy, now, interval, motion());
      updatePowers(now, motion());
      rank(now);
      sampleAt = now;
      updateSituation(now);
      situationChanged = situationChanged || !sitFrozen;
      dirty = true;
      updateTray();
      if (IsWindowVisible(detail.hwnd))
        draw(detail);
    }
    checkPointer(now);
    bool animate = motion() && IsWindowVisible(panel.hwnd) && !occluded &&
                   (now - sampleAt < 2.8 || (showsFan() && fanRPM(now) > 0) || powerActive() || activityActive());
    if (animate)
      SetTimer(panel.hwnd, 2, 16, nullptr);
    else
      KillTimer(panel.hwnd, 2);
    if (situationActive() && motion() && !sitFrozen) {
      if (!sitTimerOn) { SetTimer(situation.hwnd, 3, 33, nullptr); sitTimerOn = true; }
    } else if (sitTimerOn) {
      KillTimer(situation.hwnd, 3);
      sitTimerOn = false;
    }
    if (dirty && IsWindowVisible(panel.hwnd) && !occluded) {
      draw(panel);
      dirty = false;
    }
    if (situationActive() && situationChanged && (!motion() || sitFrozen)) draw(situation);
    UINT wantedTick = ((IsWindowVisible(panel.hwnd) && !occluded) || situationActive()) ? 60 : 500;
    if (tickInterval != wantedTick) {
      tickInterval = wantedTick;
      SetTimer(panel.hwnd, 1, tickInterval, nullptr);
    }
  }
  void badge(Canvas &c, const std::string &id, float x, float y, D2D1_COLOR_F col,
             double value = -1) {
    c.circle(x + 10, y + 10, 10, theme.track);
    int index = activityIndex(id);
    double phase = index >= 0 && liveMotion() && activityValue(index) > 0 ? activityMotion[index].phase : -1;
    c.activityGlyph(id, x + 3, y + 3, 14, col, phase);
    if (value >= 0)
      c.arc(x + 10, y + 10, 9, value, col);
  }
  static std::wstring unit(double v, const wchar_t *suffix, int digits = 0) {
    return v >= 0 && std::isfinite(v) ? num(v, digits) + suffix : L"—";
  }
  Block chartArea(Block b) const { return {"", b.x, b.y + 31, b.w, std::max(1.f, b.h - 58)}; }
  void drawCores(Canvas &c, Block b) {
    badge(c, "cpu", b.x, b.y + 2, theme.cpu, snap.cpu);
    c.text(unit(snap.cpu, L"%"), b.x + 27, b.y, b.w - 27, 25, 20, theme.ink, true);
    hit(panel, "metric", L"cpu", b.x, b.y, b.w, 27);
    auto chart = cpuChart(int(bars.size()), chartArea(b));
    double now = seconds();
    for (size_t i = 0; i < chart.slots.size(); ++i) {
      auto r = chart.slots[i];
      double v = bars[i].value(now);
      float radius = chart.grid ? std::min(5.f, r.w / 3) : r.w / 2;
      c.fill(r.x, r.y, r.w, r.h, theme.track, radius);
      if (v >= 0) {
        if (chart.grid) {
          auto color = theme.cpu; color.a = float(.1 + .9 * bounded(v, 0., 100.) / 100);
          c.fill(r.x, r.y, r.w, r.h, color, radius);
        } else {
          float amount = std::min(r.h, std::max(r.w * .4f, float(r.h * bounded(v, 0., 100.) / 100)));
          c.fill(r.x, r.y + r.h - amount, r.w, amount, theme.cpu, std::min(radius, amount / 2));
        }
      } else if (r.w >= 8 && r.h >= 8)
        c.line(r.x + r.w * .25f, r.y + r.h / 2, r.x + r.w * .75f, r.y + r.h / 2, theme.secondary);
      if (!chart.grid && i < snap.ghz.size() && snap.ghz[i] >= 0) {
        float at = r.y + r.h * float(1 - bounded(snap.ghz[i] / cfg.get<double>("max_ghz", 5), 0., 1.));
        c.line(r.x + 2, at, r.x + r.w - 2, at, theme.ink, 1.5);
        hit(panel, "metric", L"coreClock:" + std::to_wstring(i), r.x,
            std::max(r.y, std::min(at - 5, r.y + r.h - 10)), r.w, std::min(10.f, r.h));
      }
      hit(panel, "metric", L"core:" + std::to_wstring(i), r.x, r.y, r.w, r.h);
    }
    float fy = b.y + b.h - 20;
    c.glyph("temperature", b.x + 1, fy + 2, 16, temperatureColor(snap.cpuTemp, theme.dark, theme.secondary));
    c.text(unit(snap.cpuTemp, L" °C"), b.x + 23, fy, 50, 20, 12, theme.ink);
    hit(panel, "metric", L"temperature", b.x, fy, 74, 20);
    if (layout.c.tier == "small" && cfg.has("gpu") && b.w >= 155)
      figure(c, "gpu", b.x + b.w - 77, fy, 77);
    else if (b.w >= 155) {
      double rpm = fanRPM(now);
      c.fan(b.x + 82, fy, float(fanMotion.angle), fanColor(rpm >= 0 ? fanMotion.colorRatio : -1, theme.dark, theme.secondary));
      c.text(unit(rpm, L" RPM"), b.x + 107, fy, b.w - 107, 20, 11, theme.ink);
      hit(panel, "metric", L"cooling", b.x + 79, fy, b.w - 79, 20);
    }
  }
  void drawRam(Canvas &c, Block b) {
    c.glyph("memory", b.x, b.y + 5, 17, theme.green);
    c.text(snap.used >= 0 ? num(snap.used / 1073741824, 1) : L"—", b.x + 20, b.y, b.w - 20, 25, 17, theme.ink, true);
    float w = std::min(42.f, b.w), x = b.x + (b.w - w) / 2, y = b.y + 31, h = b.h - 58;
    c.fill(x, y, w, h, theme.track, w / 2);
    if (snap.total > 0 && snap.used >= 0) {
      float amount = h * float(bounded(snap.used / snap.total, 0., 1.));
      if (amount > 0) c.fill(x, y + h - amount, w, amount, theme.green, std::min(w / 2, amount / 2));
    } else c.text(L"—", x, y + h / 2 - 10, w, 20, 12, theme.secondary, false, 1);
    c.text(snap.total > 0 ? num(snap.total / 1073741824, 0) + L" GiB" : L"—", b.x - 3, b.y + b.h - 20, b.w + 6, 20, 10, theme.secondary, false, 1);
    hit(panel, "metric", L"memory", b.x, b.y, b.w, b.h);
  }
  void summaryMeter(Canvas &c, Block b, bool gpu) {
    double value = gpu ? snap.gpu : snap.diskBusy;
    auto color = gpu ? theme.gpu : theme.secondary;
    badge(c, gpu ? "gpu" : "disk", b.x, b.y + 2, color);
    c.text(unit(value, L"%"), b.x + 27, b.y, b.w - 27, 25, gpu ? 20 : 16, gpu ? theme.ink : theme.secondary, gpu);
    c.fill(b.x, b.y + 34, b.w, 6, theme.track, 3);
    double visual = motion() ? (gpu ? gpuBar : diskBar).value(seconds()) : value;
    if (value >= 0 && visual > 0) c.fill(b.x, b.y + 34, b.w * float(bounded(visual, 0., 100.) / 100), 6, color, 3);
    hit(panel, "metric", gpu ? L"gpu" : L"disk", b.x, b.y, b.w, b.h);
  }
  void updatePowers(double now, bool animate) {
    auto next = power::resolve(cfg, snap);
    for (int i = 0; i < 4; ++i) {
      bool sameSource = powers[i].model == next[i].model && powers[i].estimated == next[i].estimated;
      powerNumbers[i].set(next[i].watts, now, .4, animate && sameSource);
    }
    powers = std::move(next);
  }
  bool liveMotion() {
    return motion() && !occluded && IsWindowVisible(panel.hwnd) && snap.when > 0 &&
           seconds() - snap.when < std::max(3., cfg.get<double>("interval", 1) * 2.5);
  }
  bool powerMoves(int index) const {
    return powers[index].watts > 0 && !(index == 2 && powers[index].estimated);
  }
  static int activityIndex(const std::string &id) {
    return id == "down" ? 0 : id == "up" ? 1 : id == "diskRead" ? 2 : id == "diskWrite" ? 3 : id == "disk" ? 4 : -1;
  }
  double activityValue(int i) const {
    const double values[] = {snap.down, snap.up, snap.diskRead, snap.diskWrite, snap.diskBusy};
    return values[i];
  }
  bool activityActive() {
    if (!liveMotion()) return false;
    for (const auto &b : layout.blocks) {
      if (b.kind == "network" && (snap.down > 0 || snap.up > 0)) return true;
      if ((b.kind == "diskRates" || b.kind == "diskSummary") && (snap.diskRead > 0 || snap.diskWrite > 0 || snap.diskBusy > 0)) return true;
    }
    return false;
  }
  bool powerActive() {
    if (!liveMotion()) return false;
    for (const auto &b : layout.blocks) {
      if (b.kind == "powers") {
        for (int i = 0; i < 4; ++i) if (powerMoves(i)) return true;
      } else if (b.kind == "fig:systemPower" && powerMoves(3)) return true;
    }
    return false;
  }
  void powerFigure(Canvas &c, int index, float x, float y, float w) {
    const char *glyphs[] = {"cpu", "gpu", "memory", "power"};
    const wchar_t *keys[] = {L"power", L"gpuPower", L"memoryPower", L"systemPower"};
    double value = motion() ? powerNumbers[index].value(seconds()) : powers[index].watts;
    auto color = index == 0 ? theme.cpu : index == 1 ? theme.gpu : index == 2 ? theme.green : theme.secondary;
    double phase = powerMoves(index) && liveMotion() ? powerMotion[index].phase : -1;
    c.powerGlyph(glyphs[index], x, y + 1, 19, color, phase);
    auto label = power::label(powers[index], value);
    c.text(label, x + 22, y, w - 22, 21, w < 80 ? 10.f : 11.f, theme.ink);
    hit(panel, "metric", keys[index], x, y, w, 23);
  }
  bool showsFan() const {
    for (const auto &b : layout.blocks)
      if (b.kind == "cpu" || b.kind == "cornercpu" || b.kind == "fig:temperature") return true;
    return false;
  }
  double fanRPM(double now) const {
    return snap.fans.fresh(now, cfg.get<double>("interval", 1)) ? snap.fans.fastest() : -1;
  }
  void figure(Canvas &c, const std::string &id, float x, float y, float w) {
    if (id == "systemPower") { powerFigure(c, 3, x, y, w); return; }
    auto color = id == "gpu" ? theme.gpu : id == "cpu" ? theme.cpu : id == "memory" ? theme.green : theme.secondary;
    badge(c, id, x, y, color);
    std::wstring value = L"—";
    if (id == "cpu") value = unit(snap.cpu, L"%");
    if (id == "gpu") value = unit(snap.gpu, L"%");
    if (id == "memory") value = unit(snap.total > 0 && snap.used >= 0 ? 100 * snap.used / snap.total : -1, L"%");
    if (id == "down") value = rate(snap.down);
    if (id == "up") value = rate(snap.up);
    if (id == "diskRead") value = rate(snap.diskRead);
    if (id == "diskWrite") value = rate(snap.diskWrite);
    c.text(value, x + 27, y, w - 27, 20, 12, theme.ink);
    hit(panel, "metric", wide(id), x, y, w, 20);
  }
  Group *findGroup(const std::wstring &key) {
    for (auto &g : snap.groups)
      if (g.key == key)
        return &g;
    return nullptr;
  }
  void drawProcesses(Canvas &c, Block b) {
    int cols = layout.c.wide ? 2 : 1, count = layout.c.count;
    float gap = 22, w = (b.w - (cols - 1) * gap) / cols, header = cols == 2 ? 28 : 0;
    bool byCpu = cfg.get<std::string>("processSort", "cpu") == "cpu";
    if (cols == 2) {
      for (int i = 0; i < 2; ++i) {
        float x = b.x + i * (w + gap);
        c.text(i == 0 ? L"CPU" : tr(L"内存", L"Memory"), x, b.y, w, 22, 11,
               i == (byCpu ? 0 : 1) ? theme.cpu : theme.secondary, true);
        hit(panel, i == 0 ? "sortCpu" : "sortMem", L"", x, b.y, w, 22);
      }
    }
    for (int i = 0; i < count * cols && i < int(order.size()); ++i) {
      auto *g = findGroup(order[i]);
      if (!g)
        continue;
      int column = i / count, row = i % count;
      float x = b.x + column * (w + gap), y = b.y + header + row * 28;
      auto old = rowFrom.find(g->key);
      double age = seconds() - transition;
      if (motion() && old != rowFrom.end() && int(old->second) / count == column &&
          abs(int(old->second) - i) <= 2 && age < .25)
        y += float((old->second - i) * 28 * pow(1 - age / .25, 3));
      if (hoverKey == g->key)
        c.fill(x - 4, y - 3, w + 8, 26, theme.track, 7);
      c.circle(x + 10, y + 10, 10, theme.track);
      if (g->icon)
        c.icon(g->key, g->icon, x + 3, y + 3, 14);
      else
        c.glyph("processes", x + 4, y + 4, 12, theme.secondary);
      c.arc(x + 10, y + 10, 9, std::min(100., g->cpu), theme.cpu);
      std::wstring label = g->name;
      if (g->members.size() > 1)
        label += L" ×" + std::to_wstring(g->members.size());
      c.text(label, x + 28, y, w - 88, 20, 12, theme.ink);
      c.text(byCpu ? num(g->cpu, g->cpu < 10 ? 1 : 0) + L"%" : mem(g->bytes), x + w - 62, y, 62, 20, 12, theme.ink,
             false, 2);
      hit(panel, "process", g->key, x - 4, y - 3, w + 8, 26);
    }
  }
  void drawPanel(Canvas &c) {
    for (const auto &b : layout.blocks) {
      // Only process rows are static. Sensor icons and numbers share the live sample.
      if (buildingBase && (b.kind != "processes" || !rowsCached)) continue;
      if (!buildingBase && b.kind == "processes" && rowsCached) continue;
      if (b.kind == "cpu") drawCores(c, b);
      else if (b.kind == "ram") drawRam(c, b);
      else if (b.kind == "gpuSummary") summaryMeter(c, b, true);
      else if (b.kind == "diskSummary") summaryMeter(c, b, false);
      else if (b.kind == "network" || b.kind == "diskRates") {
        figure(c, b.kind == "network" ? "down" : "diskRead", b.x, b.y, (b.w - 12) / 2);
        figure(c, b.kind == "network" ? "up" : "diskWrite", b.x + (b.w + 12) / 2, b.y, (b.w - 12) / 2);
      } else if (b.kind == "powers") {
        for (int i = 0; i < 4; ++i) powerFigure(c, i, b.x + i * b.w / 4, b.y, b.w / 4 - 3);
      } else if (b.kind == "processes") drawProcesses(c, b);
      else if (b.kind.rfind("fig:", 0) == 0) figure(c, b.kind.substr(4), b.x, b.y, b.w);
      else if (b.kind == "micro") {
        c.text(num(snap.cpu), b.x, b.y, 24, 20, 12, theme.cpu, true);
        c.text(num(snap.total > 0 && snap.used >= 0 ? 100 * snap.used / snap.total : -1), b.x + 26, b.y, 24, 20, 12, theme.green, true);
        c.text(num(snap.gpu), b.x + 52, b.y, 24, 20, 12, theme.gpu, true);
        hit(panel, "metric", L"cpu", b.x, b.y, 24, 20);
        hit(panel, "metric", L"memory", b.x + 26, b.y, 24, 20);
        hit(panel, "metric", L"gpu", b.x + 52, b.y, 24, 20);
      }
    }
  }
  void iconButton(Canvas &c, Window &win, const std::string &id, const std::string &glyph,
                  float x, float y, bool active = false) {
    bool over = win.hover == id, down = win.pressed == id;
    if (active || over || down) c.fill(x, y, 28, 28, down ? theme.track : theme.card, 6);
    c.glyph(glyph, x + 6, y + 6, 16, active ? theme.cpu : theme.secondary);
    hit(win, id, L"", x, y, 28, 28);
  }
  static constexpr int SettingsBase = 154, SettingsFull = SettingsBase + 10 * 34 + 10;
  void drawSettings(Canvas &c) {
    const float w = 300;
    auto edge = theme.ink; edge.a = .12f;
    c.fill(12, 10, 276, 34, theme.card, 8);
    c.glyph(overviewMode ? "expand" : "tray", 22, 19, 16, theme.cpu);
    c.text(overviewMode ? tr(L"态势总览", L"Overview") : tr(L"精简面板", L"Compact"), 46, 12, 160, 30, 12.5f, theme.ink);
    hit(settings, "switchView", L"", 16, 12, 194, 30);
    iconButton(c, settings, "pin", "pin", 220, 13, cfg.get<bool>("floating", false));
    iconButton(c, settings, "quit", "quit", 254, 13);
    const std::string ids[] = {"memory", "gpu", "network", "power", "processes"};
    const std::wstring labels[] = {tr(L"内存", L"Memory"), L"GPU", tr(L"网络", L"Network"), tr(L"功耗", L"Power"),
                                  tr(L"进程", L"Apps")};
    for (int i = 0; i < 5; ++i) {
      bool active = cfg.has(ids[i]);
      float cellW = 54.f;
      float x = 15 + i * cellW, y = 54.f;
      float cx = x + cellW / 2;
      auto selected = theme.cpu; selected.a = .10f;
      c.fill(x + 2, y, cellW - 4, 52, active ? selected : theme.card, 7);
      c.glyph(ids[i], cx - 8, y + 7, 16, active ? theme.cpu : theme.secondary);
      c.text(labels[i], x, y + 29, cellW, 20, 11, active ? theme.ink : theme.secondary, active, 1);
      hit(settings, ids[i], L"", x + 2, y, cellW - 4, 52);
    }
    c.line(16, 116, 284, 116, edge);
    c.text(more ? tr(L"收起设置", L"Fewer settings") : tr(L"更多设置", L"More settings"), 20, 120, 220, 28, 11, theme.ink);
    c.glyph(more ? "less" : "more", 262, 127, 14, theme.secondary);
    hit(settings, "more", L"", 16, 120, 268, 28);
    if (!more) return;
    RECT rr; GetClientRect(settings.hwnd, &rr);
    float height = rr.bottom / settings.dpi;
    c.rt->PushAxisAlignedClip(D2D1::RectF(8, SettingsBase, w - 8, height - 8), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const std::vector<std::pair<std::string, std::wstring>> rows = {
      {"menuBarShows", tr(L"托盘显示", L"Tray metric")},
      {"interval", tr(L"刷新间隔", L"Update every")},
      {"processSort", tr(L"进程排序", L"Sort apps by")},
      {"maxProcesses", tr(L"进程行数", L"App rows")},
      {"palette", tr(L"主题", L"Theme")},
      {"appearance", tr(L"外观", L"Appearance")},
      {"language", tr(L"语言", L"Language")},
      {"autostart", tr(L"开机启动", L"Start at login")},
      {"powerSetup", tr(L"功耗估算（高级）…", L"Power estimates (advanced)…")},
      {"openConfig", tr(L"打开设置文件", L"Open settings file")}
    };
    for (int i = 0; i < int(rows.size()); ++i) {
      float y = SettingsBase + i * 34.f - scroll;
      if (y < SettingsBase || y + 30 > height - 8) continue;
      const auto &id = rows[i].first;
      std::wstring value;
      if (id == "menuBarShows") value = trayReading(cfg, snap).label;
      else if (id == "interval") value = num(cfg.get<double>("interval", 1), cfg.get<double>("interval", 1) == .5 ? 1 : 0) + L" s";
      else if (id == "maxProcesses") value = std::to_wstring(cfg.get<int>("maxProcesses", 24));
      else if (id == "processSort") value = cfg.get<std::string>("processSort", "cpu") == "cpu" ? L"CPU" : tr(L"内存", L"Memory");
      else if (id == "palette") {
        auto v = cfg.get<std::string>("palette", "sea");
        value = v == "graphite" ? tr(L"石墨", L"Graphite") : v == "warm" ? tr(L"暖", L"Warm") : tr(L"海", L"Sea");
      } else if (id == "appearance") {
        auto v = cfg.get<std::string>("appearance", "auto");
        value = v == "auto" ? tr(L"跟随系统", L"System") : v == "dark" ? tr(L"深色", L"Dark") : tr(L"浅色", L"Light");
      } else if (id == "language") {
        auto v = cfg.get<std::string>("language", "auto");
        value = v == "auto" ? tr(L"跟随系统", L"System") : v == "zh" ? L"中文" : L"English";
      } else if (id == "autostart") value = autostartEnabled() ? tr(L"开启", L"On") : tr(L"关闭", L"Off");
      if (settings.hover == id) c.fill(12, y, 276, 30, theme.card, 5);
      c.text(rows[i].second, 20, y, value.empty() ? 244 : 150, 30, 11, theme.ink);
      if (!value.empty()) c.text(value, 174, y, 90, 30, 10, theme.secondary, false, 2);
      c.text(L"›", 267, y, 14, 30, 13, theme.secondary, false, 1);
      hit(settings, id, L"", 16, y, 268, 30);
    }
    c.rt->PopAxisAlignedClip();
    if (height + 1 < SettingsFull) {
      float trackH = height - SettingsBase - 12;
      float thumb = std::max(20.f, trackH * trackH / (SettingsFull - SettingsBase));
      c.fill(291, SettingsBase, 2, trackH, theme.track, 1);
      c.fill(291, SettingsBase + (trackH - thumb) * scroll / std::max(1.f, SettingsFull - height),
             2, thumb, theme.secondary, 1);
    }
  }
  std::wstring controlTip(const Hit &h) {
    if (h.id == "switchView") return overviewMode ? tr(L"切换到精简面板", L"Switch to compact") : tr(L"切换到态势总览", L"Switch to overview");
    if (h.id == "situation") return tr(L"打开态势总览", L"Open situation overview");
    if (h.id == "settings") return tr(L"设置", L"Settings");
    if (h.id == "pin") return cfg.get<bool>("floating", false) ? tr(L"取消置顶", L"Unpin panel") : tr(L"面板置顶", L"Keep panel on top");
    if (h.id == "hide") return tr(L"隐藏面板", L"Hide panel");
    if (h.id == "togglePanel") return IsWindowVisible(panel.hwnd) ? tr(L"隐藏面板", L"Hide panel") : tr(L"显示面板", L"Show panel");
    if (h.id == "quit") return tr(L"退出 top", L"Quit top");
    if (h.id == "sitFreeze") return sitFrozen ? tr(L"继续实时 · 空格", L"Resume live · Space") : tr(L"冻结 · 空格", L"Freeze · Space");
    if (h.id == "sitScope") {
      auto s = sitShown();
      return s ? std::to_wstring(s->entities.size())+tr(L" 组已采样 · ",L" sampled groups · ")+
        std::to_wstring(s->processCount)+tr(L" 进程",L" processes") : L"";
    }
    if (h.id == "sitEntity") {
      if (auto s = sitShown()) for (const auto &e:s->entities) if (e.key==h.key)
        return e.name+L" · "+mem(e.bytes)+L" · "+std::to_wstring(e.processes)+tr(L" 进程 · ",L" processes · ")+
          (sitPinned == h.key ? tr(L"取消固定",L"Unpin") : tr(L"点击固定",L"Click to pin"));
      return tr(L"点击固定",L"Click to pin");
    }
    if (h.id == "sitTime") return tr(L"点击回看", L"Click to review");
    if (h.id == "sitTab") return L"";
    if (h.id == "sitLane" || h.id == "sitHubLane") {
      const wchar_t *names[]={L"CPU",L"GPU",L"内存",L"磁盘",L"网络"};
      const wchar_t *english[]={L"CPU",L"GPU",L"Memory",L"Disk",L"Network"};
      int lane=std::stoi(h.key);
      return tr(names[lane],english[lane])+tr(L" · 点击筛选",L" · Click to filter");
    }
    return L"";
  }
  void drawControlFeedback(Canvas &c, Window &win, float w, float h) {
    const Hit *tipHit = nullptr;
    for (const auto &item : win.hits) {
      if (item.id == win.hover && item.key == win.hoverItem) {
        tipHit = &item;
        auto col = theme.ink; col.a = win.pressed.empty() ? .06f : .12f;
        c.fill(item.r.left, item.r.top, item.r.right - item.r.left, item.r.bottom - item.r.top, col, 5);
      }
    }
    bool keyboardTip = false;
    if (win.focus >= 0 && win.focus < int(win.hits.size())) {
      const auto &f = win.hits[win.focus];
      c.border(f.r.left + 1, f.r.top + 1, f.r.right - f.r.left - 2, f.r.bottom - f.r.top - 2, theme.ink, 5, 1);
      tipHit = &f; keyboardTip = true;
    }
    if (!tipHit || (!keyboardTip && !win.tipReady) || !win.pressed.empty()) return;
    auto label = controlTip(*tipHit);
    if (label.empty()) return;
    float size = win.kind == 3 ? 12.5f : 11.f;
    float width = std::min(w - 16, std::ceil(c.textWidth(label, size)) + 20);
    float x = bounded((tipHit->r.left + tipHit->r.right - width) / 2, 8.f, w - width - 8);
    float y = tipHit->r.top - 34;
    if (y < 8) y = tipHit->r.bottom + 6;
    if (y + 28 > h - 6) return;
    auto shadow = color(0x000000, .07f);
    auto edge = theme.ink; edge.a = .12f;
    c.fill(x - 1, y + 2, width + 2, 29, shadow, 8);
    c.fill(x, y, width, 28, theme.card, 7);
    c.border(x, y, width, 28, edge, 7, .7f);
    c.text(label, x + 10, y, width - 20, 28, size, theme.ink, false, 1);
  }

  std::wstring cpuSensorStatus() {
    std::wstring status = tr(L"读取未完成", L"Reading not ready");
    if (snap.sensorState == "ready") status = tr(L"每秒采样 · 只读", L"One sample per second · read-only");
    else if (snap.sensorState == "warming_up") status = tr(L"正在建立功耗基线", L"Establishing power baseline");
    else if (snap.sensorState == "driver_unavailable") status = tr(L"传感器驱动未就绪", L"Sensor driver not ready");
    else if (snap.sensorState == "module_missing" || snap.sensorState == "module_rejected") status = tr(L"传感器模块加载失败", L"Sensor module could not be loaded");
    else if (snap.sensorState == "affinity_failed") status = tr(L"CPU 采样线程初始化失败", L"CPU sampling thread initialization failed");
    else if (snap.sensorState == "driver_disabled") status = tr(L"top 传感器驱动已禁用", L"top sensor driver disabled");
    else if (snap.sensorState == "access_denied") status = tr(L"驱动拒绝访问，需要管理员权限", L"Driver access requires elevation");
    else if (snap.sensorState == "driver_signature_rejected") status = tr(L"Windows 未接受驱动签名", L"Windows rejected the driver signature");
    else if (snap.sensorState == "protocol_mismatch") status = tr(L"应用与驱动版本不匹配", L"App and driver versions do not match");
    else if (snap.sensorState == "read_failed") status = tr(L"CPU 传感器读取失败", L"CPU sensor read failed");
    else if (snap.sensorState == "unsupported_cpu") status = tr(L"此 CPU 尚未支持", L"CPU not supported yet");
    else if (snap.sensorState == "partial_or_failed") status = tr(L"部分传感器读取失败", L"Some sensor reads failed");
    return status;
  }
  std::vector<std::wstring> powerDetails(int index) {
    auto r = powers[index];
    std::vector<std::wstring> lines{
        index == 2 ? tr(L"内存功耗", L"Memory power") : tr(L"整机功耗", L"System power"),
        power::label(r, r.watts)};
    auto source = [&](const char *key) {
      auto text = wide(cfg.get<std::string>(key, ""));
      return text.size() > 20 ? text.substr(0, 20) + L"…" : text;
    };
    if (!power::valid(r.watts)) {
      lines.push_back(cfg.get<bool>("estimatePower", false)
          ? tr(L"数据不足，暂不可估算", L"Insufficient data to estimate")
          : tr(L"暂无读数 · 估算未开启", L"No reading · estimates off"));
    } else if (!r.estimated) {
      lines.push_back(index == 2 && r.model == "intel-dram-rapl"
          ? tr(L"Intel RAPL 按能量增量 ÷ 时间估计全部内存功耗。", L"Intel RAPL estimates all RAM power from energy delta / time.")
          : tr(L"读取硬件功耗传感器。", L"Read from the hardware power sensor."));
    } else if (index == 2) {
      lines.push_back(r.model == "samsung-ddr4-reference-v1"
          ? tr(L"按三星规格电流与固定工况，汇总各条内存参考功耗。", L"Samsung current specs and fixed workload give the combined RAM reference.")
          : tr(L"采用固定参考值，来源：", L"Fixed reference from: ") + source("memoryReferenceSource") + L"。");
    } else if (r.model == "component-sum-v1") {
      lines.push_back(tr(L"CPU＋GPU＋内存加基础参考值，来源：", L"CPU + GPU + RAM plus baseline from: ") + source("platformBaseSource") + L"。");
    } else {
      lines.push_back(tr(L"CPU＋GPU＋内存加硬件基础模型，粗估直流功耗（不含充电）。",
                         L"CPU + GPU + RAM plus a hardware baseline model roughly estimates DC power, excluding charging."));
    }
    // Wrap explanatory text without shrinking the existing tooltip font.
    std::vector<std::wstring> wrapped;
    for (auto line : lines) {
      std::wstring row; double width = 0;
      for (wchar_t ch : line) {
        double next = ch < 128 ? .58 : 1.;
        if (width + next > 26) { wrapped.push_back(row); row.clear(); width = 0; }
        row += ch; width += next;
      }
      wrapped.push_back(row);
    }
    return wrapped;
  }
  std::vector<std::wstring> details() {
    std::vector<std::wstring> lines;
    if (!detailKey.empty()) {
      auto *g = findGroup(detailKey);
      if (!g)
        return {tr(L"进程已退出", L"Process has exited")};
      lines = {g->name,
               L"CPU " + num(g->cpu, 1) + L"%    " + tr(L"内存 ", L"Memory ") + mem(g->bytes),
               std::to_wstring(g->members.size()) + tr(L" 个进程", L" processes")};
      auto members = g->members;
      std::sort(members.begin(), members.end(), [](auto &a, auto &b) { return a.cpu > b.cpu; });
      for (size_t i = 0; i < std::min(size_t(3), members.size()); ++i)
        lines.push_back(members[i].name + L" · PID " + std::to_wstring(members[i].pid) + L"   " +
                        num(members[i].cpu) + L"%");
      return lines;
    }
    if (detailMetric == "cooling") {
      bool fresh = snap.fans.fresh(seconds(), cfg.get<double>("interval", 1));
      lines = {tr(L"散热", L"Cooling"),
               L"CPU " + num(snap.cpuTemp) + L" °C"};
      if (snap.cpuTemp < 0) lines.push_back(cpuSensorStatus());
      for (size_t i = 0; i < snap.fans.rpm.size(); ++i)
        lines.push_back(tr(L"风扇 ", L"Fan ") + std::to_wstring(i + 1) + L"    " +
                        num(fresh ? snap.fans.rpm[i] : -1) + tr(L" 转/分钟", L" RPM"));
      if (!fresh && snap.fans.rpm.empty()) lines.push_back(tr(L"风扇：—", L"Fans: —"));
      return lines;
    }
    if (detailMetric == "memoryPower") return powerDetails(2);
    if (detailMetric == "systemPower") return powerDetails(3);
    if (detailMetric == "disk" || detailMetric == "diskRead" || detailMetric == "diskWrite")
      return {tr(L"磁盘汇总", L"All disks"),
              tr(L"活动时间  ", L"Active time  ") + unit(snap.diskBusy, L"%"),
              tr(L"读取  ", L"Read  ") + rate(snap.diskRead), tr(L"写入  ", L"Write  ") + rate(snap.diskWrite)};
    if (detailMetric == "gpuClock")
      return {tr(L"GPU 频率", L"GPU frequency"), num(snap.gpuClock >= 0 ? snap.gpuClock * 1000 : -1) + L" MHz"};
    if (detailMetric.rfind("coreClock:", 0) == 0) {
      size_t i = std::stoul(detailMetric.substr(10));
      double ghz = i < snap.ghz.size() ? snap.ghz[i] : -1;
      return {tr(L"逻辑处理器 ", L"Logical processor ") + std::to_wstring(i + 1) + tr(L" · 频率", L" · Frequency"),
              tr(L"估计 ", L"Estimated ") + num(ghz >= 0 ? ghz * 1000 : -1) + L" MHz"};
    }
    if (detailMetric == "cpu") {
      lines = {L"CPU " + unit(snap.cpu, L"%"), std::to_wstring(bars.size()) + tr(L" 个逻辑处理器", L" logical processors")};
      return lines;
    }
    if (detailMetric.rfind("core:", 0) == 0) {
      size_t i = std::stoul(detailMetric.substr(5));
      return {tr(L"逻辑处理器 ", L"Logical processor ") + std::to_wstring(i + 1),
              L"CPU " + num(i < snap.cores.size() ? snap.cores[i] : -1) + L"%",
              tr(L"估计 ", L"Estimated ") + num(i < snap.ghz.size() ? snap.ghz[i] : -1, 2) + L" GHz"};
    }
    if (detailMetric == "memory") {
      auto traffic = [](double v) { return v >= 999500000 ? num(v / 1e9, 1) + L" GB/s" : rate(v); };
      return {tr(L"内存", L"Memory"), mem(snap.used) + L" / " + mem(snap.total),
              tr(L"读取：", L"Read: ") + traffic(snap.memoryRead),
              tr(L"写入：", L"Write: ") + traffic(snap.memoryWrite),
              tr(L"分页已用：", L"Pagefile used: ") + mem(snap.swap)};
    }
    if (detailMetric == "gpuPower")
      return {tr(L"GPU 功耗", L"GPU power"), unit(snap.gpuPower, L" W", 1),
              tr(L"NVML 读取显卡板级功耗。", L"NVML reports GPU board power.")};
    if (detailMetric == "gpu" || detailMetric == "gpuMem")
      return {L"NVIDIA GPU", num(snap.gpu) + L"% · " + num(snap.gpuTemp) + L" °C",
              num(snap.gpuClock, 2) + L" GHz · " + num(snap.gpuPower, 1) + L" W",
              tr(L"显存 ", L"VRAM ") + (snap.gpuMem >= 0 ? mem(snap.gpuMem) + L" / " + mem(snap.gpuTotal) : L"—")};
    if (detailMetric == "down" || detailMetric == "up")
      return {tr(L"网络", L"Network"), L"↓ " + rate(snap.down), L"↑ " + rate(snap.up)};
    if (detailMetric == "power")
      return {tr(L"CPU 封装功耗", L"CPU package power"), unit(snap.cpuPower, L" W", 1),
              snap.cpuPower >= 0 ? tr(L"Intel RAPL 按能量增量 ÷ 时间估计功耗。", L"Intel RAPL: energy delta / time.") : cpuSensorStatus()};
    lines = {tr(L"CPU 温度", L"CPU temperature"), unit(snap.cpuTemp, L" °C")};
    if (snap.cpuTemp < 0) lines.push_back(cpuSensorStatus());
    return lines;
  }
  void drawDetail(Canvas &c) {
    auto lines = details();
    for (size_t i = 0; i < lines.size(); ++i)
      c.text(lines[i], 14, 12 + float(i) * 22, 292, 22, i == 0 ? 12.f : 11.f,
             i < 2 ? theme.ink : theme.secondary, i == 0);
  }

  // ---------- situation overview ----------
  bool situationVisible() const { return IsWindowVisible(situation.hwnd) != 0; }
  bool situationActive() const { return situationVisible() && !IsIconic(situation.hwnd) && !sitOccluded; }
  bool situationFresh() {
    auto *s = sitHistory.latest();
    if (!s || s->when <= 0) return false;
    return seconds() - s->when < std::max(3., cfg.get<double>("interval", 1) * 2.5);
  }
  const SitSample *sitShown() {
    if (sitHistory.samples.empty()) return nullptr;
    if (sitCursor < sitHistory.samples.size()) return &sitHistory.samples[sitCursor];
    return sitHistory.latest();
  }
  void updateSituation(double now, bool force = false) {
    if ((!situationActive() && !force) || (sitFrozen && !force)) return;
    SitSample frame = situationSample(snap, now, 12, sitPinned);
    if (!sitPinned.empty()) {
      auto found = std::find_if(frame.entities.begin(), frame.entities.end(),
                               [&](const SitEntity &e) { return e.key == sitPinned; });
      if (found != frame.entities.end()) sitPinnedEntity = *found;
      else {
        auto missing = sitPinnedEntity;
        missing.cpu = missing.bytes = -1;
        missing.gpu = missing.disk = missing.net = -1;
        missing.processes = 0;
        missing.available = false;
        frame.entities.insert(frame.entities.begin(), missing);
      }
    }
    if (sitKeys.empty() || now - sitRankAt > 5) {
      std::vector<std::wstring> next;
      if (!sitPinned.empty()) next.push_back(sitPinned);
      auto has = [&](const std::wstring &k) {
        for (const auto &e : frame.entities) if (e.key == k) return true;
        return false;
      };
      for (const auto &k : sitKeys)
        if (k != sitPinned && has(k) && next.size() < 6) next.push_back(k);
      for (const auto &e : frame.entities) {
        if (next.size() >= 6) break;
        if (std::find(next.begin(), next.end(), e.key) == next.end()) next.push_back(e.key);
      }
      sitKeys = next;
      sitRankAt = now;
    }
    std::vector<SitEntity> ordered;
    for (const auto &k : sitKeys)
      for (const auto &e : frame.entities)
        if (e.key == k) { ordered.push_back(e); break; }
    frame.entities = ordered;
    sitHistory.push(std::move(frame));
    if (sitCursor != (size_t)-1) sitCursor = sitHistory.samples.size() - 1;
  }
  void seedSituation() {
    if (!sitHistory.samples.empty() && situationFresh()) return;
    sitHistory.clear();
    sitKeys.clear();
    sitCursor = (size_t)-1;
    if (snap.when > 0) updateSituation(seconds(), true);
  }
  void saveSituationGeometry() {
    if (!situation.hwnd) return;
    RECT r;
    GetWindowRect(situation.hwnd, &r);
    cfg.load();
    cfg.data["sitX"] = r.left / situation.dpi;
    cfg.data["sitY"] = r.top / situation.dpi;
    cfg.data["sitW"] = (r.right - r.left) / situation.dpi;
    cfg.data["sitH"] = (r.bottom - r.top) / situation.dpi;
    cfg.save();
  }
  void showSituation() {
    overviewMode = true;
    sitOccluded = false;
    if (!situationFresh()) { sitFrozen = false; sitCursor = (size_t)-1; }
    seedSituation();
    situation.dpi = dpiFor(panel.hwnd);
    // Apply the compact default once to older saved geometry, then respect resizing.
    bool compactGeometry = cfg.get<int>("sitGeometryVersion", 0) < 2;
    double savedW = cfg.get<double>("sitW", 840), savedH = cfg.get<double>("sitH", 500);
    if (savedW <= 0) savedW = 840;
    if (savedH <= 0) savedH = 500;
    if (compactGeometry) { savedW = std::min(savedW, 840.); savedH = std::min(savedH, 500.); }
    int w = int(savedW * situation.dpi), h = int(savedH * situation.dpi);
    int x = int(cfg.get<double>("sitX", -1) * situation.dpi);
    int y = int(cfg.get<double>("sitY", -1) * situation.dpi);
    if (x < 0 || y < 0) {
      RECT pr;
      GetWindowRect(panel.hwnd, &pr);
      MONITORINFO mi{sizeof(mi)};
      GetMonitorInfoW(MonitorFromRect(&pr, MONITOR_DEFAULTTONEAREST), &mi);
      x = int(mi.rcWork.left) + std::max(0, int((mi.rcWork.right - mi.rcWork.left - w) / 2));
      y = int(mi.rcWork.top) + std::max(0, int((mi.rcWork.bottom - mi.rcWork.top - h) / 2));
    }
    ShowWindow(settings.hwnd, SW_HIDE);
    closeDetail();
    ShowWindow(panel.hwnd, SW_HIDE);
    KillTimer(panel.hwnd, 2);
    release(staticBitmap);
    staticOwner = nullptr;
    staticCanvas.clear();
    panel.canvas.clear();
    settings.canvas.clear();
    detail.canvas.clear();
    RECT proposed{x, y, x + w, y + h};
    MONITORINFO work{sizeof(work)};
    GetMonitorInfoW(MonitorFromRect(&proposed, MONITOR_DEFAULTTONEAREST), &work);
    w = std::min(std::max(w, int(SituationLayout::MinWidth * situation.dpi)), int(work.rcWork.right - work.rcWork.left));
    h = std::min(std::max(h, int(SituationLayout::MinHeight * situation.dpi)), int(work.rcWork.bottom - work.rcWork.top));
    x = bounded(x, int(work.rcWork.left), int(work.rcWork.right - w));
    y = bounded(y, int(work.rcWork.top), int(work.rcWork.bottom - h));
    SetWindowPos(situation.hwnd, cfg.get<bool>("floating",false) ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, w, h, SWP_NOACTIVATE);
    if (compactGeometry) {
      cfg.data["sitGeometryVersion"] = 2;
      cfg.data["sitW"] = w / situation.dpi;
      cfg.data["sitH"] = h / situation.dpi;
      cfg.save();
    }
    roundWindow(situation.hwnd, situation.dpi);
    draw(situation);
    ShowWindow(situation.hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(situation.hwnd);
    SetTimer(situation.hwnd, 3, 33, nullptr);
    sitTimerOn = true;
    sampler.situation(true);
    sampler.settings(false, cfg.get<double>("interval", 1), cfg);
  }
  void hideSituation() {
    if (!situation.hwnd) return;
    sampler.situation(false);
    sitFrozen = false;
    sitCursor = (size_t)-1;
    KillTimer(situation.hwnd, 3);
    sitTimerOn = false;
    saveSituationGeometry();
    ShowWindow(situation.hwnd, SW_HIDE);
    clearSituationBase();
    situation.canvas.clear();
  }
  void toggleSituation() {
    if (situationVisible()) hideSituation();
    else showSituation();
  }
  static D2D1_COLOR_F sitFade(D2D1_COLOR_F c, float a) { c.a *= a; return c; }
  void sitBadge(Canvas &c, const SitEntity &e, float x, float y, float size, float opacity = 1) {
    auto *g = findGroup(e.key);
    c.fill(x-2, y-2, size+4, size+4, sitFade(theme.card, opacity), 5);
    if (g && g->icon) c.icon(e.key, g->icon, x, y, size, opacity);
    else {
      const D2D1_COLOR_F accents[] = {theme.cpu, theme.gpu, theme.green, theme.eff, theme.secondary};
      auto accent = accents[situationIdentity(e.key)%5];
      c.fill(x-2, y-2, size+4, size+4, sitFade(accent, .15f*opacity), 5);
      c.border(x-2, y-2, size+4, size+4, sitFade(accent, .4f*opacity), 5, .8f);
      c.text(situationInitials(e), x-2, y-2, size+4, size+4, 10, sitFade(theme.ink, opacity), true, 1);
    }
  }
  static D2D1_COLOR_F sitMix(D2D1_COLOR_F a, D2D1_COLOR_F b, float t) {
    return D2D1::ColorF(a.r+(b.r-a.r)*t,a.g+(b.g-a.g)*t,a.b+(b.b-a.b)*t,a.a+(b.a-a.a)*t);
  }
  static D2D1_POINT_2F sitCurvePoint(float x1, float y1, float x2, float y2, float t) {
    float u=1-t, dx=x2-x1;
    return {u*u*u*x1+3*u*u*t*(x1+dx*.55f)+3*u*t*t*(x2-dx*.55f)+t*t*t*x2,
            u*u*u*y1+3*u*u*t*y1+3*u*t*t*y2+t*t*t*y2};
  }
  static void sitBranch(Canvas &c, float x1, float y1, float x2, float y2,
                        D2D1_COLOR_F col, float width=2, bool dashed=false) {
    float dx=(x2-x1)*.55f;
    c.bezier({x1,y1},{x1+dx,y1},{x2-dx,y2},{x2,y2},col,width,dashed);
  }
  static D2D1_POINT_2F sitFlowPoint(float x1, float y1, float hub, float x2, float y2,
                                   double progress, float center=250, float *pathLength=nullptr,
                                   float hubWidth=0) {
    // Arc-length lookup over the exact rendered cubics keeps icons on the line.
    std::array<D2D1_POINT_2F, 50> points;
    for (int i=0;i<=24;++i) points[i]=sitCurvePoint(x1,y1,hub-hubWidth/2,center,i/24.f);
    for (int i=0;i<=24;++i) points[25+i]=sitCurvePoint(hub+hubWidth/2,center,x2,y2,i/24.f);
    float total=0;
    // The hub is a handoff between two visible curves, not an invisible journey.
    // Its width must not consume animation time while the badge is fully masked.
    for (size_t i=1;i<points.size();++i) if (i!=25)
      total+=std::hypot(points[i].x-points[i-1].x,points[i].y-points[i-1].y);
    if (pathLength) *pathLength=total;
    float distance=float(bounded(progress,0.,1.))*total;
    for (size_t i=1;i<points.size();++i) {
      if (i==25) continue;
      auto a=points[i-1], b=points[i]; float length=std::hypot(b.x-a.x,b.y-a.y);
      if (distance<=length && length>0) {
        float t=distance/length; return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
      }
      distance-=length;
    }
    return points.back();
  }
  void sitSpark(Canvas &c, const std::vector<double> &values, float x, float y, float w, float h,
                D2D1_COLOR_F col) {
    if (values.size() < 2) return;
    double maxValue = 1;
    for (double v : values) if (v > maxValue) maxValue = v;
    auto edge = sitFade(col, .75f);
    bool started = false;
    D2D1_POINT_2F prev{};
    for (size_t i = 0; i < values.size(); ++i) {
      if (values[i] < 0) { started = false; continue; }
      D2D1_POINT_2F p{x + w * float(i) / float(values.size() - 1),
                      y + h - h * float(bounded(values[i] / maxValue, 0., 1.))};
      if (started) c.line(prev.x, prev.y, p.x, p.y, edge, 1.2f);
      prev = p;
      started = true;
    }
  }
  std::vector<double> sitSeries(const std::wstring &key) {
    std::vector<double> out;
    for (size_t i = 0; i < sitHistory.size() && (sitCursor == (size_t)-1 || i <= sitCursor); ++i) {
      const auto &s = sitHistory.samples[i];
      double v = -1;
      for (const auto &e : s.entities) if (e.key == key) { v = sitAppValue(e, sitMode); break; }
      out.push_back(v);
    }
    return out;
  }
  std::vector<double> sitLaneSeries(int lane) {
    std::vector<double> out;
    for (size_t i = 0; i < sitHistory.size() && (sitCursor == (size_t)-1 || i <= sitCursor); ++i) {
      const auto &s = sitHistory.samples[i];
      out.push_back(lane == 0 ? s.cpu : lane == 1 ? s.gpu : lane == 2 ? s.mem
                       : lane == 3 ? s.disk / 1048576. : s.net / 1048576.);
    }
    return out;
  }
  static std::wstring sitName(const std::wstring &raw) {
    std::wstring name = raw;
    if (name.size() > 4 && lower(name.substr(name.size() - 4)) == L".exe")
      name.resize(name.size() - 4);
    return name;
  }
  void drawSituation(Canvas &c, bool animationOnly = false) {
    RECT r; GetClientRect(situation.hwnd, &r);
    float w = r.right / situation.dpi, h = r.bottom / situation.dpi;
    std::wstring keyboardKey;
    if(situation.focus>=0 && situation.focus<int(situation.hits.size()) &&
       situation.hits[situation.focus].id=="sitEntity") keyboardKey=situation.hits[situation.focus].key;
    if (!animationOnly) situation.hits.clear();
    const auto layout = SituationLayout::choose(w,h);
    const float pad = layout.pad, leftW = layout.leftW, rightW = layout.rightW;
    const float contentW = layout.compact ? w-2*pad : std::min(w-2*pad,860.f);
    const float leftX = (w-contentW)/2, rightX = (w+contentW)/2-rightW;
    const float top = layout.top, bottom = layout.bottom, bodyH = bottom - top;
    const int visibleSlots = layout.slots;
    const float cardGap = layout.gap, cardH = layout.cardH, laneH = layout.laneH;
    auto edge = theme.ink; edge.a = .12f;
    auto *s = sitShown();
    SitSample visible;
    if (s) {
      visible=*s;
      if (visible.entities.size()>size_t(visibleSlots)) {
        auto pinned=std::find_if(visible.entities.begin()+visibleSlots,visible.entities.end(),
          [&](const SitEntity &e){ return e.key==sitPinned; });
        if (pinned!=visible.entities.end()) std::rotate(visible.entities.begin(),pinned,pinned+1);
        visible.entities.resize(visibleSlots);
      }
      s=&visible;
    }
    if (!animationOnly) {
    if (!layout.compact) c.text(L"top", pad, 16, 100, 28, 18, theme.ink, true);
    const std::wstring tabs[] = {tr(L"总览", L"Overview"), L"CPU", L"GPU", tr(L"内存", L"Memory"), L"I/O"};
    float tabX = layout.compact ? pad : (w - layout.tabW) / 2, tabW = layout.tabW / 5, tabY = layout.tabY;
    c.fill(tabX, tabY, layout.tabW, 30, theme.track, 8);
    for (int i = 0; i < 5; ++i) {
      float x = tabX + i * tabW;
      if (sitMode == i) c.fill(x + 2, tabY+2, tabW-4, 26, theme.card, 6);
      c.text(tabs[i], x, tabY+1, tabW, 28, 13, sitMode == i ? theme.ink : theme.secondary, sitMode == i, 1);
      hit(situation, "sitTab", std::to_wstring(i), x, tabY, tabW, 30);
    }
    float toggleX=w-pad-70;
    c.fill(toggleX,16,32,30,theme.card,6);
    c.glyph(sitFrozen ? "play" : "pause",toggleX+8,23,16,theme.ink);
    hit(situation,"sitFreeze",L"",toggleX,16,32,30);
    iconButton(c, situation, "switchView", "tray", w - pad - 28, 16);
    }
    if (!s) {
      sitBadges.clear();
      c.text(tr(L"等待采样…", L"Waiting for samples…"), pad, h / 2 - 20, w - 2 * pad, 40, 13, theme.secondary, false, 1);
      return;
    }
    bool memoryFlow = sitMode == 3;
    std::wstring scope=tr(L"应用",L"Apps");
    if (s->appCount) scope+=L" · "+std::to_wstring(s->appCount);
    scope+=sitMode==2 ? L" · GPU" : sitMode==3 ? tr(L" · 内存",L" · Memory") : sitMode==4 ? L" · I/O" : L" · CPU";
    if (!animationOnly) {
      c.text(scope,leftX,layout.labelY,leftW,22,12.5f,theme.secondary);
      hit(situation,"sitScope",L"",leftX,layout.labelY,leftW,20);
    }
    const std::wstring labels[] = {L"CPU", L"GPU", tr(L"内存", L"Memory"), tr(L"磁盘", L"Disk"), tr(L"网络", L"Network")};
    const std::string icons[] = {"cpu", "gpu", "memory", "disk", "network"};
    const D2D1_COLOR_F colors[] = {theme.cpu, theme.gpu, theme.green, theme.eff,
      cfg.get<std::string>("palette","sea")=="sea" ? color(theme.dark ? 0x62B7F6 : 0x288BC0) : theme.secondary};
    std::wstring focusedKey = !sitHover.empty() ? sitHover : !keyboardKey.empty() ? keyboardKey : sitPinned;
    const SitEntity *compactFocus=nullptr;
    if (layout.compact && sitHoverLane<0)
      for (const auto &e:s->entities) if (e.key==focusedKey) { compactFocus=&e; break; }
    if (!animationOnly) c.text(compactFocus ? sitName(compactFocus->name) : tr(L"资源",L"Resources"),rightX,layout.labelY,rightW,22,13,theme.secondary);
    const auto links = situationLinks(*s, sitMode);
    const float startX = leftX+leftW, hubX = (startX+rightX)/2;
    const float flowCenter=(top+bottom)/2;
    const float hubSize=layout.hubSize, hubLeft=hubX-hubSize/2,
                hubRight=hubX+hubSize/2, hubTop=flowCenter-hubSize/2;
    const float appHeight=float(s->entities.size())*(cardH+cardGap)-cardGap;
    const float appTop=top+std::max(0.f,(bodyH-appHeight)/2);
    auto appY = [&](size_t i) { return appTop+float(i)*(cardH+cardGap)+cardH/2; };
    auto laneY = [&](int i) { return top+i*(laneH+cardGap)+laneH/2; };
    auto relevant = [&](const SituationLink &link) {
      return sitHoverLane >= 0 ? link.lane == sitHoverLane :
             focusedKey.empty() || s->entities[link.entity].key == focusedKey;
    };
    bool hasFocus = !focusedKey.empty() || sitHoverLane >= 0;
    bool laneAttributed[5]{}, laneUsed[5]{}, appUsed[6]{}, laneFocus[5]{}, appFocus[6]{};
    bool laneActive[5]{}, appActive[6]{};
    for (const auto &link:situationLinks(*s,0)) laneAttributed[link.lane]=true;
    for (const auto &link : links) {
      laneUsed[link.lane] = appUsed[link.entity] = true;
      if (relevant(link)) laneFocus[link.lane] = appFocus[link.entity] = true;
      if (link.value>0 && relevant(link)) laneActive[link.lane]=appActive[link.entity]=true;
    }
    for (int i=0;i<5;++i) if (situationLaneFocused(sitMode,i) && sitLaneValue(*s,i)>=0) {
      laneUsed[i]=true;
      // System-only activity is never assigned to an arbitrary application.
      if (!laneAttributed[i] && !hasFocus) laneActive[i]=sitLaneValue(*s,i)>0;
    }
    double flowNow = seconds(), elapsed = sitFlowAt > 0 ? bounded(flowNow-sitFlowAt, 0., .1) : 0;
    sitFlowAt = flowNow;
    bool animate=motion() && !sitFrozen;
    float appEmphasis[6]{}, laneEmphasis[5]{}, hubEmphasis=0;
    sitFocusMoving = false;
    auto neutral=sitMix(theme.card,theme.secondary,.38f);
    for (size_t i=0;i<s->entities.size();++i) {
      float target=hasFocus ? (appUsed[i] && appFocus[i] ? 1.f : 0.f) : (appActive[i] ? .30f : 0.f);
      appEmphasis[i]=sitAppFocus[s->entities[i].key].step(target,elapsed,animate);
      sitFocusMoving |= appEmphasis[i] != target;
      hubEmphasis=std::max(hubEmphasis,target);
      // One shared port on each card edge; gray remains visible when inactive.
      if (!animationOnly && appUsed[i]) sitBranch(c,startX,appY(i),hubLeft,flowCenter,
        sitMix(sitFade(theme.secondary,.18f),sitFade(theme.cpu,.85f),appEmphasis[i]),1.5f);
    }
    for (int i=0;i<5;++i) {
      float target=hasFocus ? (laneUsed[i] && laneFocus[i] ? 1.f : 0.f) : (laneActive[i] ? .30f : 0.f);
      laneEmphasis[i]=sitLaneFocus[i].step(target,elapsed,animate);
      hubEmphasis=std::max(hubEmphasis,target);
      sitFocusMoving |= laneEmphasis[i] != target;
      if (!animationOnly && laneUsed[i]) sitBranch(c,hubRight,flowCenter,rightX,laneY(i),
        sitMix(sitFade(theme.secondary,.18f),sitFade(colors[i],.85f),laneEmphasis[i]),1.5f,!laneAttributed[i]);
    }
    float hubTarget=hubEmphasis;
    hubEmphasis=sitHubFocus.step(hubTarget,elapsed,animate);
    sitFocusMoving |= hubEmphasis != hubTarget;
    if (sitFrozen || !motion()) elapsed = 0;
    std::set<std::wstring> visibleTokens;
    struct MovingBadge { size_t entity; int lane; D2D1_POINT_2F p; float opacity; bool emphasized; };
    std::vector<MovingBadge> badges;
    for (size_t i=0; i<s->entities.size() && i<6; ++i) {
      const auto &e = s->entities[i];
      std::vector<int> active;
      for (const auto &link : links)
        if (link.entity==i && link.lane!=2 && link.value>0) active.push_back(link.lane);
      auto &token = sitTokens[e.key]; visibleTokens.insert(e.key);
      float pathLength=560;
      int route=token.lane>=0 ? token.lane : active.empty() ? -1 : active.front();
      if (route>=0) sitFlowPoint(startX,appY(i),hubX,rightX,laneY(route),0,flowCenter,&pathLength,hubSize);
      double reading = route==0 ? e.cpu : route==1 ? e.gpu : route==3 ? e.disk : route==4 ? e.net : -1;
      double speed = token.followSpeed(route,reading,elapsed);
      token.step(active, elapsed, double(situationIdentity(e.key)%71)/100.,pathLength/speed);
      if (token.lane<0 || sitFrozen || !motion()) continue; // Static states show relationships, not stranded tokens.
      auto p = sitFlowPoint(startX,appY(i),hubX,rightX,laneY(token.lane),token.progress,flowCenter,nullptr,hubSize);
      bool emphasized = sitHoverLane>=0 ? token.lane==sitHoverLane : focusedKey.empty() || e.key==focusedKey;
      // Pass behind the hub. Ease out/in at the ports instead of abruptly clipping icons.
      float portFade=bounded(std::max(hubLeft-p.x,p.x-hubRight)/24.f,0.f,1.f);
      portFade=portFade*portFade*(3-2*portFade);
      badges.push_back({i,token.lane,p,float(token.opacity())*portFade,emphasized});
    }
    // Overlap never toggles visibility. Keep the selected app above other icons.
    std::stable_sort(badges.begin(),badges.end(),[](const MovingBadge &a,const MovingBadge &b) {
      return a.emphasized<b.emphasized;
    });
    sitBadges.clear();
    sitPorts[0]=startX; sitPorts[1]=hubLeft; sitPorts[2]=hubRight; sitPorts[3]=rightX;
    for (const auto &b:badges)
      sitBadges.push_back({s->entities[b.entity],b.p,
        sitFade(b.emphasized ? colors[b.lane] : neutral,.8f*b.opacity),b.opacity});
    for (auto it=sitTokens.begin(); it!=sitTokens.end();)
      if (!visibleTokens.count(it->first)) it=sitTokens.erase(it); else ++it;
    for (auto it=sitAppFocus.begin();it!=sitAppFocus.end();)
      if (!visibleTokens.count(it->first)) it=sitAppFocus.erase(it); else ++it;
    if (animationOnly) return;
    // Compact hub: five equal sectors identify resources, never percentage slices.
    auto focusCard=[&](float x,float y,float width,float height,D2D1_COLOR_F accent,float amount) {
      c.fill(x,y,width,height,theme.card,10);
      c.fill(x,y,width,height,sitFade(accent,.025f*amount),10);
      c.border(x,y,width,height,sitMix(sitFade(theme.secondary,.10f),sitFade(accent,.55f),amount),10,1);
    };
    if (layout.compact) {
      c.circle(hubX,flowCenter,14,theme.card);
      c.circle(hubX,flowCenter,5,sitMix(neutral,theme.cpu,hubEmphasis));
    } else {
    focusCard(hubLeft,hubTop,hubSize,hubSize,theme.cpu,hubEmphasis);
    float outer=hubSize*.40f, inner=hubSize*.22f, glyphRadius=(outer+inner)/2;
    for (int i=0;i<5;++i) {
      float angle=-90+i*72.f, a=angle*3.14159265f/180;
      auto col=sitMix(neutral,colors[i],laneEmphasis[i]);
      c.ringSegment(hubX,flowCenter,outer,inner,angle-32,angle+32,
        sitMix(theme.card,col,.10f+.10f*laneEmphasis[i]),sitFade(col,.65f));
      float x=hubX+std::cos(a)*glyphRadius, y=flowCenter+std::sin(a)*glyphRadius;
      c.glyph(icons[i],x-8,y-8,16,col);
      hit(situation,"sitHubLane",std::to_wstring(i),x-14,y-14,28,28);
    }
    c.circle(hubLeft,flowCenter,4,sitMix(neutral,theme.cpu,hubEmphasis));
    c.circle(hubRight,flowCenter,4,sitMix(neutral,theme.cpu,hubEmphasis));
    }
    for (int i = 0; i < 5; ++i) {
      float y = top + i * (laneH + cardGap);
      bool focused = situationLaneFocused(sitMode, i);
      auto col = focused ? colors[i] : sitFade(colors[i], .35f);
      focusCard(rightX,y,rightW,laneH,colors[i],laneEmphasis[i]);
      if (sitMode && focused) c.fill(rightX, y + 12, 3, laneH - 24, col, 1);
      double value = sitLaneValue(*s,i);
      if (compactFocus) value=!compactFocus->available ? -1 : i==0 ? compactFocus->cpu : i==1 ? compactFocus->gpu :
        i==2 ? compactFocus->bytes : i==3 ? compactFocus->disk : compactFocus->net;
      std::wstring shown = compactFocus && i==2 ? mem(value) : i >= 3 ? rate(value) : value >= 0 ? num(value, 1) + L"%" : L"—";
      float rowY=y+(!layout.compact && laneH>=58 ? 6.f : (laneH-24)/2);
      float valueW=bounded(std::ceil(c.textWidth(shown,layout.valueSize))+8.f,40.f,rightW-60);
      c.glyph(icons[i],rightX+8,rowY+4,16,col);
      c.text(labels[i],rightX+30,rowY+1,rightW-valueW-48,22,layout.nameSize,theme.ink,focused);
      c.text(shown,rightX+rightW-valueW-10,rowY,valueW,24,layout.valueSize,theme.ink,true,2);
      std::wstring note=i==2 ? mem(s->used)+L" / "+mem(s->total) : L"";
      // Keep selected-app values inside the card so no label cuts a curve.
      bool appNote=false;
      for (const auto &link:links) if (link.lane==i && !focusedKey.empty() && sitHoverLane<0 &&
                                      s->entities[link.entity].key==focusedKey) {
        note=sitName(s->entities[link.entity].name)+L" · "+
          (i==2 ? mem(link.value) : i>=3 ? rate(link.value) : num(link.value,1)+L"%");
        appNote=true; break;
      }
      if (!layout.compact && laneH>=58) {
        if (i==2 && !appNote) {
          c.fill(rightX+10,y+33,rightW-20,3,theme.track,1);
          if(value>=0) c.fill(rightX+10,y+33,(rightW-20)*float(bounded(value/100.,0.,1.)),3,col,1);
          c.text(note,rightX+10,y+laneH-23,rightW-20,20,layout.noteSize,theme.secondary);
        } else if (appNote) {
          // Keep the app name in its own card; here only its contribution matters.
          auto split=note.find(L" · ");
          if(split!=std::wstring::npos) note=note.substr(split+3);
          c.text(tr(L"选中 ",L"Selected ")+note,rightX+10,y+laneH-24,rightW-20,20,layout.noteSize,colors[i]);
        } else sitSpark(c,sitLaneSeries(i),rightX+12,y+laneH-22,rightW-24,13,col);
      }
      hit(situation, "sitLane", std::to_wstring(i), rightX, y, rightW, laneH);
    }
    for (int i = 0; i < int(s->entities.size()) && i < 6; ++i) {
      const auto &e = s->entities[i];
      float y = appTop + i * (cardH + cardGap);
      bool pinned = sitPinned == e.key;
      double primary=sitAppValue(e,sitMode);
      auto appColor=colors[sitMode==2 ? 1 : sitMode==3 ? 2 : sitMode==4 ? 3 : 0];
      bool highlighted = sitHoverLane >= 0 ? appFocus[i] : focusedKey == e.key;
      focusCard(leftX,y,leftW,cardH,appColor,pinned || highlighted ? 1.f : 0.f);
      if (pinned || highlighted) c.border(leftX, y, leftW, cardH, appColor, 10, 1.5f);
      std::wstring mainValue=memoryFlow ? mem(primary) : sitMode==4 ? rate(primary) : primary>=0 ? num(primary,1)+L"%" : L"—";
      float valueSize=layout.stackedApp ? 14.f : layout.valueSize;
      float valueW=bounded(std::ceil(c.textWidth(mainValue,valueSize))+8.f,40.f,110.f);
      if (layout.compact) {
        float row=y+(cardH-24)/2;
        sitBadge(c,e,leftX+10,row+4,16);
        c.text(sitName(e.name),leftX+34,row+1,leftW-valueW-52,22,layout.nameSize,theme.ink);
        c.text(mainValue,leftX+leftW-valueW-10,row,valueW,24,valueSize,highlighted ? appColor : theme.ink,true,2);
        if (pinned) c.glyph("pin",leftX+3,y+3,9,theme.cpu);
      } else {
        sitBadge(c,e,leftX+10,y+9,18);
        c.text(sitName(e.name),leftX+36,y+6,leftW-56-valueW,22,layout.nameSize,theme.ink);
        c.text(mainValue,leftX+leftW-valueW-10,y+6,valueW,22,valueSize,highlighted ? appColor : theme.ink,true,2);
        if (!e.available) c.text(tr(L"暂无数据",L"Unavailable"),leftX+12,y+cardH-25,leftW-24,20,layout.noteSize,theme.secondary);
        else {
          float sparkX=leftX+12;
          if(e.processes>1) {
            auto count=std::to_wstring(e.processes)+tr(L" 进程",L" proc");
            float countW=c.textWidth(count,layout.noteSize)+10;
            c.text(count,sparkX,y+cardH-25,countW,20,layout.noteSize,theme.secondary);
            sparkX+=countW;
          }
          sitSpark(c,sitSeries(e.key),sparkX,y+cardH-19,leftX+leftW-sparkX-(pinned ? 28 : 12),10,appColor);
        }
        if (pinned) c.glyph("pin",leftX+leftW-22,y+cardH-21,10,theme.cpu);
      }
      hit(situation, "sitEntity", e.key, leftX, y, leftW, cardH);
    }
    if (s->entities.empty()) c.text(tr(L"暂无应用数据", L"No app data"), leftX, top + 16, leftW, 28, 12, theme.secondary);
    if (layout.compact) return; // History remains intact and returns when the window expands.
    float timelineY=h-68, timelineW=w-2*pad;
    c.line(pad,timelineY-5,w-pad,timelineY-5,edge);
    const std::wstring metric=sitMode==2 ? L"GPU" : sitMode==3 ? tr(L"内存",L"Memory") : sitMode==4 ? tr(L"磁盘 + 网络",L"Disk + network") : L"CPU";
    c.text(metric,pad,timelineY,240,24,12.5f,theme.secondary);
    double age=sitHistory.latest() ? std::max(0.,sitHistory.latest()->when-s->when) : 0;
    if (sitFrozen && age>.1)
      c.text(L"−"+num(age,1)+L"s",w-pad-120,timelineY,120,24,12.5f,theme.secondary,false,2);
    float barsY=timelineY+20;
    double span=sitHistory.size()>1 ? sitHistory.latest()->when-sitHistory.samples.front().when : 0;
    std::vector<float> centers;
    for (size_t i=0;i<sitHistory.size();++i) {
      double fraction=span>0 ? (sitHistory.samples[i].when-sitHistory.samples.front().when)/span : 1;
      centers.push_back(pad+8+float(fraction)*(timelineW-16));
    }
    double maximum=sitMode==4 ? 1 : 100;
    if (sitMode==4) for (const auto &frame:sitHistory.samples) maximum=std::max(maximum,sitMetricValue(frame,sitMode));
    auto metricColor=colors[sitMode==2 ? 1 : sitMode==3 ? 2 : sitMode==4 ? 3 : 0];
    for (size_t i=0;i<sitHistory.size();++i) {
      double value=sitMetricValue(sitHistory.samples[i],sitMode);
      float x=centers[i];
      bool selected=i==(sitCursor<sitHistory.size() ? sitCursor : sitHistory.size()-1);
      float bh=value>=0 ? 3+23*float(bounded(value/maximum,0.,1.)) : 2;
      c.fill(x-7,barsY+29-bh,14,bh,sitFade(value>=0 ? metricColor : theme.secondary,.55f),1);
      if (selected) { c.line(x,barsY,x,barsY+34,theme.ink,1.5f); c.circle(x,barsY,2.5f,theme.ink); }
      float from=i ? (centers[i-1]+x)/2 : pad;
      float to=i+1<centers.size() ? (x+centers[i+1])/2 : w-pad;
      hit(situation,"sitTime",std::to_wstring(i),from,barsY,to-from,34);
    }
    c.text(L"−"+num(span,1)+L"s",pad,timelineY,120,24,12.5f,theme.secondary,false,2);
    if(!sitFrozen || age<=.1) c.text(sitFrozen ? tr(L"暂停",L"Paused") : tr(L"现在",L"Now"),w-pad-120,timelineY,120,24,12.5f,theme.secondary,false,2);
  }

  std::wstring topCpuName(const SitSample *s) {
    if (!s) return L"—";
    const SitEntity *best = nullptr;
    for (const auto &e : s->entities) if (e.cpu >= 0 && (!best || e.cpu > best->cpu)) best = &e;
    if (!best) return L"—";
    return sitName(best->name) + L" " + num(best->cpu) + L"%";
  }

  void drawSituationBadges(Canvas &c) {
    // Cards and the hub occlude badges just as in the full scene. The base
    // contains no moving badges, so restoring it cannot leave trails.
    for (int side=0; side<2; ++side) {
      c.rt->PushAxisAlignedClip(D2D1::RectF(sitPorts[side*2],0,sitPorts[side*2+1],
        c.height/situation.dpi),D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
      for (const auto &b:sitBadges) if (b.opacity>0 &&
          b.point.x+12>=sitPorts[side*2] && b.point.x-12<=sitPorts[side*2+1]) {
        sitBadge(c,b.entity,b.point.x-8,b.point.y-8,16,b.opacity);
        c.border(b.point.x-11,b.point.y-11,22,22,b.edge,6,1.2f);
      }
      c.rt->PopAxisAlignedClip();
    }
  }
  void draw(Window &win, bool animationFrame = false) {
    if (!win.hwnd)
      return;
    double start = seconds();
    RECT r;
    GetClientRect(win.hwnd, &r);
    auto &c = win.canvas;
    if (win.kind == 3 && !animationFrame) sitBaseDirty = true;
    if (c.width != r.right || c.height != r.bottom) {
      if (win.kind == 3) clearSituationBase();
      if (win.kind == 0) {
        release(staticBitmap);
        staticOwner = nullptr;
      }
    }
    if (!c.prepare(r.right, r.bottom))
      return;
    float scale = win.dpi * (win.kind == 0 ? layout.scale : 1.f), w = r.right / scale,
          h = r.bottom / scale;
    auto background = [&](Canvas &target) {
      auto bg = win.kind == 2 ? theme.card : theme.bg;
      bg.a = win.kind == 0 ? float(cfg.get<double>("opacity", .93)) : 1.f;
      target.fill(0, 0, w, h, bg, std::min(18.f, h / 2));
      auto edge = theme.ink;
      edge.a = .16f;
      target.border(.5, .5, w - 1, h - 1, edge, std::min(18.f, h / 2), .7);
    };
    if (win.kind == 0) {
      if (!rowsCached && seconds() - transition >= .25)
        dirty = true;
      if (dirty || !staticBitmap || staticOwner != c.rt) {
        rowsCached = !motion() || seconds() - transition >= .25;
        if (!staticCanvas.prepare(r.right, r.bottom))
          return;
        staticCanvas.begin(scale);
        background(staticCanvas);
        win.hits.clear();
        buildingBase = true;
        drawPanel(staticCanvas);
        buildingBase = false;
        staticHits = win.hits;
        HRESULT hr = staticCanvas.rt->EndDraw();
        if (FAILED(hr))
          return;
        if (staticOwner != c.rt) {
          release(staticBitmap);
          staticOwner = c.rt;
        }
        if (!staticBitmap) {
          auto props = D2D1::BitmapProperties(
              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
          c.rt->CreateBitmap(D2D1::SizeU(r.right, r.bottom), staticCanvas.pixels,
                             UINT32(r.right * 4), props, &staticBitmap);
        } else
          staticBitmap->CopyFromMemory(nullptr, staticCanvas.pixels, UINT32(r.right * 4));
      }
      fanMotion.step(seconds(), fanRPM(seconds()), snap.fans.reference,
                     motion() && !occluded && IsWindowVisible(panel.hwnd) && showsFan());
      double now = seconds(); bool moving = liveMotion();
      for (int i = 0; i < 4; ++i) powerMotion[i].step(now, powers[i].watts, moving && powerMoves(i));
      for (int i = 0; i < 5; ++i) activityMotion[i].step(now, activityValue(i), moving);
      c.begin(1);
      if (staticBitmap)
        c.rt->DrawBitmap(staticBitmap);
      c.rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
      win.hits = staticHits;
      drawPanel(c);
      // Float the view switch over the top-right corner only while hovered.
      c.rt->SetTransform(D2D1::Matrix3x2F::Scale(win.dpi,win.dpi));
      float panelWidth=r.right/win.dpi;
      if (win.hover == "switchView" || win.pressed == "switchView")
        c.glyph("expand",panelWidth-28,10,16,theme.secondary);
      hit(panel,"switchView",L"",(panelWidth-34)/layout.scale,4/layout.scale,28/layout.scale,28/layout.scale);
      c.rt->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale));
      dirty = false;
    } else if (win.kind == 3) {
      const bool rebuild = sitBaseDirty || !sitBase || sitFocusMoving;
      c.begin(scale);
      if (rebuild) {
        background(c);
        drawSituation(c);
        // One bounded bitmap, owned by this canvas; refresh only on a new
        // reading, interaction, resize, or while focus is easing.
        if (FAILED(c.rt->EndDraw())) { clearSituationBase(); c.clear(); return; }
        HRESULT hr;
        if (!sitBase) {
          auto props=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
            D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
          hr=c.rt->CreateBitmap(D2D1::SizeU(r.right,r.bottom),c.pixels,
            UINT32(r.right*4),props,&sitBase);
        } else hr=sitBase->CopyFromMemory(nullptr,c.pixels,UINT32(r.right*4));
        sitBaseDirty=FAILED(hr);
        c.rt->BeginDraw(); // Preserve the freshly rendered pixels for this frame.
      } else {
        c.rt->SetTransform(D2D1::Matrix3x2F::Identity());
        c.rt->DrawBitmap(sitBase);
        c.rt->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale));
        drawSituation(c,true);
      }
      drawSituationBadges(c);
    } else {
      c.begin(scale);
      background(c);
      win.hits.clear();
      if (win.kind == 1)
        drawSettings(c);
      else
        drawDetail(c);
    }
    if (win.kind != 2) drawControlFeedback(c, win, w, h);
    if (!c.end(win.hwnd) && win.kind == 3) clearSituationBase();
    if (win.kind == 0) {
      framesMs += (seconds() - start) * 1000;
      ++frames;
    }
  }
  void placePopup(Window &win, POINT anchor, int width, int height) {
    win.dpi = panel.dpi;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST), &mi);
    int w = int(width * win.dpi),
        h = std::min(int(height * win.dpi), int(mi.rcWork.bottom - mi.rcWork.top - 12));
    int x = bounded(int(anchor.x), int(mi.rcWork.left + 6), int(mi.rcWork.right - w - 6)),
        y = bounded(int(anchor.y), int(mi.rcWork.top + 6), int(mi.rcWork.bottom - h - 6));
    SetWindowPos(win.hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    roundWindow(win.hwnd, win.dpi);
    draw(win);
    ShowWindow(win.hwnd, SW_SHOWNOACTIVATE);
  }
  void showSettings(POINT anchor) {
    closeDetail();
    startupCheckedAt = -10;
    more = false;
    scroll = 0;
    placePopup(settings, anchor, 300, SettingsBase);
    SetForegroundWindow(settings.hwnd);
  }
  void closeDetail() {
    ShowWindow(detail.hwnd, SW_HIDE);
    detailKey.clear();
    detailMetric.clear();
    outsideSince = 0;
  }
  void showDetail(const Hit &h) {
    detailKey = h.id == "process" ? h.key : L"";
    detailMetric = h.id == "metric" ? utf8(h.key) : "";
    RECT r;
    GetWindowRect(panel.hwnd, &r);
    POINT pt{r.left + LONG(h.r.left * layout.scale * panel.dpi),
             r.top + LONG(h.r.bottom * layout.scale * panel.dpi + 7)};
    placePopup(detail, pt, 320, int(24 + details().size() * 22));
    outsideSince = 0;
  }
  void checkPointer(double now) {
    POINT p;
    GetCursorPos(&p);
    RECT pr;
    GetWindowRect(panel.hwnd, &pr);
    float x = (p.x - pr.left) / (panel.dpi * layout.scale),
          y = (p.y - pr.top) / (panel.dpi * layout.scale);
    bool overPanel = PtInRect(&pr, p) && IsWindowVisible(panel.hwnd);
    HWND under = WindowFromPoint(p);
    overPanel = overPanel && under == panel.hwnd;
    Hit *h = overPanel ? at(panel, x, y) : nullptr;
    std::wstring current = h && h->id == "process" ? h->key : L"";
    if (hoverKey != current) {
      hoverKey = current;
      hoverAt = now;
      dirty = true;
    }
    if (IsWindowVisible(detail.hwnd)) {
      RECT dr;
      GetWindowRect(detail.hwnd, &dr);
      bool same = h && ((h->id == "process" && h->key == detailKey) ||
                        (h->id == "metric" && utf8(h->key) == detailMetric));
      bool inside = PtInRect(&dr, p) && under == detail.hwnd;
      if (same || inside)
        outsideSince = 0;
      else if (outsideSince == 0)
        outsideSince = now;
      else if (now - outsideSince > .12)
        closeDetail();
    } else if (h && !resizing && !IsWindowVisible(settings.hwnd) &&
               (h->id == "process" || h->id == "metric") && now - hoverAt > .55) {
      showDetail(*h);
      hoverAt = now;
    }
  }
  bool autostartEnabled() {
    if (seconds() - startupCheckedAt > 5) {
      startupOn = startup::enabled();
      startupCheckedAt = seconds();
    }
    return startupOn;
  }
  void toggleAutostart() {
    const HRESULT hr = startup::change(settings.hwnd, !startup::enabled(), cfg.path);
    startupCheckedAt = -10;
    if (FAILED(hr)) {
      wchar_t code[32];
      swprintf(code, 32, L" (0x%08lX)", static_cast<unsigned long>(hr));
      const auto text = tr(L"未能完成开机启动设置。", L"Could not change start at login.") + std::wstring(code);
      MessageBoxW(settings.hwnd, text.c_str(), L"top", MB_OK | MB_ICONERROR);
    }
  }
  template <class T> void cycle(const std::string &key, const std::vector<T> &values) {
    T v = cfg.get<T>(key.c_str(), values.front());
    auto p = std::find(values.begin(), values.end(), v);
    cfg.data[key] = p == values.end() || ++p == values.end() ? values.front() : *p;
  }
  void action(std::string id) {
    if (id == "switchView") {
      if (overviewMode) showPanel(); else showSituation();
      return;
    }
    if (id == "settings") {
      RECT r;
      GetWindowRect(situationVisible() ? situation.hwnd : panel.hwnd, &r);
      showSettings({r.right - int(300 * panel.dpi), r.top + int(52 * panel.dpi)});
      return;
    }
    if (id == "quit") {
      DestroyWindow(panel.hwnd);
      return;
    }
    if (id == "togglePanel") {
      if (IsWindowVisible(panel.hwnd)) action("hide");
      else showPanel();
      return;
    }
    if (id == "hide") {
      if (situationVisible()) hideSituation();
      ShowWindow(panel.hwnd, SW_HIDE);
      ShowWindow(settings.hwnd, SW_HIDE);
      closeDetail();
      sampler.settings(false, cfg.get<double>("interval", 1), cfg);
      return;
    }
    if (id == "more") {
      more = !more;
      scroll = 0;
      RECT r;
      GetWindowRect(settings.hwnd, &r);
      placePopup(settings, {r.left, r.top}, 300, more ? SettingsFull : SettingsBase);
      return;
    }
    if (id == "situation") {
      ShowWindow(settings.hwnd, SW_HIDE);
      showSituation();
      return;
    }
    if (id == "powerSetup") {
      ShowWindow(settings.hwnd, SW_HIDE);
      PowerSetup setup{cfg, snap};
      if (setup.show(panel.hwnd, instance)) { applyConfig(); draw(panel); }
      return;
    }
    if (id == "openConfig") {
      ShellExecuteW(settings.hwnd, L"open", cfg.path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      return;
    }
    if (id == "autostart") {
      toggleAutostart();
      draw(settings);
      return;
    }
    if (id == "palette") {
      HMENU menu = CreatePopupMenu();
      const std::vector<std::string> palettes{"sea", "graphite", "warm"};
      const std::wstring names[] = {tr(L"海", L"Sea"), tr(L"石墨", L"Graphite"), tr(L"暖", L"Warm")};
      for (size_t i = 0; i < palettes.size(); ++i)
        AppendMenuW(menu, MF_STRING | (cfg.get<std::string>("palette", "sea") == palettes[i] ? MF_CHECKED : 0),
                    UINT_PTR(i + 1), names[i].c_str());
      POINT point{};
      for (const auto &h : settings.hits) if (h.id == "palette")
        point = {int(h.r.left * settings.dpi), int(h.r.bottom * settings.dpi)};
      ClientToScreen(settings.hwnd, &point);
      int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, settings.hwnd, nullptr);
      DestroyMenu(menu);
      if (selected > 0 && selected <= int(palettes.size())) action(palettes[selected - 1]);
      return;
    }
    if (id == "menuBarShows") {
      HMENU menu = CreatePopupMenu();
      const std::vector<std::string> metrics{"cpu", "memory", "gpu", "download", "upload", "power"};
      for (size_t i = 0; i < metrics.size(); ++i) {
        Config choice = cfg;
        choice.data["menuBarShows"] = metrics[i];
        auto label = trayReading(choice, snap).label;
        if (metrics[i] == "memory") label += L" (%)";
        AppendMenuW(menu, MF_STRING | (cfg.trayMetric() == metrics[i] ? MF_CHECKED : 0), UINT_PTR(i + 1), label.c_str());
      }
      POINT point{};
      GetCursorPos(&point);
      int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, settings.hwnd, nullptr);
      DestroyMenu(menu);
      if (selected > 0 && selected <= int(metrics.size())) {
        cfg.load();
        cfg.data["menuBarShows"] = metrics[selected - 1];
        cfg.save();
        applyConfig();
        draw(settings);
      }
      return;
    }
    cfg.load();
    if (id == "pin")
      cfg.data["floating"] = !cfg.get<bool>("floating", false);
    else if (id == "tray")
      cfg.data["menuBar"] = !cfg.get<bool>("menuBar", true);
    else if (id == "motion")
      cfg.data["motion"] = !cfg.get<bool>("motion", true);
    else if (id == "estimatePower")
      cfg.data["estimatePower"] = !cfg.get<bool>("estimatePower", false);
    else if (id == "sea" || id == "graphite" || id == "warm")
      cfg.data["palette"] = id;
    else if (id == "sortCpu" || id == "sortMem")
      cfg.data["processSort"] = id == "sortCpu" ? "cpu" : "memory";
    else if (id == "interval")
      cycle<double>(id, {.5, 1, 2, 5, 10});
    else if (id == "maxProcesses")
      cycle<int>(id, {3, 5, 7, 9, 12, 16, 24});
    else if (id == "processSort")
      cycle<std::string>(id, {"cpu", "memory"});
    else if (id == "language")
      cycle<std::string>(id, {"auto", "zh", "en"});
    else if (id == "appearance")
      cycle<std::string>(id, {"auto", "dark", "light"});
    else {
      std::string key = id == "gpu" || id == "power" ? "hidden" : "modules";
      auto list = cfg.data[key];
      bool removed = false;
      json out = json::array();
      for (auto &v : list)
        if (v == id)
          removed = true;
        else
          out.push_back(v);
      if (!removed)
        out.push_back(id);
      cfg.data[key] = out;
    }
    cfg.save();
    applyConfig();
    rank(seconds());
    if (IsWindowVisible(settings.hwnd))
      draw(settings);
  }
  void inspect(const std::wstring &path) {
    PROCESS_MEMORY_COUNTERS_EX m{};
    m.cb = sizeof(m);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&m),
                         sizeof(m));
    json j = {{"version", "native-0.1"},
              {"pid", GetCurrentProcessId()},
              {"visible", IsWindowVisible(panel.hwnd) != 0},
              {"detailVisible", IsWindowVisible(detail.hwnd) != 0},
              {"detailMetric", detailMetric},
              {"fans", snap.fans.report()},
              {"fanVisualRPM", fanRPM(seconds())},
              {"fanAnimation", {{"angle", fanMotion.angle}, {"turnsPerSecond", fanMotion.speed},
                                {"colorRatio", fanMotion.colorRatio}}},
              {"sampleSequence", snap.seq},
              {"topmost", (GetWindowLongPtrW(panel.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0},
              {"covered", fullyCovered()},
              {"trayVisible", trayAdded},
              {"autostartEnabled", autostartEnabled()},
              {"settingsVisible", IsWindowVisible(settings.hwnd) != 0},
              {"situationVisible", situationVisible()},
              {"situationSamples", sitHistory.size()},
              {"situationMode", sitMode},
              {"situationFrozen", sitFrozen},
              {"situationPinned", utf8(sitPinned)},
              {"more", more},
              {"workingSetMB", m.WorkingSetSize / 1048576.},
              {"privateMB", m.PrivateUsage / 1048576.},
              {"cpu", snap.cpu},
              {"trayMetric", cfg.trayMetric()},
              {"trayText", utf8(trayReading(cfg, snap).text)},
              {"trayTip", utf8(lastTrayTip)},
              {"samplerDemand", InterlockedCompareExchange(&sampler.mode, 0, 0)},
              {"cpuScope", "whole_machine_time"},
              {"cpuPackageWatts", snap.cpuPower},
              {"memoryWatts", snap.memoryPower}, {"systemWatts", snap.systemPower},
              {"memoryPowerState", snap.memoryPowerState}, {"memoryPowerError", snap.memoryPowerError},
              {"diskBusy", snap.diskBusy}, {"diskRead", snap.diskRead}, {"diskWrite", snap.diskWrite},
              {"cpuPackageCelsius", snap.cpuTemp},
              {"sensorState", snap.sensorState},
              {"sensorError", snap.sensorError},
              {"sensorMs", snap.sensorMs},
              {"memoryUsed", snap.used},
              {"memoryTotal", snap.total}, {"pagefileUsed", snap.swap},
              {"memoryReadBytesPerSec", snap.memoryRead}, {"memoryWriteBytesPerSec", snap.memoryWrite},
              {"memoryBandwidthState", snap.memoryBandwidthState}, {"memoryBandwidthError", snap.memoryBandwidthError},
              {"networkDown", snap.down},
              {"networkUp", snap.up},
              {"gpu", snap.gpu},
              {"gpuMemory", snap.gpuMem}, {"gpuTotal", snap.gpuTotal},
              {"gpuReserved", snap.gpuReserved},
              {"gpuCelsius", snap.gpuTemp}, {"gpuWatts", snap.gpuPower}, {"gpuGHz", snap.gpuClock},
              {"gpuMaxGHz", snap.gpuMaxClock},
              {"processGroups", snap.groups.size()},
              {"cores", snap.cores},
              {"ghz", snap.ghz},
              {"samplerMs", snap.cost},
              {"averageDrawMs", frames ? framesMs / frames : 0},
              {"layout",
               {{"tier", layout.c.tier},
                {"columns", layout.c.cols},
                {"count", layout.c.count},
                {"wide", layout.c.wide},
                {"scale", layout.scale}}}};
    for (Window *w : {&panel, &settings, &detail, &situation}) {
      std::string name = w->kind == 0 ? "panel" : w->kind == 1 ? "settings"
                         : w->kind == 2 ? "detail" : "situation";
      RECT r;
      GetWindowRect(w->hwnd, &r);
      j[name] = {{"rect", {r.left, r.top, r.right, r.bottom}}, {"dpi", w->dpi}};
      for (auto &h : w->hits)
        j[name]["hits"].push_back({{"id", h.id},
                                   {"key", utf8(h.key)},
                                   {"rect", {h.r.left, h.r.top, h.r.right, h.r.bottom}}});
    }
    for (const auto &b : layout.blocks) {
      j["layout"]["blocks"].push_back({b.kind, b.x, b.y, b.w, b.h});
      if (b.kind == "cpu") {
        auto chart = cpuChart(int(bars.size()), chartArea(b));
        j["layout"]["cpuMode"] = chart.grid ? "grid" : chart.rows == 1 ? "bars" : "twoRows";
      }
    }
    j["detailLines"] = json::array();
    if (IsWindowVisible(detail.hwnd)) for (const auto &line : details()) j["detailLines"].push_back(utf8(line));
    j["situationWindow"] = j["situation"];
    j["situation"] = nullptr;
    if (auto *shown = sitShown()) {
      j["situation"] = {{"when", shown->when}, {"cpu", shown->cpu}, {"mem", shown->mem},
                        {"gpu", shown->gpu}, {"disk", shown->disk}, {"net", shown->net},
                        {"used", shown->used}, {"total", shown->total}, {"cores", shown->cores},
                        {"gpuMem", shown->gpuMem}, {"gpuTotal", shown->gpuTotal},
                        {"processCount",shown->processCount},{"appCount",shown->appCount},{"entities", json::array()}};
      for (const auto &e : shown->entities)
        j["situation"]["entities"].push_back({{"key", utf8(e.key)}, {"cpu", e.cpu}, {"bytes", e.bytes},
          {"name", utf8(e.name)}, {"processes", e.processes}, {"available", e.available},
          {"gpu",e.gpu},{"disk",e.disk},{"net",e.net}});
    }
    j["appIo"] = snap.appIoState;
    j["appResources"] = json::array();
    for(const auto &g:snap.groups) if(g.gpu>0||g.disk>0||g.net>0)
      j["appResources"].push_back({{"name",utf8(g.name)},{"gpu",g.gpu},{"disk",g.disk},{"net",g.net}});
    j["estimatePower"] = cfg.get<bool>("estimatePower", false);
    j["powerHardware"] = snap.powerHardware;
    j["discreteGpuCount"] = snap.discreteGpuCount;
    j["displayPower"] = json::array();
    for (int i = 0; i < 4; ++i) j["displayPower"].push_back({{"watts", powers[i].watts}, {"estimated", powers[i].estimated},
        {"model", powers[i].model}, {"text", utf8(power::label(powers[i], powers[i].watts))}, {"phase", powerMotion[i].phase}});
    j["memoryModules"] = json::array();
    for (const auto &m : snap.memoryModules) j["memoryModules"].push_back({{"part", m.part}, {"gib", m.gib}, {"speed", m.speed}, {"type", m.type}});
    j["platformHardware"] = {{"kind", snap.platform.kind}, {"ssd", snap.platform.ssd}, {"hdd", snap.platform.hdd},
        {"otherDisks", snap.platform.otherDisks}, {"disksKnown", snap.platform.disksKnown}, {"brightness", snap.platform.brightness}};
    auto base = power::automaticBaseline(snap, powers[2].watts);
    j["automaticBaseline"] = {{"watts", base.watts}, {"board", base.board}, {"display", base.display},
        {"storage", base.storage}, {"fans", base.fans}, {"conversion", base.conversion},
        {"assumedBrightness", base.assumedBrightness}, {"assumedDisks", base.assumedDisks},
        {"assumedFans", base.assumedFans}, {"assumedActivity", base.assumedActivity}};
    writeFile(path, j.dump(2));
  }
  void captureWindow(Window &win, const std::wstring &path) {
    auto &c = win.canvas;
    draw(win);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + c.width * c.height * 4;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = c.width;
    info.biHeight = -c.height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    HANDLE out = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out != INVALID_HANDLE_VALUE) {
      DWORD n;
      WriteFile(out, &file, sizeof(file), &n, nullptr);
      WriteFile(out, &info, sizeof(info), &n, nullptr);
      WriteFile(out, c.pixels, c.width * c.height * 4, &n, nullptr);
      CloseHandle(out);
    }
  }
  void capture(const std::wstring &path) { captureWindow(panel, path); }
  void captureSituation(const std::wstring &path) {
    if (sitHistory.samples.empty()) seedSituation();
    if (!IsWindowVisible(situation.hwnd)) showSituation();
    draw(situation);
    captureWindow(situation, path);
  }
  LRESULT event(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto &win = window(hwnd);
    if (msg == taskbarCreated) {
      trayAdded = false;
      lastTray.clear();
      updateTray();
      return 0;
    }
    switch (msg) {
    case WM_NCCALCSIZE:
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      EndPaint(hwnd, &ps);
      if (panel.hwnd)
        draw(win);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      auto p = reinterpret_cast<MINMAXINFO *>(lp);
      p->ptMinTrackSize = win.kind == 3 ? POINT{LONG(SituationLayout::MinWidth * win.dpi), LONG(SituationLayout::MinHeight * win.dpi)} : POINT{50, 20};
      return 0;
    }
    case WM_SIZE:
      lastEnv = 0;
      if (hwnd == situation.hwnd) {
        // WM_DPICHANGED owns DPI; resizing inside that handler must retain it.
        if (situationVisible() && !IsIconic(situation.hwnd)) draw(situation);
      } else if (hwnd == panel.hwnd) {
        relayout(false);
        roundWindow(hwnd, panel.dpi);
        if (!resizing)
          dirty = true;
        else
          draw(panel);
      }
      return 0;
    case WM_DPICHANGED: {
      win.dpi = HIWORD(wp) / 96.f;
      auto r = reinterpret_cast<RECT *>(lp);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      if (hwnd == panel.hwnd) {
        relayout(false);
        applyConfig();
      } else if (hwnd == situation.hwnd && IsWindowVisible(situation.hwnd)) {
        draw(situation);
      }
      return 0;
    }
    case WM_NCHITTEST:
      if (hwnd == situation.hwnd) {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &point);
        auto hitNow = at(situation, point.x / situation.dpi, point.y / situation.dpi);
        if (hitNow) return HTCLIENT;
        RECT r; GetClientRect(hwnd, &r);
        int edge = int(6 * situation.dpi);
        bool left = point.x < edge, right = point.x >= r.right - edge;
        bool top = point.y < edge, bottom = point.y >= r.bottom - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        return point.y / situation.dpi < 60 ? HTCAPTION : HTCLIENT;
      }
      if (hwnd == panel.hwnd) {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        RECT r;
        GetClientRect(hwnd, &r);
        auto h = at(panel, p.x / (panel.dpi * layout.scale), p.y / (panel.dpi * layout.scale));
        int edge = int(5 * panel.dpi);
        bool left = p.x < edge, right = p.x >= r.right - edge, top = p.y < edge,
             bottom = p.y >= r.bottom - edge;
        if (top && left)
          return HTTOPLEFT;
        if (top && right)
          return HTTOPRIGHT;
        if (bottom && left)
          return HTBOTTOMLEFT;
        if (bottom && right)
          return HTBOTTOMRIGHT;
        if (left)
          return HTLEFT;
        if (right)
          return HTRIGHT;
        if (top)
          return HTTOP;
        if (bottom)
          return HTBOTTOM;
        return h ? HTCLIENT : HTCAPTION;
      }
      return HTCLIENT;
    case WM_ENTERSIZEMOVE:
      resizing = true;
      closeDetail();
      ShowWindow(settings.hwnd, SW_HIDE);
      backdrop(hwnd, false, theme.dark);
      KillTimer(panel.hwnd, 2);
      return 0;
    case WM_EXITSIZEMOVE:
      resizing = false;
      if (hwnd == situation.hwnd) {
        saveSituationGeometry();
        roundWindow(hwnd, situation.dpi);
        draw(situation);
        tick();
        return 0;
      }
      resizing = false;
      relayout(true);
      roundWindow(hwnd, panel.dpi);
      backdrop(hwnd, true, theme.dark);
      saveGeometry();
      tick();
      return 0;
    case WM_MOUSEMOVE: {
      float factor = win.dpi * (win.kind == 0 ? layout.scale : 1);
      float x = GET_X_LPARAM(lp) / factor, y = GET_Y_LPARAM(lp) / factor;
      auto target = at(win, x, y);
      std::string control = target ? target->id : "";
      std::wstring item = target ? target->key : L"";
      if (control != win.hover || item != win.hoverItem) {
        win.hover = control; win.hoverItem = item;
        win.tipReady = false;
        KillTimer(hwnd, 4);
        if (target && !controlTip(*target).empty()) SetTimer(hwnd, 4, 500, nullptr);
        win.focus = -1;
        draw(win);
      }
      if (abs(x - pointerX) + abs(y - pointerY) > 2) {
        pointerX = x;
        pointerY = y;
        if (hoverKey.empty())
          hoverAt = seconds();
      }
      if (win.kind == 3) {
        auto h = at(win, x, y);
        std::wstring key = h && h->id == "sitEntity" ? h->key : L"";
        int lane = h && (h->id == "sitLane" || h->id == "sitHubLane") ? std::stoi(h->key) : -1;
        if (key != sitHover || lane != sitHoverLane) {
          sitHover = key; sitHoverLane = lane;
          draw(situation);
        }
      }
      TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
      TrackMouseEvent(&t);
      return 0;
    }
    case WM_MOUSELEAVE:
      win.hover.clear(); win.hoverItem.clear();
      win.tipReady = false;
      KillTimer(hwnd, 4);
      if (hwnd == situation.hwnd && (!sitHover.empty() || sitHoverLane >= 0)) {
        sitHover.clear(); sitHoverLane = -1;
        draw(situation);
      }
      dirty = true;
      if (win.kind != 0) draw(win);
      return 0;
    case WM_LBUTTONDOWN: {
      float factor = win.dpi * (win.kind == 0 ? layout.scale : 1);
      auto h = at(win, GET_X_LPARAM(lp) / factor, GET_Y_LPARAM(lp) / factor);
      win.pressed = h ? h->id : "";
      win.pressedItem = h ? h->key : L"";
      win.focus = -1;
      if (h) SetCapture(hwnd);
      draw(win);
      return 0;
    }
    case WM_LBUTTONUP: {
      float factor = win.dpi * (win.kind == 0 ? layout.scale : 1);
      auto target = at(win, GET_X_LPARAM(lp) / factor, GET_Y_LPARAM(lp) / factor);
      Hit clicked = target ? *target : Hit{};
      bool confirmed = target && (win.kind == 0 || (win.pressed == clicked.id && win.pressedItem == clicked.key));
      win.pressed.clear(); win.pressedItem.clear();
      if (GetCapture() == hwnd) ReleaseCapture();
      if (!confirmed) { draw(win); return 0; }
      const Hit *h = &clicked;
      if (win.kind == 3 && h) {
        if (h->id == "switchView") action("switchView");
        else if (h->id == "sitFreeze") {
          sitFrozen = !sitFrozen;
          sitCursor = sitFrozen ? (sitHistory.samples.empty() ? (size_t)-1 : sitHistory.samples.size() - 1) : (size_t)-1;
          if (!sitFrozen) updateSituation(seconds());
          draw(situation);
        } else if (h->id == "sitTab") {
          sitMode = std::stoi(utf8(h->key));
          draw(situation);
        } else if (h->id == "sitLane" || h->id == "sitHubLane") {
          int lane = std::min(4, std::stoi(utf8(h->key)) + 1);
          sitMode = sitMode == lane ? 0 : lane;
          draw(situation);
        } else if (h->id == "sitEntity") {
          sitPinned = sitPinned == h->key ? L"" : h->key;
          if (auto s = sitShown())
            for (const auto &e : s->entities) if (e.key == sitPinned) sitPinnedEntity = e;
          sitKeys.clear();
          draw(situation);
        } else if (h->id == "sitTime") {
          size_t i = size_t(std::stoul(utf8(h->key)));
          if (i < sitHistory.samples.size()) {
            sitCursor = i;
            sitFrozen = true;
            draw(situation);
          }
        } else if (h->id == "settings") action("settings");
        return 0;
      }
      if (h) {
        Hit copy = *h;
        if (copy.id == "process" || copy.id == "metric")
          showDetail(copy);
        else
          action(copy.id);
      }
      return 0;
    }
    case WM_CONTEXTMENU:
      return 0;
    case WM_MOUSEWHEEL:
      if (hwnd == settings.hwnd && more) {
        RECT r;
        GetClientRect(hwnd, &r);
        int maxScroll = std::max(0, SettingsFull - int(r.bottom / settings.dpi));
        scroll = bounded(scroll - GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 34, 0, maxScroll);
        draw(settings);
      }
      return 0;
    case WM_ACTIVATE:
      if (hwnd == settings.hwnd && LOWORD(wp) == WA_INACTIVE)
        ShowWindow(settings.hwnd, SW_HIDE);
      return 0;
    case WM_KEYDOWN:
      if (win.kind != 2 && wp == VK_TAB && !win.hits.empty()) {
        int step = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
        win.focus = win.focus < 0 ? (step > 0 ? 0 : int(win.hits.size()) - 1)
                                 : (win.focus + step + int(win.hits.size())) % int(win.hits.size());
        draw(win);
        return 0;
      }
      if (win.kind != 2 && wp == VK_RETURN && win.focus >= 0 && win.focus < int(win.hits.size())) {
        const Hit selected = win.hits[win.focus];
        float factor=win.dpi*(win.kind==0 ? layout.scale : 1);
        LPARAM xy = MAKELPARAM(int((selected.r.left + selected.r.right) * .5f * factor),
                               int((selected.r.top + selected.r.bottom) * .5f * factor));
        event(hwnd, WM_LBUTTONDOWN, 0, xy);
        event(hwnd, WM_LBUTTONUP, 0, xy);
        // Mouse activation clears focus. Keyboard activation must preserve its place.
        for (size_t i=0;i<win.hits.size();++i)
          if (win.hits[i].id==selected.id && win.hits[i].key==selected.key) { win.focus=int(i); break; }
        if (IsWindowVisible(hwnd)) draw(win);
        return 0;
      }
      if (hwnd == situation.hwnd) {
        if (wp == VK_ESCAPE) hideSituation();
        else if (wp == VK_UP || wp == VK_DOWN) {
          std::vector<int> applications;
          for(size_t i=0;i<win.hits.size();++i) if(win.hits[i].id=="sitEntity") applications.push_back(int(i));
          if(!applications.empty()) {
            auto current=std::find(applications.begin(),applications.end(),win.focus);
            int index=current==applications.end() ? (wp==VK_DOWN ? 0 : int(applications.size())-1)
              : bounded(int(current-applications.begin())+(wp==VK_DOWN ? 1 : -1),0,int(applications.size())-1);
            win.focus=applications[index];
            draw(situation);
          }
        }
        else if ((wp == VK_LEFT || wp == VK_RIGHT) && !sitHistory.samples.empty()) {
          size_t index = sitCursor < sitHistory.size() ? sitCursor : sitHistory.size() - 1;
          sitCursor = wp == VK_LEFT ? (index ? index - 1 : 0) : std::min(index + 1, sitHistory.size() - 1);
          sitFrozen = true;
          draw(situation);
        }
        else if (wp == VK_SPACE) {
          sitFrozen = !sitFrozen;
          sitCursor = sitFrozen ? (sitHistory.samples.empty() ? (size_t)-1 : sitHistory.samples.size() - 1) : (size_t)-1;
          if (!sitFrozen) updateSituation(seconds());
          draw(situation);
        }
        return 0;
      }
      if (wp == VK_ESCAPE) {
        if (IsWindowVisible(detail.hwnd))
          closeDetail();
        else if (IsWindowVisible(settings.hwnd))
          ShowWindow(settings.hwnd, SW_HIDE);
        else
          action("hide");
      }
      return 0;
    case WM_TIMER:
      if (wp == 4) {
        KillTimer(hwnd, 4);
        win.tipReady = !win.hover.empty();
        if (IsWindowVisible(hwnd)) draw(win);
        return 0;
      }
      if (hwnd == panel.hwnd) {
        if (wp == 1 && !resizing)
          tick();
        if (wp == 2 && !resizing && IsWindowVisible(panel.hwnd) && !occluded)
          draw(panel);
      } else if (hwnd == situation.hwnd && wp == 3 && situationActive() &&
                 !sitFrozen) {
        draw(situation,true);
      }
      return 0;
    case TRAY_MESSAGE:
      if (lp == WM_LBUTTONUP) {
        if (situationVisible()) hideSituation();
        else if (IsWindowVisible(panel.hwnd) && !coveredByOtherWindow(false))
          action("hide");
        else if (overviewMode) showSituation();
        else showPanel();
      } else if (lp == WM_RBUTTONUP) {
        POINT p;
        GetCursorPos(&p);
        showSettings(p);
      }
      return 0;
    case WM_COPYDATA: {
      auto data = reinterpret_cast<COPYDATASTRUCT *>(lp);
      if (!data || data->cbData > 65536 || data->cbData < 2)
        return FALSE;
      std::wstring command(static_cast<wchar_t *>(data->lpData),
                           data->cbData / sizeof(wchar_t) - 1);
      if (command == L"quit")
        PostMessageW(panel.hwnd, WM_CLOSE, 0, 0);
      else if (command == L"hide")
        action("hide");
      else if (command == L"toggle") {
        if (IsWindowVisible(panel.hwnd))
          action("hide");
        else showPanel();
      } else if (command == L"settings") {
        RECT r;
        GetWindowRect(panel.hwnd, &r);
        showSettings({r.left, r.bottom + 6});
      } else if (command == L"situation")
        showSituation();
      else if (command.rfind(L"captureSit:", 0) == 0)
        captureSituation(command.substr(11));
      else if (command.rfind(L"inspect:", 0) == 0)
        inspect(command.substr(8));
      else if (command.rfind(L"capture:", 0) == 0)
        capture(command.substr(8));
      else showPanel();
      return TRUE;
    }
    case WM_CLOSE:
      if (hwnd == panel.hwnd)
        DestroyWindow(hwnd);
      else if (hwnd == situation.hwnd)
        hideSituation();
      else
        ShowWindow(hwnd, SW_HIDE);
      return 0;
    case WM_DESTROY:
      if (hwnd == panel.hwnd) {
        quitting = true;
        KillTimer(hwnd, 1);
        KillTimer(hwnd, 2);
        if (IsWindowVisible(situation.hwnd)) saveSituationGeometry();
        if (trayAdded)
          Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
      }
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
};
int selfTest() {
  Config c;
  {
    Config cfg; Snapshot s;
    s.memoryModules = {{"M471A1K43DB1-CWE", 8, 2933, 26}, {"M471A1K43DB1-CWE", 8, 2933, 26}};
    s.cpuPower = 12; s.gpuPower = 4; s.discreteGpuCount = 1; s.powerHardware = "fixture";
    assert(power::resolve(cfg, s)[2].watts == -1);
    cfg.data["estimatePower"] = true;
    auto reference = power::resolve(cfg, s);
    assert(reference[2].estimated && reference[2].watts > 1 && reference[2].watts < 2);
    assert(reference[3].watts == -1);
    s.used = 1; s.total = 16;
    assert(power::resolve(cfg, s)[2].watts == reference[2].watts);
    s.used = 15; s.cpu = 100;
    assert(power::resolve(cfg, s)[2].watts == reference[2].watts);
    cfg.data["platformBaseWatts"] = 10; cfg.data["platformBaseSource"] = "test reference";
    cfg.data["powerProfileHardware"] = "fixture";
    auto sum = power::resolve(cfg, s);
    assert(sum[3].estimated && std::abs(sum[3].watts - (26 + reference[2].watts)) < 1e-9);
    s.gpuPower = -1; assert(power::resolve(cfg, s)[3].watts == -1); s.gpuPower = 4;
    s.discreteGpuCount = 2; assert(power::resolve(cfg, s)[3].watts == -1); s.discreteGpuCount = 1;
    cfg.data["platformBaseSource"] = " "; assert(power::resolve(cfg, s)[3].watts == -1);
    cfg.data["platformBaseSource"] = "test reference";
    s.powerHardware = "other-machine"; assert(power::resolve(cfg, s)[3].watts == -1); s.powerHardware = "fixture";
    s.memoryModules[1].part = "unsupported"; assert(power::resolve(cfg, s)[2].watts == -1);
    cfg.data["memoryReferenceWatts"] = 3; cfg.data["memoryReferenceSource"] = "test reference";
    assert(power::resolve(cfg, s)[2].watts == 3);
    s.memoryPower = 0; s.systemPower = 30;
    auto measured = power::resolve(cfg, s);
    assert(!measured[2].estimated && measured[2].watts == 0 && !measured[3].estimated && measured[3].watts == 30);
    cfg.data["estimatePower"] = false;
    assert(power::resolve(cfg, s)[3].watts == 30);
    s.memoryPowerState = "ready";
    assert(power::resolve(cfg, s)[2].model == "intel-dram-rapl");
    assert(!power::resolve(cfg, s)[2].estimated);
    s.memoryPower = s.systemPower = -1;
    assert(power::resolve(cfg, s)[2].watts == -1 && power::resolve(cfg, s)[3].watts == -1);
    assert(power::label(reference[2], reference[2].watts).find(L"≈") == 0);
    assert(power::label({}, -1) == L"—");
    power::Motion m;
    m.step(0, 12, true); m.step(.1, 12, true); assert(m.phase > 0);
    double phase = m.phase;
    m.step(.2, 12, false); m.step(.3, -1, true); m.step(.4, 0, true); assert(m.phase == phase);
    cfg.data["estimatePower"] = "bad"; cfg.data["platformBaseWatts"] = -5;
    cfg.normalize(); assert(!cfg.get<bool>("estimatePower", true) && cfg.get<double>("platformBaseWatts", 0) == -1);
  }
  {
    Config cfg; cfg.data["estimatePower"] = true; Snapshot s;
    s.cpuPower = 18; s.gpuPower = 4; s.memoryPower = 2; s.discreteGpuCount = 1;
    s.platform.kind = 2; s.platform.disksKnown = true; s.platform.ssd = 1;
    s.platform.brightness = 70; s.diskBusy = 0;
    s.when = s.fans.when = 100; s.fans.state = "ready"; s.fans.rpm = {2100, 2100};
    auto b = power::automaticBaseline(s, 2);
    assert(std::abs(b.watts - (5 + 4.8 + .5 + 2 + 20. / 9)) < 1e-9);
    auto r = power::resolve(cfg, s);
    assert(r[3].model == "hardware-reference-v1" && std::abs(r[3].watts - (24 + b.watts)) < 1e-9);
    assert(!b.assumedBrightness && !b.assumedDisks && !b.assumedFans && !b.assumedActivity);
    s.platform.brightness = 0; double dim = power::automaticBaseline(s, 2).watts;
    s.platform.brightness = 100; assert(std::abs(power::automaticBaseline(s, 2).watts - dim - 4) < 1e-9);
    s.diskBusy = 100; assert(std::abs(power::automaticBaseline(s, 2).storage - 5) < 1e-9);
    s.platform.brightness = -1; assert(power::automaticBaseline(s, 2).assumedBrightness);
    s.fans.rpm = {0, 0}; assert(power::automaticBaseline(s, 2).fans == 0);
    s.fans.state = "failed"; assert(power::automaticBaseline(s, 2).assumedFans);
    s.platform.kind = 1; assert(power::automaticBaseline(s, 2).display == 0);
    s.platform.kind = 0; assert(power::resolve(cfg, s)[3].watts == -1);
    s.platform.kind = 2; s.gpuPower = -1; assert(power::resolve(cfg, s)[3].watts == -1);
    s.gpuPower = 4; s.discreteGpuCount = 2; assert(power::resolve(cfg, s)[3].watts == -1);
    s.systemPower = 0; assert(!power::resolve(cfg, s)[3].estimated && power::resolve(cfg, s)[3].watts == 0);
  }
  {
    Snapshot s;
    s.cpu = 25; s.gpu = 10; s.used = 8; s.total = 16;
    s.diskRead = 100; s.diskWrite = 50; s.down = 30; s.up = 20;
    Group ga; ga.key = L"a"; ga.name = L"A"; ga.cpu = 12; ga.bytes = 200;
    Group gb; gb.key = L"b"; gb.name = L"B"; gb.cpu = 40; gb.bytes = 100;
    s.groups = {ga, gb};
    auto f = situationSample(s, 100, 1);
    assert(f.cpu == 25 && f.mem == 50 && f.gpu == 10 && f.disk == 150 && f.net == 50);
    assert(f.entities.size() == 1 && f.entities[0].key == L"b");
    auto all = situationSample(s, 100, 6);
    assert(all.entities.size() == 2 && all.entities[0].key == L"b");
    SituationHistory h;
    for (int i = 0; i < 70; ++i) h.push(situationSample(s, i, 6));
    assert(h.size() == SituationHistory::Capacity);
    assert(h.latest() && h.latest()->when == 69);
    h.clear(); assert(h.size() == 0 && h.latest() == nullptr);
  }
  int count = 0;
  for (int n : {2, 4, 8, 12, 24, 32, 64})
    for (int w : {40, 90, 180, 280, 400, 600, 900})
      for (int h : {20, 40, 100, 200, 400, 700}) {
        auto l = LayoutEngine{n, c}.choose(float(w), float(h));
        assert(l.w > 0 && l.h > 0 && l.scale > 0);
        for (auto &b : l.blocks) {
          assert(b.x >= 0 && b.y >= 0 && b.w > 0 && b.h > 0);
          assert(b.x + b.w <= l.w + .01 && b.y + b.h <= l.h + .01);
          if (b.kind == "cpu") {
            Block area{"", b.x, b.y + 31, b.w, b.h - 58};
            auto chart = cpuChart(n, area);
            assert(int(chart.slots.size()) == n);
            for (size_t i = 0; i < chart.slots.size(); ++i) {
              auto r = chart.slots[i];
              assert(r.w > 0 && r.h > 0 && r.x >= area.x - .01 && r.y >= area.y - .01);
              assert(r.x + r.w <= area.x + area.w + .01 && r.y + r.h <= area.y + area.h + .01);
              for (size_t j = 0; j < i; ++j) {
                auto q = chart.slots[j];
                assert(r.x + r.w <= q.x + .01 || q.x + q.w <= r.x + .01 ||
                       r.y + r.h <= q.y + .01 || q.y + q.h <= r.y + .01);
              }
            }
          }
        }
        ++count;
      }
  Follow f;
  f.set(10, 0, 1, false);
  f.set(90, 1, 1, true);
  assert(std::abs(f.value(1) - 10) < .001);
  assert(f.value(4) > 89);
  NtCore a{}, b{};
  a.Kernel.QuadPart = 100;
  a.User.QuadPart = 100;
  a.Idle.QuadPart = 40;
  assert(Sampler::utilization(a, b) == 80);
  assert(Sampler::utilization(b, b) == -1);
  assert(std::abs(telemetry::cpuPercent(10000000, 0, 120000000) - 100. / 12) < 1e-9);
  assert(telemetry::cpuPercent(120000000, 0, 120000000) == 100);
  assert(telemetry::cpuPercent(1, 2, 100) == -1);
  assert(telemetry::cpuPercent(100, 0, 0) == -1);
  assert(telemetry::cpuPercent(101, 0, 100) == -1);
  assert(telemetry::temperature((1ull << 31) | (35ull << 16), 100) == 65);
  assert(telemetry::temperature(35ull << 16, 100) == -1);
  assert(telemetry::temperature(1ull << 31, 0) == -1);
  {
    FanReading f;
    assert(f.fastest() == -1 && !f.fresh(1, 1));
    f.state = "ready"; f.when = 10; f.rpm = {0, 0};
    assert(f.fastest() == 0 && f.fresh(12, 1));
    f.rpm = {1800, 2100};
    assert(f.fastest() == 2100 && !f.fresh(17, 1));
    f.rpm = {1800, -1};
    assert(f.fastest() == -1);
    FanMotion m;
    for (int i = 0; i < 100; ++i) m.step(1 + i * .02, 2100, 3400, true);
    assert(m.speed > 0 && m.angle > 0);
    double angle = m.angle;
    m.step(3.1, -1, 3400, true);
    assert(m.speed == 0 && m.angle == angle);
    m.step(3.2, 2100, 3400, false);
    assert(m.speed == 0 && m.angle == angle);
    m.step(3.3, 0, 3400, true);
    assert(m.speed == 0 && m.angle == angle);
  }
  {
    Config cfg;
    Snapshot s;
    cfg.data["menuBarShows"] = "memory";
    assert(trayReading(cfg, s).text == L"—");
    s.used = 4; s.total = 16;
    assert(trayReading(cfg, s).text == L"25");
    assert(Sampler::demand(false, cfg) == Sampler::Memory);
    cfg.data["menuBarShows"] = "gpu";
    cfg.data["hidden"] = {"gpu"};
    assert(Sampler::demand(false, cfg) == Sampler::GPU);
    s.gpu = 0;
    assert(trayReading(cfg, s).text == L"0");
    cfg.data["menuBarShows"] = "power";
    assert(Sampler::demand(false, cfg) == Sampler::Power);
    assert(trayReading(cfg, s).text == L"—");
    cfg.data["menuBarShows"] = "download";
    assert(Sampler::demand(false, cfg) == Sampler::Network);
    s.down = 999500;
    assert(trayReading(cfg, s).text == L"1.0M");
    cfg.data["menuBar"] = false;
    assert(Sampler::demand(false, cfg) == 0);
    cfg.data["menuBarShows"] = "invalid";
    cfg.normalize();
    assert(cfg.trayMetric() == "cpu");
  }
  telemetry::Energy e;
  telemetry::Traffic traffic;
  assert(traffic.sample(0xfffffff0u, 1) == -1);
  assert(traffic.sample(0x10u, 2) == 2048);
  assert(traffic.sample(0x10u, 3) == 0);
  assert(traffic.sample(0x20u, 3) == -1);
  assert(traffic.sample(0x30u, 8) == -1);
  traffic.reset(); assert(traffic.sample(0x40u, 9) == -1);
  assert(e.sample(0xfffffff0u, 1, 1. / 16384) == -1);
  assert(e.sample(0x00003ff0u, 2, 1. / 16384) == 1);
  assert(e.sample(0x00007ff0u, 2, 1. / 16384) == -1);
  assert(e.sample(0x0000bff0u, 100, 1. / 16384) == -1);
  e.reset();
  assert(e.sample(42, 101, 1. / 16384) == -1);
  { // A failed enumeration must not preserve an old numerator with a new denominator.
    Sampler sampler;
    sampler.query = nullptr;
    sampler.previous[1] = Member{};
    sampler.previousCapacity = 100;
    Snapshot sample;
    sample.groups.push_back(Group{});
    sample.groups[0].cpu = 50;
    sampler.processes(sample);
    assert(sampler.previous.empty() && sampler.previousCapacity == 0);
    assert(sample.groups[0].cpu == -1);
    sample.cores.assign(sampler.nominal.size(), 25);
    sample.ghz.assign(sampler.nominal.size(), 3);
    sampler.cpu(sample, true);
    assert(std::all_of(sample.cores.begin(), sample.cores.end(), [](double v) { return v == -1; }));
    const bool nvReady = sampler.nv.ok;
    sampler.nv.ok = false;
    sample.gpu = sample.gpuPower = sample.gpuTemp = 50;
    sampler.nv.read(sample);
    assert(sample.gpu == -1 && sample.gpuPower == -1 && sample.gpuTemp == -1);
    sampler.nv.ok = nvReady;
  }
  return count == 294 ? 0 : 1;
}
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, LPWSTR, int) {
  using SetDpi = BOOL(WINAPI *)(HANDLE);
  auto dpi = reinterpret_cast<SetDpi>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
  if (dpi)
    dpi(reinterpret_cast<HANDLE>(-4));
  else
    SetProcessDPIAware();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::wstring config, startupUser, command = L"show";
  int startupAction = -1;
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    if (a == L"--install-sensors") {
      // Explicit repair also exercises the bundled official offline installer.
      DWORD result = sensorSetup::install();
      if (!result) result = sensorSetup::ensure();
      LocalFree(argv); CoUninitialize(); return int(result);
    }
    if (a == L"--layout-report" && i + 2 < argc) {
      auto input = std::wstring(argv[++i]), output = std::wstring(argv[++i]);
      int result = 0;
      try {
        json results = json::array();
        for (auto &test : json::parse(readFile(input))) {
          Config c;
          c.data.update(test.value("config", json::object()));
          c.normalize();
          auto l = LayoutEngine{test["n"].get<int>(), c}.choose(test["width"].get<float>(),
                                                                test["height"].get<float>());
          json item = {{"tier", l.c.tier},   {"columns", l.c.cols}, {"count", l.c.count},
                       {"strip", l.c.strip}, {"wide", l.c.wide},    {"width", l.w},
                       {"height", l.h},      {"scale", l.scale}};
          for (auto &b : l.blocks) {
            item["blocks"].push_back({b.kind, b.x, b.y, b.w, b.h});
            if (b.kind == "cpu") {
              auto chart = cpuChart(test["n"].get<int>(), {"", b.x, b.y + 31, b.w, b.h - 58});
              item["cpuMode"] = chart.grid ? "grid" : chart.rows == 1 ? "bars" : "twoRows";
              for (const auto &r : chart.slots) item["slots"].push_back({r.x, r.y, r.w, r.h});
            }
          }
          results.push_back(item);
        }
        if (!writeFile(output, results.dump()))
          result = 6;
      } catch (...) {
        result = 7;
      }
      LocalFree(argv);
      CoUninitialize();
      return result;
    }
    if (a == L"--sample-report" && i + 2 < argc) {
      const std::wstring output = argv[++i];
      const int count = std::max(2, std::min(120, _wtoi(argv[++i])));
      Sampler sampler;
      Snapshot sample = sampler.copy();
      json samples = json::array();
      for (int n = 0; n < count; ++n) {
        const double start = seconds();
        sampler.cpu(sample, true);
        sampler.processes(sample);
        sampler.memory(sample);
        sampler.disk(sample);
        sampler.net(sample, seconds());
        sampler.nv.read(sample);
        sampler.sensors.read(sample);
        sampler.bandwidth.read(sample, sampler.sensors);
        sampler.fans.read(sample.fans);
        double sum = 0, maximum = 0;
        for (const auto &g : sample.groups) if (g.cpu >= 0) {
          sum += g.cpu; maximum = std::max(maximum, g.cpu);
        }
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory));
        json coresRaw = json::array();
        for (const auto &core : sampler.lastCores)
          coresRaw.push_back({core.Idle.QuadPart, core.Kernel.QuadPart, core.User.QuadPart});
        json netRaw = json::array();
        for (const auto &entry : sampler.network)
          netRaw.push_back({entry.first, entry.second.first, entry.second.second});
        samples.push_back({{"fans", sample.fans.report()}, {"cpu", sample.cpu}, {"processSum", sum}, {"processMax", maximum},
          {"memoryWatts", sample.memoryPower}, {"systemWatts", sample.systemPower},
          {"memoryPowerState", sample.memoryPowerState}, {"memoryPowerError", sample.memoryPowerError},
          {"diskBusy", sample.diskBusy}, {"diskRead", sample.diskRead}, {"diskWrite", sample.diskWrite},
          {"cpuPackageWatts", sample.cpuPower}, {"cpuPackageCelsius", sample.cpuTemp},
          {"sensorState", sample.sensorState}, {"sensorError", sample.sensorError},
          {"thermalRaw", sampler.sensors.thermalRaw}, {"targetRaw", sampler.sensors.targetRaw},
          {"unitRaw", sampler.sensors.unitRaw}, {"energyRaw", sampler.sensors.energy.counter},
          {"energyTime", sampler.sensors.energy.when},
          {"cpuRaw", {sampler.lastTotal.Idle.QuadPart, sampler.lastTotal.Kernel.QuadPart, sampler.lastTotal.User.QuadPart}},
          {"coresRaw", coresRaw}, {"cores", sample.cores}, {"ghz", sample.ghz},
          {"networkRaw", netRaw}, {"networkTime", sampler.lastNetwork},
          {"networkDown", sample.down}, {"networkUp", sample.up},
          {"memoryUsed", sample.used}, {"memoryTotal", sample.total}, {"pagefileUsed", sample.swap},
          {"memoryReadBytesPerSec", sample.memoryRead}, {"memoryWriteBytesPerSec", sample.memoryWrite},
          {"memoryBandwidthState", sample.memoryBandwidthState}, {"memoryBandwidthError", sample.memoryBandwidthError},
          {"gpu", sample.gpu}, {"gpuMemory", sample.gpuMem}, {"gpuTotal", sample.gpuTotal},
          {"gpuReserved", sample.gpuReserved},
          {"gpuCelsius", sample.gpuTemp}, {"gpuWatts", sample.gpuPower}, {"gpuGHz", sample.gpuClock},
          {"sensorMs", sample.sensorMs}, {"sampleMs", (seconds()-start)*1000},
          {"privateMiB", memory.PrivateUsage / 1048576.}});
        if (n + 1 < count) Sleep(1000);
      }
      const int result = writeFile(output, samples.dump(2)) ? 0 : 8;
      sampler.fans.close();
      LocalFree(argv); CoUninitialize(); return result;
    }
    if (a == L"--self-test") {
      LocalFree(argv);
      int result = selfTest();
      CoUninitialize();
      return result;
    }
    if (a == L"--autostart-enable" || a == L"--autostart-disable")
      startupAction = a == L"--autostart-enable" ? 1 : 0;
    else if (a == L"--startup-user" && i + 1 < argc)
      startupUser = argv[++i];
    else if (a == L"--config" && i + 1 < argc)
      config = argv[++i];
    else if (a == L"--inspect" && i + 1 < argc)
      command = L"inspect:" + std::wstring(argv[++i]);
    else if (a == L"--capture" && i + 1 < argc)
      command = L"capture:" + std::wstring(argv[++i]);
    else if (a == L"--capture-situation" && i + 1 < argc)
      command = L"captureSit:" + std::wstring(argv[++i]);
    else if (a == L"show" || a == L"hide" || a == L"toggle" || a == L"quit" || a == L"settings" ||
             a == L"situation")
      command = a;
  }
  LocalFree(argv);
  if (config.empty()) {
    auto dir = appdata() + L"\\ftop-native";
    CreateDirectoryW(dir.c_str(), nullptr);
    config = dir + L"\\config.json";
    if (GetFileAttributesW(config.c_str()) == INVALID_FILE_ATTRIBUTES)
      CopyFileW((appdata() + L"\\ftop\\config.json5").c_str(), config.c_str(), TRUE);
  }
  wchar_t full[32768];
  GetFullPathNameW(config.c_str(), 32768, full, nullptr);
  config = full;
  if (startupAction >= 0) {
    // Do not silently register for a different account supplied in a UAC credential prompt.
    const HRESULT hr = !startupUser.empty() && startupUser != startup::sid()
                           ? E_ACCESSDENIED : startup::set(startupAction != 0, config);
    CoUninitialize();
    return int(hr);
  }
  unsigned long long hash = 1469598103934665603ull;
  for (auto ch : lower(config)) {
    hash ^= ch;
    hash *= 1099511628211ull;
  }
  std::wstring cls = L"ftop-native-" + std::to_wstring(hash);
  HANDLE mutex = CreateMutexW(nullptr, FALSE, (L"Local\\" + cls).c_str());
  bool exists = GetLastError() == ERROR_ALREADY_EXISTS;
  if (exists) {
    HWND target = nullptr;
    for (int i = 0; i < 30 && !target; ++i) {
      target = FindWindowW(cls.c_str(), L"top");
      if (!target)
        Sleep(100);
    }
    if (target) {
      COPYDATASTRUCT data{0, DWORD((command.size() + 1) * sizeof(wchar_t)), command.data()};
      DWORD_PTR result = 0;
      BOOL ok = SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                                    SMTO_ABORTIFHUNG, 5000, &result) != 0;
      CloseHandle(mutex);
      CoUninitialize();
      return ok && result ? 0 : 2;
    }
    CloseHandle(mutex);
    CoUninitialize();
    return 3;
  }
  if (command == L"quit" || command == L"hide" || command.rfind(L"inspect:", 0) == 0 ||
      command.rfind(L"capture:", 0) == 0 || command.rfind(L"captureSit:", 0) == 0) {
    CloseHandle(mutex);
    CoUninitialize();
    return 4;
  }
  int result = 0;
  {
    IntelTelemetry capabilities;
    if (capabilities.supported) {
      const DWORD setupResult = sensorSetup::ensure();
      if (setupResult) {
        const std::wstring message = setupResult == ERROR_SUCCESS_REBOOT_REQUIRED
            ? L"传感器驱动需要在下次重启后生效。当前温度暂不可用。\nSensor setup requires a restart. Temperature is unavailable until then."
            : L"传感器准备未完成，温度暂不可用。错误码：" + std::to_wstring(setupResult) +
              L"\nSensor setup did not complete. Other monitoring remains available.";
        MessageBoxW(nullptr, message.c_str(), L"top", MB_OK | MB_ICONINFORMATION);
      }
    }
    App app(h);
    app.cfg.path = config;
    if (!app.cfg.load(true) && GetFileAttributesW(config.c_str()) == INVALID_FILE_ATTRIBUTES)
      app.cfg.save();
    app.create(cls);
    if (!app.panel.hwnd)
      result = 5;
    else {
      if (command == L"situation") app.showSituation();
      MSG msg;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
    }
  }
  CloseHandle(mutex);
  CoUninitialize();
  return result;
}
