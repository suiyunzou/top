#pragma once
#include "model.hpp"
#include "sensor_resources.hpp"
#include <sddl.h>
#include <wintrust.h>
#include <softpub.h>

namespace sensorSetup {
inline DWORD probe() {
  HANDLE device = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (device == INVALID_HANDLE_VALUE) return GetLastError();
  CloseHandle(device); return ERROR_SUCCESS;
}
// Use the publisher's installer unmodified. No internet, boot-policy changes,
// unsigned edition, shared-service removal, or automatic restart.
inline DWORD install() {
  const auto bytes = sensorResources::get(sensorResources::installer);
  if (!bytes.data || !bytes.size) return ERROR_RESOURCE_DATA_NOT_FOUND;
  wchar_t temp[32768]{}, guidText[40]{};
  GUID guid{};
  DWORD length = GetTempPathW(32768, temp);
  if (!length || length >= 32768 || FAILED(CoCreateGuid(&guid))) return ERROR_PATH_NOT_FOUND;
  StringFromGUID2(guid, guidText, 40);
  const std::wstring directory = std::wstring(temp) + L"ftop-sensors-" + guidText;
  const std::wstring file = directory + L"\\PawnIO_setup.exe";
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
      L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1, &descriptor, nullptr)) return GetLastError();
  SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
  const BOOL created = CreateDirectoryW(directory.c_str(), &security);
  const DWORD createError = created ? ERROR_SUCCESS : GetLastError();
  LocalFree(descriptor);
  if (!created) return createError;
  DWORD result = ERROR_SUCCESS;
  HANDLE output = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) result = GetLastError();
  else {
    DWORD written = 0;
    if (!WriteFile(output, bytes.data, bytes.size, &written, nullptr)) result = GetLastError();
    else if (written != bytes.size) result = ERROR_WRITE_FAULT;
    CloseHandle(output);
  }
  HANDLE locked = INVALID_HANDLE_VALUE;
  if (!result) {
    locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (locked == INVALID_HANDLE_VALUE) result = GetLastError();
  }
  if (!result) {
    WINTRUST_FILE_INFO info{}; info.cbStruct = sizeof(info); info.pcwszFilePath = file.c_str(); info.hFile = locked;
    WINTRUST_DATA trust{}; trust.cbStruct = sizeof(trust); trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE; trust.dwUnionChoice = WTD_CHOICE_FILE; trust.pFile = &info;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    result = DWORD(WinVerifyTrust(nullptr, &policy, &trust));
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trust);
  }
  if (!result) {
    std::wstring command = L"\"" + file + L"\" -install -silent";
    STARTUPINFOW start{}; start.cb = sizeof(start); start.dwFlags = STARTF_USESHOWWINDOW; start.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(file.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, directory.c_str(), &start, &process)) result = GetLastError();
    else {
      CloseHandle(process.hThread);
      const DWORD wait = WaitForSingleObject(process.hProcess, 180000);
      if (wait == WAIT_OBJECT_0) {
        if (!GetExitCodeProcess(process.hProcess, &result)) result = GetLastError();
      } else result = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
      CloseHandle(process.hProcess);
    }
  }
  if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
  DeleteFileW(file.c_str()); RemoveDirectoryW(directory.c_str());
  return result;
}
inline DWORD ensure() {
  DWORD result = probe();
  if (!result || result == ERROR_ACCESS_DENIED) return result;
  SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!manager) return GetLastError();
  SC_HANDLE service = OpenServiceW(manager, L"PawnIO", SERVICE_START | SERVICE_QUERY_STATUS);
  result = service ? ERROR_SUCCESS : GetLastError();
  if (service) {
    SERVICE_STATUS status{};
    if (!QueryServiceStatus(service, &status)) result = GetLastError();
    else if (status.dwCurrentState == SERVICE_STOPPED && !StartServiceW(service, 0, nullptr)) result = GetLastError();
    CloseServiceHandle(service);
  }
  CloseServiceHandle(manager);
  if (result == ERROR_SERVICE_DOES_NOT_EXIST) result = install();
  if (result == ERROR_SERVICE_MARKED_FOR_DELETE) return ERROR_SUCCESS_REBOOT_REQUIRED;
  if (result && result != ERROR_SERVICE_ALREADY_RUNNING) return result;
  for (int attempt = 0; attempt < 20; ++attempt) {
    result = probe();
    if (!result) break;
    Sleep(100);
  }
  return result;
}
}
