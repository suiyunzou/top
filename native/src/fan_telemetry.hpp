#pragma once
#include "model.hpp"
#include <wbemidl.h>

// Local Lenovo firmware getters only. No fan-control or power-mode writes.
struct LenovoFans {
  IWbemServices *service = nullptr;
  std::wstring path;
  FanReading cached;
  double nextRead = 0;
  ~LenovoFans() { close(); }
  template<class T> static void drop(T *&p) { if (p) { p->Release(); p = nullptr; } }
  struct Text {
    BSTR value;
    explicit Text(const wchar_t *s) : value(SysAllocString(s)) {}
    ~Text() { SysFreeString(value); }
    operator BSTR() const { return value; }
  };
  void close() { drop(service); path.clear(); }
  void pause() { cached = FanReading{}; cached.state = "paused"; nextRead = 0; }
  HRESULT connect() {
    if (service && !path.empty()) return S_OK;
    close();
    IWbemLocator *locator = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWbemLocator, reinterpret_cast<void **>(&locator));
    if (FAILED(hr)) return hr;
    Text ns(L"ROOT\\WMI");
    hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, WBEM_FLAG_CONNECT_USE_MAX_WAIT,
                                nullptr, nullptr, &service);
    drop(locator);
    if (FAILED(hr)) return hr;
    hr = CoSetProxyBlanket(service, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                          RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) return hr;
    IEnumWbemClassObject *items = nullptr;
    Text language(L"WQL"), query(L"SELECT * FROM LENOVO_GAMEZONE_DATA");
    hr = service->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_ENSURE_LOCATABLE,
                            nullptr, &items);
    if (FAILED(hr)) return hr;
    IWbemClassObject *item = nullptr;
    ULONG count = 0;
    hr = items->Next(500, 1, &item, &count);
    drop(items);
    if (hr == S_OK && count && item) {
      VARIANT v; VariantInit(&v);
      hr = item->Get(L"__PATH", 0, &v, nullptr, nullptr);
      if (SUCCEEDED(hr) && v.vt == VT_BSTR && v.bstrVal) path = v.bstrVal;
      VariantClear(&v);
    }
    drop(item);
    if (hr == WBEM_S_TIMEDOUT) return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    return FAILED(hr) ? hr : path.empty() ? HRESULT(WBEM_E_NOT_FOUND) : S_OK;
  }
  HRESULT get(const wchar_t *method, double &number) {
    IWbemCallResult *call = nullptr;
    IWbemClassObject *result = nullptr;
    Text object(path.c_str()), name(method);
    HRESULT hr = service->ExecMethod(object, name, WBEM_FLAG_RETURN_IMMEDIATELY,
                                     nullptr, nullptr, nullptr, &call);
    if (SUCCEEDED(hr) && call) hr = call->GetResultObject(500, &result);
    drop(call);
    if (hr == WBEM_S_TIMEDOUT) hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    if (hr == S_OK && result) {
      VARIANT status, data; VariantInit(&status); VariantInit(&data);
      hr = result->Get(L"ReturnValue", 0, &status, nullptr, nullptr);
      // Some firmware exposes only Data; CIM's invocation wrapper can add its own
      // ReturnValue. If the native result has a status, require boolean success.
      if (hr == HRESULT(WBEM_E_NOT_FOUND)) hr = S_OK;
      else if (SUCCEEDED(hr) && (status.vt != VT_BOOL || status.boolVal != VARIANT_TRUE)) hr = E_FAIL;
      if (SUCCEEDED(hr)) hr = result->Get(L"Data", 0, &data, nullptr, nullptr);
      if (SUCCEEDED(hr)) {
        if (data.vt == VT_UI4) number = data.ulVal;
        else if (data.vt == VT_I4 && data.lVal >= 0) number = data.lVal;
        else hr = E_UNEXPECTED;
      }
      VariantClear(&status); VariantClear(&data);
    } else if (SUCCEEDED(hr)) hr = E_FAIL;
    drop(result);
    return hr;
  }
  void read(FanReading &out) {
    const double now = seconds();
    if (now < nextRead) { out = cached; return; }
    FanReading value;
    value.source = "Lenovo WMI";
    HRESULT hr = connect();
    double count = -1;
    if (SUCCEEDED(hr)) hr = get(L"GetFanCount", count);
    if (SUCCEEDED(hr) && count != 1 && count != 2) hr = WBEM_E_NOT_SUPPORTED;
    if (SUCCEEDED(hr)) {
      value.rpm.assign(size_t(count), -1);
      for (size_t i = 0; i < value.rpm.size(); ++i) {
        HRESULT read = get(i == 0 ? L"GetFan1Speed" : L"GetFan2Speed", value.rpm[i]);
        if (FAILED(read) || value.rpm[i] > 30000) {
          value.rpm[i] = -1;
          hr = FAILED(read) ? read : E_UNEXPECTED;
        }
      }
      double limit = -1;
      if (SUCCEEDED(get(L"GetFanMaxSpeed", limit)) && limit >= 100 && limit <= 30000)
        value.reference = limit;
    }
    value.when = seconds();
    value.ms = (value.when - now) * 1000;
    value.error = DWORD(hr);
    value.state = SUCCEEDED(hr) ? "ready"
        : hr == E_ACCESSDENIED || hr == HRESULT(WBEM_E_ACCESS_DENIED) ? "access_denied"
        : hr == HRESULT(WBEM_E_INVALID_CLASS) || hr == HRESULT(WBEM_E_NOT_FOUND) || hr == HRESULT(WBEM_E_NOT_SUPPORTED) ? "unsupported"
        : hr == HRESULT_FROM_WIN32(WAIT_TIMEOUT) ? "timeout" : "read_failed";
    if (FAILED(hr)) close();
    nextRead = value.when + (SUCCEEDED(hr) ? 2 : 30);
    cached = value;
    out = value;
  }
};
