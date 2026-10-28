#pragma once
#include "model.hpp"
#include "telemetry_math.hpp"
#include "sensor_resources.hpp"
#include <cpuid.h>
#include <cstring>

// Independent Win32 client of PawnIO's public device protocol. No PawnIOLib DLL.
// Only five read-only MSRs are exposed here. The official signed IntelMSR module
// enforces its own allowlist and is authenticated by the signed driver.
struct IntelTelemetry {
  HANDLE device = INVALID_HANDLE_VALUE;
  telemetry::Energy energy, dramEnergy;
  double target = -1, unit = -1, nextRead = 0, retryAt = 0, nextLimits = 0;
  double temp = -1, power = -1, dramPower = -1, readMs = 0;
  DWORD dramError = 0;
  std::string dramState = "not_sampled";
  DWORD error = 0;
  uint64_t thermalRaw = 0, targetRaw = 0, unitRaw = 0;
  DWORD_PTR affinity = 0;
  unsigned model = 0;
  bool supported = false, packageThermal = false;
  std::string state = "not_sampled";
  IntelTelemetry() {
    unsigned a, b, c, d;
    __cpuid(0, a, b, c, d);
    char vendor[13]{};
    memcpy(vendor, &b, 4); memcpy(vendor + 4, &d, 4); memcpy(vendor + 8, &c, 4);
    if (strcmp(vendor, "GenuineIntel") || a < 6) return;
    __cpuid(1, a, b, c, d);
    model = ((a >> 4) & 15) | ((a >> 12) & 240);
    if (((a >> 8) & 15) != 6 || (model != 0xa5 && model != 0xa6)) return;
    __cpuid(6, a, b, c, d);
    packageThermal = (a & (1u << 6)) != 0;
    DWORD_PTR system = 0, process = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process, &system) || !process) return;
    affinity = process & (~process + 1);
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationProcessorPackage, nullptr, &size);
    std::vector<BYTE> info(size);
    if (!size || !GetLogicalProcessorInformationEx(RelationProcessorPackage,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(info.data()), &size)) return;
    unsigned packages = 0;
    for (DWORD off = 0; off < size;) {
      auto p = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(info.data() + off);
      if (!p->Size || off + p->Size > size) return;
      ++packages; off += p->Size;
    }
    supported = packages == 1 && GetActiveProcessorGroupCount() == 1;
  }
  ~IntelTelemetry() { close(); }
  IntelTelemetry(const IntelTelemetry &) = delete;
  IntelTelemetry &operator=(const IntelTelemetry &) = delete;
  void close() {
    if (device != INVALID_HANDLE_VALUE) CloseHandle(device);
    device = INVALID_HANDLE_VALUE;
    energy.reset(); dramEnergy.reset(); dramPower = -1; target = unit = -1; nextLimits = 0;
  }
  bool readMsr(uint32_t index, uint64_t &value) {
    if (index != 0x1a2 && index != 0x1b1 && index != 0x606 && index != 0x611 && index != 0x619) return false;
    struct Request { char name[32]; uint64_t index; } request{};
    static_assert(sizeof(Request) == 40, "PawnIO wire format");
    memcpy(request.name, "ioctl_read_msr", 14); request.index = index;
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(device, (41394u << 16) | (0x841u << 2),
        &request, sizeof(request), &value, sizeof(value), &returned, nullptr);
    if (!ok || returned != sizeof(value)) {
      error = ok ? ERROR_INVALID_DATA : GetLastError(); return false;
    }
    return true;
  }
  bool open() {
    device = CreateFileW(L"\\\\?\\GLOBALROOT\\Device\\PawnIO", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
      error = GetLastError(); state = error == ERROR_ACCESS_DENIED ? "access_denied" : "driver_unavailable";
      return false;
    }
    const auto bytes = sensorResources::get(sensorResources::module);
    const bool loaded = bytes.data && bytes.size && bytes.size <= 65536;
    DWORD returned = 0;
    if (!loaded || !DeviceIoControl(device, (41394u << 16) | (0x821u << 2),
        const_cast<void *>(bytes.data), bytes.size, nullptr, 0, &returned, nullptr)) {
      error = loaded ? GetLastError() : ERROR_INVALID_DATA;
      state = "module_rejected"; close(); return false;
    }
    return true;
  }
  void pause() {
    energy.reset(); dramEnergy.reset(); nextRead = 0; temp = power = dramPower = -1;
    dramState = "paused"; dramError = 0;
  }
  void read(Snapshot &s) {
    const double now = seconds();
    if (now >= nextRead) {
      nextRead = now + 1.;
      temp = power = dramPower = -1;
      dramState = "unavailable"; dramError = 0;
      if (!supported) state = "unsupported_cpu";
      else if (device != INVALID_HANDLE_VALUE || now >= retryAt) {
        const DWORD_PTR old = SetThreadAffinityMask(GetCurrentThread(), affinity);
        if (!old) { error = GetLastError(); state = "affinity_failed"; }
        else {
          if (device != INVALID_HANDLE_VALUE || open()) {
            uint64_t value = 0;
            if (now >= nextLimits) {
              if (packageThermal && (target < 50 || target > 125) && readMsr(0x1a2, value)) {
                targetRaw = value;
                target = double((value >> 16) & 255);
              }
              if (unit <= 0 && readMsr(0x606, value)) {
                unitRaw = value; unit = std::ldexp(1., -int((value >> 8) & 31));
              }
              nextLimits = now + 30.;
            }
            bool thermalOk = false, energyOk = false;
            thermalRaw = 0;
            if (target > 0 && readMsr(0x1b1, value)) {
              thermalRaw = value;
              temp = telemetry::temperature(value, target); thermalOk = temp >= 0;
            }
            const double before = seconds();
            if (unit > 0 && readMsr(0x611, value)) {
              power = energy.sample(uint32_t(value), (before + seconds()) * .5, unit);
              energyOk = true;
            } else energy.reset();
            state = thermalOk && energyOk ? (power < 0 ? "warming_up" : "ready") : "partial_or_failed";
            if (!thermalOk && !energyOk) close();
            else if (thermalOk && energyOk) error = 0;
            // Optional DRAM domain: failure must not disable CPU telemetry.
            // Comet Lake uses the RAPL energy unit for this domain as well.
            const DWORD cpuError = error;
            const double dramBefore = seconds();
            if (device != INVALID_HANDLE_VALUE && unit > 0 && readMsr(0x619, value)) {
              dramPower = dramEnergy.sample(uint32_t(value), (dramBefore + seconds()) * .5, unit);
              dramState = dramPower < 0 ? "warming_up" : "ready";
            } else {
              dramError = error ? error : ERROR_NOT_SUPPORTED;
              dramEnergy.reset();
            }
            error = cpuError;
          }
          SetThreadAffinityMask(GetCurrentThread(), old);
        }
        retryAt = now + 30.;
      }
      if (dramState == "unavailable") dramEnergy.reset();
      readMs = (seconds() - now) * 1000.;
    }
    s.cpuTemp = temp; s.cpuPower = power; s.sensorMs = readMs;
    s.sensorState = state; s.sensorError = error;
    s.memoryPower = dramPower; s.memoryPowerState = dramState; s.memoryPowerError = dramError;
  }
};
