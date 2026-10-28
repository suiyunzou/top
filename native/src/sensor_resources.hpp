#pragma once
#include <windows.h>

namespace sensorResources {
constexpr int module = 201, installer = 202, mchbar = 203;
struct Bytes { const void *data = nullptr; DWORD size = 0; };
inline Bytes get(int id) {
  HMODULE app = GetModuleHandleW(nullptr);
  HRSRC resource = FindResourceW(app, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));
  if (!resource) return {};
  HGLOBAL memory = LoadResource(app, resource);
  return memory ? Bytes{LockResource(memory), SizeofResource(app, resource)} : Bytes{};
}
}
