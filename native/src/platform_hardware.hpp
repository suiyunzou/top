#pragma once
#include "fan_telemetry.hpp"

// Read-only inventory and brightness on the existing sampler worker.
// WMI reports hardware/state; watt coefficients live separately in the model.
struct PlatformProbe {
  IWbemServices *wmi = nullptr;
  PlatformHardware cached;
  double nextBrightness = 0;
  ~PlatformProbe() { close(); }
  void close() { LenovoFans::drop(wmi); }
  static IWbemServices *connect(const wchar_t *name) {
    IWbemLocator *locator = nullptr; IWbemServices *service = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void **>(&locator));
    if (SUCCEEDED(hr)) {
      LenovoFans::Text ns(name);
      hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT,
                                   nullptr, nullptr, &service);
    }
    LenovoFans::drop(locator);
    if (SUCCEEDED(hr)) hr = CoSetProxyBlanket(service, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
        nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) LenovoFans::drop(service);
    return service;
  }
  static double number(IWbemClassObject *item, const wchar_t *key) {
    VARIANT v, n; VariantInit(&v); VariantInit(&n); double result = -1;
    if (SUCCEEDED(item->Get(key, 0, &v, nullptr, nullptr)) && v.vt != VT_NULL && v.vt != VT_EMPTY &&
        SUCCEEDED(VariantChangeType(&n, &v, 0, VT_R8))) result = n.dblVal;
    VariantClear(&v); VariantClear(&n); return result;
  }
  template<class F> static bool query(IWbemServices *service, const wchar_t *sql, F use, double budget = 1.5) {
    if (!service) return false;
    IEnumWbemClassObject *items = nullptr;
    LenovoFans::Text language(L"WQL"), text(sql);
    HRESULT hr = service->ExecQuery(language, text, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                    nullptr, &items);
    if (FAILED(hr)) { LenovoFans::drop(items); return false; }
    bool complete = false; double deadline = seconds() + budget;
    for (int i = 0; i < 64 && seconds() < deadline; ++i) {
      IWbemClassObject *item = nullptr; ULONG count = 0;
      hr = items->Next(300, 1, &item, &count);
      if (FAILED(hr)) { LenovoFans::drop(item); break; }
      if (count && item) use(item);
      LenovoFans::drop(item);
      if (hr == WBEM_S_FALSE) { complete = true; break; }
      // A short wait expiring does not end the enumeration. Keep waiting
      // within the total budget, including slow storage-provider startup.
      if (hr != S_OK && hr != WBEM_S_TIMEDOUT) break;
    }
    LenovoFans::drop(items); return complete;
  }
  void initialize() {
    auto *cim = connect(L"ROOT\\CIMV2"); int kind = 0;
    if (query(cim, L"SELECT PCSystemType FROM Win32_ComputerSystem", [&](IWbemClassObject *o) {
      int type = int(number(o, L"PCSystemType"));
      kind = type == 2 ? 2 : type == 1 || type == 3 ? 1 : 0;
    })) cached.kind = kind;
    LenovoFans::drop(cim);
    auto *storage = connect(L"ROOT\\Microsoft\\Windows\\Storage");
    cached.disksKnown = query(storage, L"SELECT MediaType,BusType FROM MSFT_PhysicalDisk", [&](IWbemClassObject *o) {
      int bus = int(number(o, L"BusType")), type = int(number(o, L"MediaType"));
      // Do not count USB, iSCSI, virtual disks or Storage Spaces as internal drives.
      if (bus == 7 || bus == 9 || bus == 14 || bus == 15 || bus == 16) return;
      if (type == 4) ++cached.ssd;
      else if (type == 3) ++cached.hdd;
      else ++cached.otherDisks;
    }, 5.);
    LenovoFans::drop(storage);
    if (!cached.disksKnown) cached.ssd = cached.hdd = cached.otherDisks = 0;
    wmi = connect(L"ROOT\\WMI");
  }
  void read(Snapshot &s, bool active) {
    double now = seconds();
    if (active && now >= nextBrightness) {
      double b = -1; int count = 0;
      bool ok = query(wmi, L"SELECT CurrentBrightness FROM WmiMonitorBrightness WHERE Active=TRUE", [&](IWbemClassObject *o) {
        double v = number(o, L"CurrentBrightness");
        if (std::isfinite(v) && v >= 0 && v <= 100) { b = v; ++count; }
      });
      cached.brightness = ok && count == 1 ? b : -1;
      nextBrightness = now + (ok ? 10 : 30);
    }
    s.platform = cached;
  }
};
