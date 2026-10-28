#pragma once
#include "power_estimate.hpp"

struct PowerSetup {
  Config &cfg;
  const Snapshot &snap;
  std::wstring text(HWND hwnd, int id) {
    wchar_t value[256]{}; GetDlgItemTextW(hwnd, id, value, 256);
    std::wstring s(value);
    auto start = s.find_first_not_of(L" \t\r\n");
    return start == std::wstring::npos ? L"" : s.substr(start, s.find_last_not_of(L" \t\r\n") - start + 1);
  }
  bool save(HWND hwnd) {
    double values[2]; std::wstring sources[2];
    for (int i = 0; i < 2; ++i) {
      auto s = text(hwnd, 1001 + i * 2);
      sources[i] = text(hwnd, 1002 + i * 2);
      values[i] = -1;
      if (!s.empty()) {
        wchar_t *end = nullptr;
        values[i] = wcstod(s.c_str(), &end);
        if (end == s.c_str() || *end || !power::valid(values[i]) || values[i] > 1000 || sources[i].empty()) {
          SetDlgItemTextW(hwnd, 1020, cfg.tr(L"请输入 0–1000 W 和来源；留空表示不用。", L"Enter 0–1000 W and a source, or leave watts empty.").c_str());
          SetFocus(GetDlgItem(hwnd, 1001 + i * 2)); return false;
        }
      }
    }
    if (snap.powerHardware.empty() && (values[0] >= 0 || values[1] >= 0)) {
      SetDlgItemTextW(hwnd, 1020, cfg.tr(L"尚未读到硬件配置，暂时不能保存参考值。", L"Hardware identity is unavailable; cannot bind this profile.").c_str());
      return false;
    }
    cfg.load();
    cfg.data["memoryReferenceWatts"] = values[0];
    cfg.data["memoryReferenceSource"] = utf8(sources[0]);
    cfg.data["platformBaseWatts"] = values[1];
    cfg.data["platformBaseSource"] = utf8(sources[1]);
    cfg.data["powerProfileHardware"] = snap.powerHardware;
    if (cfg.save()) return true;
    SetDlgItemTextW(hwnd, 1020, cfg.tr(L"设置未保存，请检查配置文件。", L"Could not save the configuration.").c_str());
    return false;
  }
  static INT_PTR CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *self = reinterpret_cast<PowerSetup *>(GetWindowLongPtrW(hwnd, DWLP_USER));
    if (msg == WM_INITDIALOG) {
      self = reinterpret_cast<PowerSetup *>(lp); SetWindowLongPtrW(hwnd, DWLP_USER, lp);
      auto &c = self->cfg;
      SetWindowTextW(hwnd, c.tr(L"功耗估算参数", L"Power estimate parameters").c_str());
      const wchar_t *zh[] = {L"内存参考功耗（W，全部内存条合计）", L"依据 / 来源", L"其余部件基础功耗（W）", L"依据 / 来源"};
      const wchar_t *en[] = {L"Memory reference (W, all modules)", L"Basis / source", L"Other components baseline (W)", L"Basis / source"};
      for (int i = 0; i < 4; ++i) SetDlgItemTextW(hwnd, 1011 + i, c.tr(zh[i], en[i]).c_str());
      SetDlgItemTextW(hwnd, IDOK, c.tr(L"保存", L"Save").c_str());
      SetDlgItemTextW(hwnd, IDCANCEL, c.tr(L"取消", L"Cancel").c_str());
      SetDlgItemTextW(hwnd, 1015, c.tr(L"基础功耗含屏幕、主板、磁盘和风扇；不含 CPU、独显、内存。\n范围为电脑直流侧，不含充电、外设和适配器损耗。", L"Baseline includes display, board, disks and fans; excludes CPU, GPU and RAM.\nDC device power; excludes charging, peripherals and adapter losses.").c_str());
      double ref = power::samsungReference(self->snap.memoryModules);
      auto note = ref >= 0 ? c.tr(L"内存留空：自动参考工况 ", L"Memory empty: automatic reference ") + power::label({ref, true, ""}, ref)
                           : c.tr(L"内存留空：未匹配模型时显示 —", L"Memory empty: — if no model matches");
      note += c.tr(L"；基础功耗留空：按硬件自动估算。", L". Baseline empty: automatic hardware estimate.");
      SetDlgItemTextW(hwnd, 1016, note.c_str());
      SetDlgItemTextW(hwnd, 1020, c.tr(L"无需填写即可自动估算。这里只用于有依据的手动覆盖。", L"No input is required. Use these fields only for documented overrides.").c_str());
      for (int id : {1001, 1003}) SendDlgItemMessageW(hwnd, id, EM_SETLIMITTEXT, 12, 0);
      for (int id : {1002, 1004}) SendDlgItemMessageW(hwnd, id, EM_SETLIMITTEXT, 80, 0);
      if (power::profileMatches(c, self->snap)) {
        for (int i = 0; i < 2; ++i) {
          double v = c.get<double>(i ? "platformBaseWatts" : "memoryReferenceWatts", -1);
          if (power::valid(v)) SetDlgItemTextW(hwnd, 1001 + i * 2, num(v, 2).c_str());
          SetDlgItemTextW(hwnd, 1002 + i * 2, wide(c.get<std::string>(i ? "platformBaseSource" : "memoryReferenceSource", "")).c_str());
        }
      }
      return TRUE;
    }
    if (!self) return FALSE;
    if (msg == WM_COMMAND) {
      if (LOWORD(wp) == IDCANCEL) { EndDialog(hwnd, IDCANCEL); return TRUE; }
      if (LOWORD(wp) == IDOK) { if (self->save(hwnd)) EndDialog(hwnd, IDOK); return TRUE; }
    }
    return FALSE;
  }
  bool show(HWND owner, HINSTANCE instance) {
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(301), owner, proc, reinterpret_cast<LPARAM>(this)) == IDOK;
  }
};
