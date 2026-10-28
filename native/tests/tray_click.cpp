#include "../src/main.cpp"
#include <iostream>

// Real windows, isolated config, and the same callback used by the tray.
int main() {
  SetProcessDPIAware();
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  int result = 0;
  {
    App app(GetModuleHandleW(nullptr));
    wchar_t temp[MAX_PATH];
    GetTempPathW(MAX_PATH, temp);
    app.cfg.path = std::wstring(temp) + L"ftop-tray-test-" + std::to_wstring(GetCurrentProcessId()) + L".json";
    app.cfg.data["motion"] = false;
    app.cfg.data["menuBar"] = false;
    // Keep unrelated foreground apps below this fixture.
    app.cfg.data["floating"] = true;
    app.create(L"ftop-tray-test-" + std::to_wstring(GetCurrentProcessId()));
    auto check = [&](bool ok, const char *label) {
      std::cout << (ok ? "PASS " : "FAIL ") << label << '\n';
      if (!ok) result = 1;
    };
    auto click = [&] { app.event(app.panel.hwnd, TRAY_MESSAGE, 1, WM_LBUTTONUP); };
    app.showPanel();
    click();
    check(!IsWindowVisible(app.panel.hwnd), "unobscured panel hides");
    click();
    check(IsWindowVisible(app.panel.hwnd), "hidden panel shows");
    for (int mode = 0; mode < 3; ++mode) {
      app.showPanel();
      RECT r{}; GetWindowRect(app.panel.hwnd, &r);
      HWND cover = CreateWindowExW(mode == 2 ? WS_EX_LAYERED : 0, L"STATIC", L"ftop test cover",
                                    WS_POPUP, r.left - 5, r.top - 5,
                                    mode == 0 ? (r.right - r.left) / 2 : r.right - r.left + 10,
                                    r.bottom - r.top + 10, nullptr, nullptr, nullptr, nullptr);
      if (mode == 2) SetLayeredWindowAttributes(cover, 0, 255, LWA_ALPHA);
      ShowWindow(cover, SW_SHOW);
      SetWindowPos(cover, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
      bool startsAbove = false;
      for (HWND h = GetWindow(app.panel.hwnd, GW_HWNDPREV); h; h = GetWindow(h, GW_HWNDPREV))
        if (h == cover) startsAbove = true;
      check(startsAbove, "cover starts above panel");
      click();
      bool coverAbove = false;
      for (HWND h = GetWindow(app.panel.hwnd, GW_HWNDPREV); h; h = GetWindow(h, GW_HWNDPREV))
        if (h == cover) coverAbove = true;
      check(IsWindowVisible(app.panel.hwnd) && !coverAbove,
            mode == 0 ? "partially covered panel raises on first click" :
            mode == 1 ? "fully covered panel raises on first click" :
                        "layered-window-covered panel raises on first click");
      check((GetWindowLongPtrW(app.panel.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0, "raising preserves pin");
      DestroyWindow(cover);
    }
    ShowWindow(app.panel.hwnd, SW_MINIMIZE);
    click();
    RECT r{}; GetWindowRect(app.panel.hwnd, &r);
    check(IsWindowVisible(app.panel.hwnd) && !IsIconic(app.panel.hwnd) && r.right - r.left > 300,
          "minimized panel restores at normal size");
    app.cfg.data["floating"] = false;
    app.applyConfig();
    app.action("hide");
    click();
    check(IsWindowVisible(app.panel.hwnd) &&
          !(GetWindowLongPtrW(app.panel.hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST),
          "showing unpinned panel does not enable pin");
    DestroyWindow(app.panel.hwnd);
    DeleteFileW(app.cfg.path.c_str());
  }
  CoUninitialize();
  return result;
}
