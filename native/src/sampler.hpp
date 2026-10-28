#pragma once
#include "model.hpp"
#include "intel_telemetry.hpp"
#include "memory_bandwidth.hpp"
#include "fan_telemetry.hpp"
#include "memory_hardware.hpp"
#include "platform_hardware.hpp"
#include "app_activity.hpp"
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <pdh.h>
#include <psapi.h>
#include <set>
#include <shellapi.h>
using NtQuery = LONG(WINAPI *)(ULONG, PVOID, ULONG, PULONG);
struct NtString {
  USHORT Length, MaximumLength;
  PWSTR Buffer;
};
struct NtProcess {
  ULONG Next, Threads;
  BYTE Reserved[24];
  LARGE_INTEGER Created, User, Kernel;
  NtString Image;
  LONG Priority;
  HANDLE Pid, Parent;
  ULONG Handles, Session;
  ULONG_PTR Key;
  SIZE_T PeakVirtual, Virtual;
  ULONG Faults;
  SIZE_T PeakWorking, Working, PeakPaged, Paged, PeakNonPaged, NonPaged, Pagefile, PeakPagefile,
      Private;
  LARGE_INTEGER Io[6];
};
static_assert(offsetof(NtProcess, Working) == 144, "Windows x64 process ABI");
struct NtCore {
  LARGE_INTEGER Idle, Kernel, User, Dpc, Interrupt;
  ULONG Interrupts;
};
struct Nvml {
  HMODULE lib = nullptr;
  void *dev = nullptr;
  bool ok = false;
  double maxClock = -1;
  unsigned count = 0;
  std::string identity;
  struct Util {
    unsigned gpu, memory;
  };
  struct Mem {
    unsigned long long total, free, used;
  };
  struct MemV2 {
    unsigned version;
    unsigned long long total, reserved, free, used;
  };
  static_assert(sizeof(MemV2) == 40 && offsetof(MemV2, total) == 8, "NVML v2 ABI");
  template <class F> F fn(const char *name) {
    return reinterpret_cast<F>(lib ? GetProcAddress(lib, name) : nullptr);
  }
  Nvml() {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring p = std::wstring(sys) + L"\\nvml.dll";
    lib = LoadLibraryExW(p.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib)
      return;
    auto init = fn<int (*)()>("nvmlInit_v2");
    auto get = fn<int (*)(unsigned, void **)>("nvmlDeviceGetHandleByIndex_v2");
    ok = init && get && init() == 0 && get(0, &dev) == 0;
    auto getCount = fn<int (*)(unsigned *)>("nvmlDeviceGetCount_v2");
    if (ok && getCount && getCount(&count) != 0) count = 0;
    auto getName = fn<int (*)(void *, char *, unsigned)>("nvmlDeviceGetName");
    char name[128]{};
    if (ok && getName && getName(dev, name, sizeof(name)) == 0) identity = name;
    auto max = fn<int (*)(void *, unsigned, unsigned *)>("nvmlDeviceGetMaxClockInfo");
    unsigned mhz = 0;
    if (ok && max && max(dev, 0, &mhz) == 0 && mhz > 0) maxClock = mhz / 1000.;
  }
  ~Nvml() {
    if (ok) {
      auto close = fn<int (*)()>("nvmlShutdown");
      if (close)
        close();
    }
    if (lib)
      FreeLibrary(lib);
  }
  void read(Snapshot &s) {
    s.discreteGpuCount = ok ? count : 0;
    s.gpu = s.gpuMem = s.gpuTotal = s.gpuReserved = s.gpuTemp = s.gpuClock = s.gpuPower = -1;
    s.gpuMaxClock = ok ? maxClock : -1;
    if (!ok)
      return;
    auto u = fn<int (*)(void *, Util *)>("nvmlDeviceGetUtilizationRates");
    Util util{};
    if (u && u(dev, &util) == 0)
      s.gpu = util.gpu;
    auto m = fn<int (*)(void *, Mem *)>("nvmlDeviceGetMemoryInfo");
    auto m2 = fn<int (*)(void *, MemV2 *)>("nvmlDeviceGetMemoryInfo_v2");
    MemV2 mm2{};
    mm2.version = unsigned(sizeof(mm2)) | (2u << 24);
    Mem mm{};
    // v1 includes system-reserved memory in used. v2 separates it, as nvidia-smi does.
    if (m2 && m2(dev, &mm2) == 0 && mm2.total && mm2.used <= mm2.total && mm2.reserved <= mm2.total) {
      s.gpuMem = double(mm2.used);
      s.gpuTotal = double(mm2.total);
      s.gpuReserved = double(mm2.reserved);
    } else if (m && m(dev, &mm) == 0 && mm.total && mm.used <= mm.total) {
      s.gpuMem = double(mm.used);
      s.gpuTotal = double(mm.total);
    }
    unsigned v = 0;
    auto t = fn<int (*)(void *, unsigned, unsigned *)>("nvmlDeviceGetTemperature");
    if (t && t(dev, 0, &v) == 0)
      s.gpuTemp = v;
    auto c = fn<int (*)(void *, unsigned, unsigned *)>("nvmlDeviceGetClockInfo");
    if (c && c(dev, 0, &v) == 0)
      s.gpuClock = v / 1000.;
    auto p = fn<int (*)(void *, unsigned *)>("nvmlDeviceGetPowerUsage");
    if (p && p(dev, &v) == 0)
      s.gpuPower = v / 1000.;
  }
};
inline std::wstring description(const std::wstring &exe) {
  DWORD dummy = 0, n = GetFileVersionInfoSizeW(exe.c_str(), &dummy);
  if (!n || n > 8 * 1024 * 1024)
    return L"";
  std::vector<BYTE> b(n);
  if (!GetFileVersionInfoW(exe.c_str(), 0, n, b.data()))
    return L"";
  struct Lang {
    WORD lang, cp;
  };
  Lang *l = nullptr;
  UINT len = 0;
  if (!VerQueryValueW(b.data(), L"\\VarFileInfo\\Translation", (void **)&l, &len) ||
      len < sizeof(Lang))
    return L"";
  wchar_t key[100];
  swprintf(key, 100, L"\\StringFileInfo\\%04x%04x\\FileDescription", l[0].lang, l[0].cp);
  wchar_t *text = nullptr;
  if (VerQueryValueW(b.data(), key, (void **)&text, &len) && len > 1)
    return text;
  return L"";
}
struct Sampler {
  SRWLOCK lock = SRWLOCK_INIT;
  Snapshot latest;
  HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr),
         wake = CreateEventW(nullptr, FALSE, FALSE, nullptr), thread = nullptr;
  enum Demand : LONG { Cores = 1, Processes = 2, Memory = 4, Network = 8, GPU = 16, Power = 32, CPU = 64, Fans = 128, Disk = 256 };
  volatile LONG mode = 63, intervalMs = 1000;
  std::map<DWORD, Member> previous;
  std::map<std::wstring, std::pair<std::wstring, HICON>> metadata;
  std::vector<BYTE> processBuffer = std::vector<BYTE>(256 * 1024);
  std::map<ULONG64, std::pair<ULONG64, ULONG64>> network;
  NtQuery query = reinterpret_cast<NtQuery>(
      GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
  std::vector<NtCore> lastCores;
  double lastNetwork = 0;
  long long processCapacity = 0, previousCapacity = 0;
  bool totalValid = false;
  unsigned sequence = 0;
  NtCore lastTotal{};
  PDH_HQUERY pdh = nullptr, diskQuery = nullptr;
  PDH_HCOUNTER diskIdle = nullptr, diskReads = nullptr, diskWrites = nullptr;
  bool diskPrimed = false;
  std::vector<PDH_HCOUNTER> frequencies;
  std::vector<double> nominal;
  Nvml nv;
  AppActivity activity;
  volatile LONG overview = 0;
  IntelTelemetry sensors;
  MemoryBandwidth bandwidth;
  LenovoFans fans;
  HANDLE low = CreateMemoryResourceNotification(LowMemoryResourceNotification),
         high = CreateMemoryResourceNotification(HighMemoryResourceNotification);
  Sampler() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = si.dwNumberOfProcessors;
    nominal.assign(n, 2.6);
    for (int i = 0; i < n; ++i) {
      DWORD mhz = 0, size = 4;
      auto key = L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\" + std::to_wstring(i);
      if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"~MHz", RRF_RT_REG_DWORD, nullptr, &mhz,
                       &size) == ERROR_SUCCESS)
        nominal[i] = mhz / 1000.;
    }
    if (PdhOpenQueryW(nullptr, 0, &pdh) == ERROR_SUCCESS) {
      for (int i = 0; i < n; ++i) {
        PDH_HCOUNTER h = nullptr;
        auto path =
            L"\\Processor Information(0," + std::to_wstring(i) + L")\\% Processor Performance";
        PdhAddEnglishCounterW(pdh, path.c_str(), 0, &h);
        frequencies.push_back(h);
      }
      PdhCollectQueryData(pdh);
    }
    if (PdhOpenQueryW(nullptr, 0, &diskQuery) == ERROR_SUCCESS) {
      PdhAddEnglishCounterW(diskQuery, L"\\PhysicalDisk(_Total)\\% Idle Time", 0, &diskIdle);
      PdhAddEnglishCounterW(diskQuery, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &diskReads);
      PdhAddEnglishCounterW(diskQuery, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &diskWrites);
    }
    latest.cores.assign(n, -1);
    latest.ghz.assign(n, -1);
  }
  ~Sampler() {
    if (thread) {
      SetEvent(stop);
      WaitForSingleObject(thread, INFINITE);
      CloseHandle(thread);
    }
    CloseHandle(stop);
    CloseHandle(wake);
    if (pdh)
      PdhCloseQuery(pdh);
    if (diskQuery) PdhCloseQuery(diskQuery);
    if (low)
      CloseHandle(low);
    if (high)
      CloseHandle(high);
    for (auto &p : metadata)
      if (p.second.second)
        DestroyIcon(p.second.second);
  }
  void start() {
    thread = CreateThread(
        nullptr, 0,
        [](LPVOID p) -> DWORD {
          static_cast<Sampler *>(p)->run();
          return 0;
        },
        this, 0, nullptr);
  }
  static LONG demand(bool active, const Config &cfg) {
    LONG m = active ? Cores | Power | Fans | Disk : 0; // CPU temperature and cooling belong to the CPU block.
    if (active && cfg.has("processes")) m |= Processes;
    auto tray = cfg.get<bool>("menuBar", true) ? cfg.trayMetric() : "";
    if (active || tray == "cpu") m |= CPU;
    if ((active && cfg.has("memory")) || tray == "memory") m |= Memory;
    if ((active && cfg.has("network")) || tray == "download" || tray == "upload") m |= Network;
    if ((active && (cfg.has("gpu") || cfg.has("power"))) || tray == "gpu") m |= GPU;
    if (tray == "power") m |= Power;
    return m;
  }
  void settings(bool active, double interval, const Config &cfg) {
    LONG m = demand(active, cfg);
    LONG ms = LONG(interval * 1000);
    bool changed = InterlockedExchange(&intervalMs, ms) != ms;
    if (InterlockedExchange(&mode, m) != m)
      changed = true;
    if (changed)
      SetEvent(wake);
  }
  void situation(bool visible) {
    if(InterlockedExchange(&overview,visible?1:0)!=(visible?1:0)) SetEvent(wake);
  }
  Snapshot copy() {
    AcquireSRWLockShared(&lock);
    auto s = latest;
    ReleaseSRWLockShared(&lock);
    return s;
  }
  unsigned revision() {
    AcquireSRWLockShared(&lock);
    unsigned revision = latest.seq;
    ReleaseSRWLockShared(&lock);
    return revision;
  }
  static double utilization(const NtCore &a, const NtCore &b) {
    auto total = a.Kernel.QuadPart + a.User.QuadPart - b.Kernel.QuadPart - b.User.QuadPart;
    auto idle = a.Idle.QuadPart - b.Idle.QuadPart;
    return total > 0 ? bounded(100. * (total - idle) / total, 0., 100.) : -1;
  }
  void cpu(Snapshot &s, bool full) {
    if (full) {
      std::fill(s.cores.begin(), s.cores.end(), -1);
      std::fill(s.ghz.begin(), s.ghz.end(), -1);
    }
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) {
      totalValid = false; lastCores.clear(); s.cpu = -1; return;
    }
    auto to = [](FILETIME t) {
      return (static_cast<long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    NtCore t{};
    t.Idle.QuadPart = to(idle);
    t.Kernel.QuadPart = to(kernel);
    t.User.QuadPart = to(user);
    s.cpu = totalValid ? utilization(t, lastTotal) : -1;
    totalValid = true;
    lastTotal = t;
    if (!full)
      return;
    std::vector<NtCore> cs(nominal.size());
    ULONG size = 0;
    if (query && query(8, cs.data(), ULONG(cs.size() * sizeof(NtCore)), &size) >= 0) {
      if (lastCores.size() == cs.size())
        for (size_t i = 0; i < cs.size(); ++i)
          s.cores[i] = utilization(cs[i], lastCores[i]);
      lastCores = cs;
    } else lastCores.clear();
    if (pdh && PdhCollectQueryData(pdh) == ERROR_SUCCESS)
      for (size_t i = 0; i < frequencies.size(); ++i) {
        PDH_FMT_COUNTERVALUE v{};
        if (frequencies[i] &&
            PdhGetFormattedCounterValue(frequencies[i], PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr,
                                        &v) == ERROR_SUCCESS &&
            v.CStatus <= 1)
          s.ghz[i] = nominal[i] * v.doubleValue / 100.;
      }
  }
  void disk(Snapshot &s) {
    s.diskBusy = s.diskRead = s.diskWrite = -1;
    if (!diskQuery || PdhCollectQueryData(diskQuery) != ERROR_SUCCESS) {
      diskPrimed = false; return;
    }
    bool ready = diskPrimed;
    diskPrimed = true;
    if (!ready) return;
    auto read = [](PDH_HCOUNTER counter) {
      PDH_FMT_COUNTERVALUE v{};
      return counter && PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100,
               nullptr, &v) == ERROR_SUCCESS && v.CStatus <= 1 && std::isfinite(v.doubleValue) && v.doubleValue >= 0
                 ? v.doubleValue : -1.;
    };
    double idle = read(diskIdle);
    if (idle >= 0) s.diskBusy = 100 - bounded(idle, 0., 100.);
    s.diskRead = read(diskReads); s.diskWrite = read(diskWrites);
  }
  void memory(Snapshot &s) {
    s.used = s.total = s.swap = -1;
    s.pressure = -1;
    MEMORYSTATUSEX m{};
    m.dwLength = sizeof(m);
    if (GlobalMemoryStatusEx(&m)) {
      s.total = double(m.ullTotalPhys);
      s.used = double(m.ullTotalPhys - m.ullAvailPhys);
    }
    BOOL lo = FALSE, hi = FALSE;
    if (low && high && QueryMemoryResourceNotification(low, &lo) &&
        QueryMemoryResourceNotification(high, &hi))
      s.pressure = lo ? 2 : hi ? 0 : 1;
    // Pagefile usage comes from the system pagefile counters, not committed
    // memory.
    struct Swap {
      double bytes = 0;
      DWORD pageSize = 4096;
    } swap;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    swap.pageSize = si.dwPageSize;
    if (EnumPageFilesW(
        [](LPVOID ctx, PENUM_PAGE_FILE_INFORMATION i, LPCWSTR) -> BOOL {
          static_cast<Swap *>(ctx)->bytes +=
              double(i->TotalInUse) * static_cast<Swap *>(ctx)->pageSize;
          return TRUE;
        },
        &swap)) s.swap = swap.bytes;
  }
  void net(Snapshot &s, double now) {
    s.down = s.up = -1;
    const double before = seconds();
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR) {
      network.clear(); lastNetwork = 0;
      return;
    }
    now = (before + seconds()) * .5;
    std::map<ULONG64, std::pair<ULONG64, ULONG64>> cur;
    double down = 0, up = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
      auto &r = table->Table[i];
      auto desc = lower(r.Description);
      auto alias = lower(r.Alias);
      bool virtualInterface = false;
      for (auto token : {L"loopback", L"vethernet", L"vmware", L"virtualbox", L"teredo", L"isatap",
                         L"bluetooth", L"tun", L"tap", L"clash", L"mihomo"})
        if (alias.find(token) != std::wstring::npos)
          virtualInterface = true;
      if (r.OperStatus != IfOperStatusUp || r.Type == IF_TYPE_SOFTWARE_LOOPBACK ||
          virtualInterface || r.Type == IF_TYPE_TUNNEL ||
          desc.find(L"virtual") != std::wstring::npos ||
          desc.find(L"loopback") != std::wstring::npos)
        continue;
      auto id = r.InterfaceLuid.Value;
      cur[id] = {r.InOctets, r.OutOctets};
      auto old = network.find(id);
      if (old != network.end()) {
        if (r.InOctets >= old->second.first)
          down += double(r.InOctets - old->second.first);
        if (r.OutOctets >= old->second.second)
          up += double(r.OutOctets - old->second.second);
      }
    }
    FreeMibTable(table);
    if (lastNetwork && now - lastNetwork > 0) {
      s.down = down / (now - lastNetwork);
      s.up = up / (now - lastNetwork);
    }
    network = std::move(cur);
    lastNetwork = now;
  }
  std::vector<Member> processRows() {
    std::vector<Member> rows;
    ULONG size = 0;
    LONG status = -1;
    for (int t = 0; t < 8; ++t) {
      status = query ? query(5, processBuffer.data(), ULONG(processBuffer.size()), &size) : -1;
      if (status >= 0)
        break;
      if (ULONG(status) != 0xC0000004)
        return rows;
      size_t need = std::max(processBuffer.size() * 2, size_t(size) + 65536);
      if (need > 128 * 1024 * 1024)
        return rows;
      processBuffer.resize(need);
    }
    if (status < 0)
      return rows;
    // Pair the process snapshot with total processor capacity before metadata work.
    // Save this baseline only with a successful process snapshot.
    FILETIME idle{}, kernel{}, user{};
    if (GetActiveProcessorGroupCount() != 1 || !GetSystemTimes(&idle, &kernel, &user))
      return rows;
    processCapacity = (static_cast<long long>(kernel.dwHighDateTime) << 32) + kernel.dwLowDateTime +
                      (static_cast<long long>(user.dwHighDateTime) << 32) + user.dwLowDateTime;
    size_t limit = std::min(size_t(size), processBuffer.size()), off = 0;
    while (off + sizeof(NtProcess) <= limit) {
      auto *r = reinterpret_cast<const NtProcess *>(processBuffer.data() + off);
      Member m;
      m.pid = DWORD(reinterpret_cast<uintptr_t>(r->Pid));
      m.parent = DWORD(reinterpret_cast<uintptr_t>(r->Parent));
      m.created = r->Created.QuadPart;
      m.cpuTime = r->User.QuadPart + r->Kernel.QuadPart;
      m.bytes = double(r->Working);
      auto name = reinterpret_cast<uintptr_t>(r->Image.Buffer),
           base = reinterpret_cast<uintptr_t>(processBuffer.data());
      if (r->Image.Length && name >= base && name + r->Image.Length <= base + limit &&
          r->Image.Length % 2 == 0)
        m.name.assign(r->Image.Buffer, r->Image.Length / 2);
      else
        m.name = m.pid == 4 ? L"System" : std::to_wstring(m.pid);
      if (m.pid) {
        auto old = previous.find(m.pid);
        if (old != previous.end() && old->second.created == m.created) {
          m.exe = old->second.exe;
          m.display = old->second.display;
        }
        if (m.exe.empty()) {
          HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m.pid);
          if (p) {
            wchar_t path[32768];
            DWORD len = 32768;
            if (QueryFullProcessImageNameW(p, 0, path, &len))
              m.exe.assign(path, len);
            CloseHandle(p);
          }
        }
        rows.push_back(std::move(m));
      }
      if (!r->Next)
        break;
      if (r->Next < sizeof(NtProcess) || off + r->Next >= limit)
        break;
      off += r->Next;
    }
    return rows;
  }
  void processes(Snapshot &s) {
    auto rows = processRows();
    if (rows.empty()) {
      previous.clear(); previousCapacity = 0;
      for (auto &g : s.groups) {
        g.cpu = -1;
        for (auto &m : g.members) m.cpu = -1;
      }
      return;
    }
    const long long capacity = previousCapacity ? processCapacity - previousCapacity : 0;
    std::map<std::wstring, Group> groups;
    std::map<DWORD, Member> current;
    std::map<DWORD, Member *> byPid;
    for (auto &m : rows)
      byPid[m.pid] = &m;
    for (auto &m : rows) {
      auto old = previous.find(m.pid);
      if (old != previous.end() && old->second.created == m.created &&
          m.cpuTime >= old->second.cpuTime)
        m.cpu = telemetry::cpuPercent(m.cpuTime, old->second.cpuTime, capacity);
      auto *owner = &m;
      auto *cursor = &m;
      std::set<DWORD> visited;
      auto path = lower(m.exe);
      auto slash = path.find_last_of(L"\\");
      auto directory = slash == std::wstring::npos ? L"" : path.substr(0, slash);
      auto filename = slash == std::wstring::npos ? path : path.substr(slash + 1);
      bool helper = filename.find(L"helper") != std::wstring::npos ||
                    filename.find(L"webview") != std::wstring::npos ||
                    filename.find(L"qtwebengine") != std::wstring::npos ||
                    filename.find(L"crashpad") != std::wstring::npos;
      while (byPid.count(cursor->parent) && !visited.count(cursor->parent)) {
        visited.insert(cursor->parent);
        auto *parent = byPid[cursor->parent];
        auto parentPath = lower(parent->exe);
        auto split = parentPath.find_last_of(L"\\");
        auto parentDir = split == std::wstring::npos ? L"" : parentPath.substr(0, split);
        bool same = parentPath == path ||
                    (helper && !parentDir.empty() &&
                     (directory == parentDir || directory.rfind(parentDir + L"\\", 0) == 0));
        if (directory.empty() || parentPath.empty() || !same)
          break;
        owner = parent;
        cursor = parent;
      }
      auto key = owner->exe.empty() ? L"pid:" + std::to_wstring(owner->pid) + L":" +
                                          std::to_wstring(owner->created)
                                    : lower(owner->exe);
      auto &g = groups[key];
      g.key = key;
      g.exe = owner->exe;
      g.name = owner->name;
      if (m.cpu >= 0) g.cpu = std::max(0., g.cpu) + m.cpu;
      g.bytes += m.bytes;
      g.members.push_back(m);
      current[m.pid] = m;
    }

    previous = std::move(current);
    previousCapacity = processCapacity;
    std::vector<Group> out;
    for (auto &p : groups)
      out.push_back(std::move(p.second));
    // Resolve shell metadata only for likely visible groups; cache stable
    // paths.
    std::vector<const Group *> top;
    top.reserve(out.size());
    for (const auto &g : out) top.push_back(&g);
    std::sort(top.begin(), top.end(), [](auto a, auto b) { return a->cpu > b->cpu; });
    std::set<std::wstring> wanted;
    for (size_t i = 0; i < std::min(size_t(48), top.size()); ++i)
      wanted.insert(top[i]->key);
    std::sort(top.begin(), top.end(), [](auto a, auto b) { return a->bytes > b->bytes; });
    for (size_t i = 0; i < std::min(size_t(48), top.size()); ++i)
      wanted.insert(top[i]->key);
    int budget = 12;
    for (auto &g : out) {
      if (!g.exe.empty() && !metadata.count(g.key) && wanted.count(g.key) &&
          metadata.size() < 384 && budget-- > 0) {
        SHFILEINFOW info{};
        SHGetFileInfoW(g.exe.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_SMALLICON | SHGFI_SYSICONINDEX);
        static const int genericIcon = [] {
          SHFILEINFOW fallback{};
          return SHGetFileInfoW(L"ftop-unassigned.exe", FILE_ATTRIBUTE_NORMAL, &fallback, sizeof(fallback),
            SHGFI_USEFILEATTRIBUTES | SHGFI_SMALLICON | SHGFI_SYSICONINDEX) ? fallback.iIcon : -1;
        }();
        if (info.hIcon && info.iIcon == genericIcon) { DestroyIcon(info.hIcon); info.hIcon = nullptr; }
        metadata[g.key] = {description(g.exe), info.hIcon};
      }
      auto p = metadata.find(g.key);
      if (p != metadata.end()) {
        if (!p->second.first.empty())
          g.name = p->second.first;
        g.icon = p->second.second;
      }
      if (lower(g.name).find(L"chatgpt") != std::wstring::npos &&
          lower(g.exe).find(L"codex") != std::wstring::npos)
        g.name = L"Codex";
    }
    // Icons stay valid until the worker stops; cap the cache at 384 paths.
    s.groups = std::move(out);
  }
  void run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Snapshot hardware;
    readMemoryHardware(hardware);
    PlatformProbe platform;
    platform.initialize();
    for (;;) {
      double now = seconds();
      LONG wanted = InterlockedCompareExchange(&mode, 0, 0);
      bool appResources = InterlockedCompareExchange(&overview,0,0)!=0;
      if(appResources) wanted |= Processes|GPU|Disk|Network|Memory|CPU;
      else activity.pause();
      if (!wanted) {
        sensors.pause();
        bandwidth.pause();
        fans.pause();
        diskPrimed = false;
        previous.clear();
        previousCapacity = 0;
        totalValid = false;
        lastCores.clear();
        lastNetwork = 0;
        network.clear();
        HANDLE events[] = {stop, wake};
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0) break;
        continue;
      }
      auto s = copy();
      s.memoryModules = hardware.memoryModules;
      platform.read(s, (wanted & Cores) != 0);
      s.powerHardware = hardware.powerHardware.empty() ? "" : hardware.powerHardware + ":" + std::to_string(nv.count) + ":" + nv.identity;
      s.when = now;
      if (wanted & CPU) cpu(s, (wanted & Cores) != 0);
      else { s.cpu = -1; totalValid = false; }
      if (wanted & Processes) processes(s);
      else {
        previous.clear();
        previousCapacity = 0;
        s.groups.clear();
      }
      if(appResources) activity.read(s);
      else { s.appGpuTotal=s.appNetTotal=-1; s.appIoState=nullptr; }
      if (wanted & Disk) disk(s);
      else { diskPrimed = false; s.diskBusy = s.diskRead = s.diskWrite = -1; }
      if (wanted & Memory) memory(s);
      else s.used = s.total = s.swap = -1;
      if ((wanted & Memory) && (wanted & Cores)) bandwidth.read(s, sensors);
      else {
        bandwidth.pause(); s.memoryRead = s.memoryWrite = -1;
        s.memoryBandwidthState = "paused"; s.memoryBandwidthError = 0;
      }
      if (wanted & GPU) nv.read(s);
      else s.gpu = s.gpuMem = s.gpuTotal = s.gpuReserved = s.gpuTemp = s.gpuClock = s.gpuMaxClock = s.gpuPower = -1;
      if (wanted & Power) sensors.read(s);
      else {
        sensors.pause();
          s.cpuPower = s.cpuTemp = s.memoryPower = -1;
          s.memoryPowerState = "paused"; s.memoryPowerError = 0;
        s.sensorState = "paused";
        s.sensorMs = 0;
      }
      if (wanted & Fans) fans.read(s.fans);
      else { fans.pause(); s.fans = fans.cached; }
      if (!(wanted & Cores)) {
        lastCores.clear();
        std::fill(s.cores.begin(), s.cores.end(), -1);
        std::fill(s.ghz.begin(), s.ghz.end(), -1);
      }
      if (wanted & Network) net(s, now);
      else {
        s.down = s.up = -1;
        lastNetwork = 0;
        network.clear();
      }
      s.seq = ++sequence;
      s.cost = (seconds() - now) * 1000;
      AcquireSRWLockExclusive(&lock);
      latest = std::move(s);
      ReleaseSRWLockExclusive(&lock);
      HANDLE events[] = {stop, wake};
      DWORD wait = WaitForMultipleObjects(2, events, FALSE, DWORD(intervalMs));
      if (wait == WAIT_OBJECT_0)
        break;
    }
    activity.pause();
    fans.close();
    platform.close();
    CoUninitialize();
  }
};
