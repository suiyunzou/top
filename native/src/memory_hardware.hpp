#pragma once
#include "fan_telemetry.hpp"

// Read module identity once on the sampler thread; no SPD driver or writes.
inline void readMemoryHardware(Snapshot &s) {
  IWbemLocator *locator = nullptr;
  IWbemServices *service = nullptr;
  IEnumWbemClassObject *items = nullptr;
  using Text = LenovoFans::Text;
  HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IWbemLocator, reinterpret_cast<void **>(&locator));
  if (SUCCEEDED(hr)) {
    Text ns(L"ROOT\\CIMV2");
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT,
                                nullptr, nullptr, &service);
  }
  if (SUCCEEDED(hr)) hr = CoSetProxyBlanket(service, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
  if (SUCCEEDED(hr)) {
    Text language(L"WQL"), query(L"SELECT PartNumber,Capacity,ConfiguredClockSpeed,SMBIOSMemoryType FROM Win32_PhysicalMemory");
    hr = service->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &items);
  }
  std::vector<MemoryModule> modules;
  bool complete = false;
  if (SUCCEEDED(hr)) for (unsigned i = 0; i < 128; ++i) {
    IWbemClassObject *item = nullptr; ULONG count = 0;
    hr = items->Next(1000, 1, &item, &count);
    if (hr == WBEM_S_FALSE && !count) { complete = true; break; }
    if (hr != S_OK || !count || !item) { LenovoFans::drop(item); break; }
    auto get = [&](const wchar_t *key) {
      VARIANT v; VariantInit(&v); std::wstring value;
      if (SUCCEEDED(item->Get(key, 0, &v, nullptr, nullptr))) {
        VARIANT text; VariantInit(&text);
        if (SUCCEEDED(VariantChangeType(&text, &v, 0, VT_BSTR)) && text.bstrVal) value = text.bstrVal;
        VariantClear(&text);
      }
      VariantClear(&v); return value;
    };
    auto part = utf8(get(L"PartNumber"));
    while (!part.empty() && part.back() == ' ') part.pop_back();
    modules.push_back({part, _wtof(get(L"Capacity").c_str()) / 1073741824.,
                       unsigned(_wtoi(get(L"ConfiguredClockSpeed").c_str())),
                       unsigned(_wtoi(get(L"SMBIOSMemoryType").c_str()))});
    LenovoFans::drop(item);
  }
  LenovoFans::drop(items); LenovoFans::drop(service); LenovoFans::drop(locator);
  if (complete) s.memoryModules = std::move(modules);
  // Bind user-supplied profiles to this installation and the installed modules.
  // Only the hash is retained; no machine ID or serial is displayed or logged.
  wchar_t guid[256]{}; DWORD bytes = sizeof(guid);
  if (!s.memoryModules.empty() && RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
      L"MachineGuid", RRF_RT_REG_SZ, nullptr, guid, &bytes) == ERROR_SUCCESS) {
    std::string key = utf8(guid);
    wchar_t cpu[256]{}; DWORD cpuBytes = sizeof(cpu);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"ProcessorNameString", RRF_RT_REG_SZ, nullptr, cpu, &cpuBytes) == ERROR_SUCCESS) key += "|" + utf8(cpu);
    std::vector<std::string> parts;
    for (const auto &m : s.memoryModules)
      parts.push_back(m.part + ":" + std::to_string(m.gib) + ":" + std::to_string(m.speed));
    std::sort(parts.begin(), parts.end());
    for (const auto &p : parts) key += "|" + p;
    unsigned long long hash = 1469598103934665603ull;
    for (unsigned char ch : key) { hash ^= ch; hash *= 1099511628211ull; }
    s.powerHardware = std::to_string(hash);
  }
}
