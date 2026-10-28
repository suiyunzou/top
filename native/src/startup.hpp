#pragma once
#include <sddl.h>
#include <taskschd.h>

// Task Scheduler is used only when reading/changing this setting. No resident helper.
namespace startup {
template <class T> struct Ptr {
  T *p = nullptr;
  ~Ptr() { if (p) p->Release(); }
  T *operator->() const { return p; }
};
struct Text {
  BSTR p;
  explicit Text(const std::wstring &s) : p(SysAllocStringLen(s.data(), UINT(s.size()))) {}
  ~Text() { SysFreeString(p); }
  operator BSTR() const { return p; }
};
inline std::wstring sid() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> data(size);
  std::wstring result;
  if (GetTokenInformation(token, TokenUser, data.data(), size, &size)) {
    LPWSTR text = nullptr;
    if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &text)) {
      result = text;
      LocalFree(text);
    }
  }
  CloseHandle(token);
  return result;
}
inline std::wstring executable() {
  wchar_t path[32768]{};
  DWORD count = GetModuleFileNameW(nullptr, path, 32768);
  return count && count < 32768 ? std::wstring(path, count) : std::wstring();
}
inline std::wstring xml(const std::wstring &s) {
  std::wstring result;
  for (wchar_t c : s) {
    if (c == L'&') result += L"&amp;";
    else if (c == L'<') result += L"&lt;";
    else if (c == L'>') result += L"&gt;";
    else if (c == L'\"') result += L"&quot;";
    else result += c;
  }
  return result;
}
inline std::wstring taskName(const std::wstring &user) { return L"ftop-native-" + user; }
struct Connection {
  Ptr<ITaskService> service;
  Ptr<ITaskFolder> folder;
  HRESULT open() {
    // Explicit GUIDs also work with the older MinGW import libraries.
    const CLSID cls = {0x0f87369f, 0xa4e5, 0x4cfc, {0xbd,0x3e,0x73,0xe6,0x15,0x45,0x72,0xdd}};
    const IID iid = {0x2faba4c7, 0x4da9, 0x4013, {0x96,0x97,0x20,0xcc,0x3f,0xd4,0x0f,0x85}};
    HRESULT hr = CoCreateInstance(cls, nullptr, CLSCTX_INPROC_SERVER, iid,
                                  reinterpret_cast<void **>(&service.p));
    VARIANT empty{};
    if (SUCCEEDED(hr)) hr = service->Connect(empty, empty, empty, empty);
    if (SUCCEEDED(hr)) hr = service->GetFolder(Text(L"\\"), &folder.p);
    return hr;
  }
};
constexpr const wchar_t *runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline bool legacyEnabled() {
  DWORD size = 0;
  return RegGetValueW(HKEY_CURRENT_USER, runKey, L"ftop-native", RRF_RT_REG_SZ,
                       nullptr, nullptr, &size) == ERROR_SUCCESS;
}
inline HRESULT removeLegacy() {
  HKEY key = nullptr;
  LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_SET_VALUE, &key);
  if (status == ERROR_FILE_NOT_FOUND) return S_OK;
  if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
  status = RegDeleteValueW(key, L"ftop-native");
  RegCloseKey(key);
  return status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(status);
}
inline bool enabled() {
  Connection c;
  if (SUCCEEDED(c.open())) {
    Ptr<IRegisteredTask> task;
    if (SUCCEEDED(c.folder->GetTask(Text(taskName(sid())), &task.p))) {
      VARIANT_BOOL on = VARIANT_FALSE;
      if (SUCCEEDED(task->get_Enabled(&on))) return on == VARIANT_TRUE;
    }
  }
  return legacyEnabled();
}
inline HRESULT set(bool on, const std::wstring &config) {
  const auto user = sid(), exe = executable();
  if (user.empty() || exe.empty()) return E_FAIL;
  Connection c;
  HRESULT hr = c.open();
  if (FAILED(hr)) return hr;
  const Text name(taskName(user));
  if (!on) {
    hr = c.folder->DeleteTask(name, 0);
    if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) hr = S_OK;
  } else {
    const auto directory = exe.substr(0, exe.find_last_of(L"\\/"));
    const std::wstring definition =
      L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
      L"<RegistrationInfo><Description>top: start for this user at sign-in</Description></RegistrationInfo>"
      L"<Triggers><LogonTrigger><Enabled>true</Enabled><UserId>" + xml(user) +
      L"</UserId></LogonTrigger></Triggers><Principals><Principal id=\"User\"><UserId>" + xml(user) +
      L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel>"
      L"</Principal></Principals><Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
      L"<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
      L"<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><StartWhenAvailable>true</StartWhenAvailable>"
      L"<AllowStartOnDemand>true</AllowStartOnDemand><Enabled>true</Enabled>"
      L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit></Settings><Actions Context=\"User\"><Exec><Command>" + xml(exe) +
      L"</Command><Arguments>" + xml(L"--config \"" + config + L"\" show") +
      L"</Arguments><WorkingDirectory>" + xml(directory) + L"</WorkingDirectory></Exec></Actions></Task>";
    Text account(user);
    VARIANT principal{}, empty{};
    principal.vt = VT_BSTR;
    principal.bstrVal = account;
    Ptr<IRegisteredTask> task;
    hr = c.folder->RegisterTask(name, Text(definition), TASK_CREATE_OR_UPDATE,
                               principal, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &task.p);
  }
  // Keep the old entry until the replacement was successfully registered.
  return SUCCEEDED(hr) ? removeLegacy() : hr;
}
inline HRESULT change(HWND owner, bool on, const std::wstring &config) {
  HRESULT hr = set(on, config);
  if (hr != E_ACCESSDENIED) return hr;
  const auto exe = executable();
  const auto args = std::wstring(on ? L"--autostart-enable" : L"--autostart-disable") +
                    L" --config \"" + config + L"\" --startup-user " + sid();
  SHELLEXECUTEINFOW info{sizeof(info)};
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.hwnd = owner;
  info.lpVerb = L"runas";
  info.lpFile = exe.c_str();
  info.lpParameters = args.c_str();
  info.nShow = SW_HIDE;
  if (!ShellExecuteExW(&info)) return HRESULT_FROM_WIN32(GetLastError());
  // The helper only changes the task, then exits. Pump paint messages while it runs.
  while (MsgWaitForMultipleObjects(1, &info.hProcess, FALSE, INFINITE, QS_PAINT) == WAIT_OBJECT_0 + 1) {
    MSG message;
    while (PeekMessageW(&message, nullptr, WM_PAINT, WM_PAINT, PM_REMOVE)) DispatchMessageW(&message);
  }
  DWORD code = ERROR_GEN_FAILURE;
  GetExitCodeProcess(info.hProcess, &code);
  CloseHandle(info.hProcess);
  return HRESULT(code);
}
} // namespace startup
