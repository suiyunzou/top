#include "../src/main.cpp"
#include <iostream>

// Exercise the rendered controls and the real event handler with an isolated config.
int main() {
  SetProcessDPIAware();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int result = 0;
  {
    App app(GetModuleHandleW(nullptr));
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    app.cfg.path = std::wstring(temp) + L"ftop-pin-test-" + std::to_wstring(GetCurrentProcessId()) + L".json";
    app.cfg.data["width"] = 615.2;
    app.cfg.data["height"] = 293.6;
    app.cfg.data["motion"] = false;
    app.cfg.data["menuBar"] = false;
    app.create(L"ftop-pin-test-" + std::to_wstring(GetCurrentProcessId()));
    auto controls = [&] {
      app.showSettings({80, 80});
    };
    auto check = [&](bool ok, const char *label) {
      std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
      if (!ok) result = 1;
    };
    controls();
    for (const auto &id : {"pin"}) {
      auto it = std::find_if(app.settings.hits.begin(), app.settings.hits.end(),
                             [&](const Hit &h) { return h.id == id; });
      if (it == app.settings.hits.end()) { check(false, "control exists"); continue; }
      const Hit button = *it;
      float factor = app.settings.dpi;
      int x = int((button.r.left + button.r.right) / 2 * factor);
      for (float y : {button.r.top + 3, (button.r.top + button.r.bottom) / 2, button.r.bottom - 3}) {
        auto h = app.at(app.settings, x / factor, y);
        check(h && h->id == id, "settings control receives its full hit area");
        POINT p{x, int(y * factor)};
        ClientToScreen(app.settings.hwnd, &p);
        check(app.event(app.settings.hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y)) == HTCLIENT,
              "settings control remains in the client area");
      }
      const LPARAM click = MAKELPARAM(x, int((button.r.bottom - 3) * factor));
      app.event(app.settings.hwnd, WM_LBUTTONDOWN, 0, click);
      app.event(app.settings.hwnd, WM_LBUTTONUP, 0, click);
      if (button.id == "pin") {
        check((GetWindowLongPtrW(app.panel.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0,
              "lower half click sets native topmost");
        check(!IsWindowVisible(app.detail.hwnd), "pin does not open metric detail");
        Config saved; saved.path = app.cfg.path; saved.load(true);
        check(saved.get<bool>("floating", false), "pin is saved");
        controls();
        app.event(app.settings.hwnd, WM_LBUTTONDOWN, 0, click);
        app.event(app.settings.hwnd, WM_LBUTTONUP, 0, click);
        check((GetWindowLongPtrW(app.panel.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,
              "second click clears native topmost");
      }
      controls();
    }
    DestroyWindow(app.situation.hwnd);
    DestroyWindow(app.detail.hwnd);
    DestroyWindow(app.settings.hwnd);
    DestroyWindow(app.panel.hwnd);
    DeleteFileW(app.cfg.path.c_str());
  }
  CoUninitialize();
  return result;
}
