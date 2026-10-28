#pragma once
#include "intel_telemetry.hpp"

// Read-only IMC traffic for the verified Comet Lake register layout.
// The separate signed module/handle cannot disturb CPU/DRAM energy sampling.
struct MemoryBandwidth {
  HANDLE device = INVALID_HANDLE_VALUE;
  telemetry::Traffic reads, writes;
  double nextRead = 0, retryAt = 0, readRate = -1, writeRate = -1;
  DWORD error = 0;
  std::string state = "not_sampled";
  ~MemoryBandwidth() { close(); }
  void close() {
    if (device != INVALID_HANDLE_VALUE) CloseHandle(device);
    device = INVALID_HANDLE_VALUE; reads.reset(); writes.reset();
  }
  void pause() {
    reads.reset(); writes.reset(); nextRead = 0;
    readRate = writeRate = -1; state = "paused"; error = 0;
  }
  bool open() {
    device = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (device == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    const auto bytes = sensorResources::get(sensorResources::mchbar);
    DWORD returned = 0;
    if (!bytes.data || !bytes.size || bytes.size > 65536) { error = ERROR_INVALID_DATA; close(); return false; }
    if (!DeviceIoControl(device, (41394u << 16) | (0x821u << 2),
        const_cast<void *>(bytes.data), bytes.size, nullptr, 0, &returned, nullptr)) {
      error = GetLastError(); close(); return false;
    }
    return true;
  }
  bool counter(uint32_t offset, uint32_t &value, double &when) {
    if (offset != 0x5050 && offset != 0x5054) { error = ERROR_ACCESS_DENIED; return false; }
    struct Request { char name[32]; uint64_t offset; } request{};
    static_assert(sizeof(Request) == 40, "PawnIO wire format");
    memcpy(request.name, "ioctl_read_dword", 16); request.offset = offset;
    uint64_t raw = 0; DWORD returned = 0;
    const double before = seconds();
    const BOOL ok = DeviceIoControl(device, (41394u << 16) | (0x841u << 2),
        &request, sizeof(request), &raw, sizeof(raw), &returned, nullptr);
    when = (before + seconds()) * .5;
    if (!ok || returned != sizeof(raw)) { error = ok ? ERROR_INVALID_DATA : GetLastError(); return false; }
    value = uint32_t(raw); return true;
  }
  void read(Snapshot &s, const IntelTelemetry &cpu) {
    const double now = seconds();
    if (now >= nextRead) {
      nextRead = now + .9;
      readRate = writeRate = -1;
      if (!cpu.supported || cpu.model != 0xa5) { state = "unsupported_cpu"; reads.reset(); writes.reset(); }
      else if (device != INVALID_HANDLE_VALUE || now >= retryAt) {
        uint32_t r = 0, w = 0; double rt = 0, wt = 0;
        if ((device != INVALID_HANDLE_VALUE || open()) && counter(0x5050, r, rt) && counter(0x5054, w, wt)) {
          readRate = reads.sample(r, rt); writeRate = writes.sample(w, wt);
          state = readRate >= 0 && writeRate >= 0 ? "ready" : "warming_up";
          error = 0;
        } else { state = "unavailable"; close(); retryAt = now + 30; }
      }
    }
    s.memoryRead = readRate; s.memoryWrite = writeRate;
    s.memoryBandwidthState = state; s.memoryBandwidthError = error;
  }
};
