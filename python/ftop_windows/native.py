"""Windows API adapters. No UI dependencies; unavailable sensors return None."""

import ctypes
from ctypes import wintypes
import os
import struct
import sys
import traceback
import time
from pathlib import Path

CONFIG_DIR = str(Path(os.environ.get("APPDATA", str(Path.home()))) / "ftop")
LOG_PATH = str(Path(CONFIG_DIR) / "ftop.log")
RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
_logged = set()
user32 = ctypes.windll.user32
dwmapi = ctypes.windll.dwmapi
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

user32.GetAncestor.restype = wintypes.HWND
user32.GetAncestor.argtypes = [wintypes.HWND, wintypes.UINT]
user32.WindowFromPoint.restype = wintypes.HWND
user32.WindowFromPoint.argtypes = [wintypes.POINT]
user32.SetWindowPos.argtypes = [
    wintypes.HWND,
    wintypes.HWND,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_int,
    wintypes.UINT,
]
user32.IsWindowVisible.argtypes = [wintypes.HWND]
user32.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
user32.SystemParametersInfoW.argtypes = [
    wintypes.UINT,
    wintypes.UINT,
    ctypes.c_void_p,
    wintypes.UINT,
]
user32.GetWindowLongW.argtypes = [wintypes.HWND, ctypes.c_int]
dwmapi.DwmGetWindowAttribute.argtypes = [
    wintypes.HWND,
    wintypes.DWORD,
    ctypes.c_void_p,
    wintypes.DWORD,
]
dwmapi.DwmSetWindowAttribute.argtypes = [
    wintypes.HWND,
    wintypes.DWORD,
    ctypes.c_void_p,
    wintypes.DWORD,
]

kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

GWL_EXSTYLE, WS_EX_TOOLWINDOW = -20, 0x80


def log_error(msg, once=False):
    """pythonw 下没有控制台，错误写到 %APPDATA%\\ftop\\ftop.log。once=True 时同一条只记一次。"""
    try:
        if once:
            if msg in _logged:
                return
            _logged.add(msg)
        os.makedirs(CONFIG_DIR, exist_ok=True)
        if os.path.exists(LOG_PATH) and os.path.getsize(LOG_PATH) > 262144:
            os.remove(LOG_PATH)
        with open(LOG_PATH, "a", encoding="utf-8") as f:
            f.write(time.strftime("%Y-%m-%d %H:%M:%S ") + msg.rstrip() + "\n")
    except Exception:
        pass


class ACCENT_POLICY(ctypes.Structure):
    _fields_ = [
        ("AccentState", ctypes.c_int),
        ("AccentFlags", ctypes.c_int),
        ("GradientColor", ctypes.c_int),
        ("AnimationId", ctypes.c_int),
    ]


class WCAD(ctypes.Structure):
    _fields_ = [
        ("Attribute", ctypes.c_int),
        ("Data", ctypes.c_void_p),
        ("SizeOfData", ctypes.c_size_t),
    ]


def _apply_accent(hwnd, state, flags=0, abgr=0):
    try:
        a = ACCENT_POLICY(state, flags, abgr, 0)
        d = WCAD(19, ctypes.cast(ctypes.byref(a), ctypes.c_void_p), ctypes.sizeof(a))
        user32.SetWindowCompositionAttribute.argtypes = [
            wintypes.HWND,
            ctypes.POINTER(WCAD),
        ]
        user32.SetWindowCompositionAttribute.restype = wintypes.BOOL
        return bool(user32.SetWindowCompositionAttribute(hwnd, ctypes.byref(d)))
    except Exception:
        return False


def apply_acrylic(hwnd, abgr=0xEE1D1312, *, enabled=True):
    """Legacy diagnostic path. Production uses region-compatible blur on Win10."""
    return _apply_accent(hwnd, 4, 2, abgr) if enabled else _apply_accent(hwnd, 0)


def apply_blur(hwnd):
    """Win10 blur behind respects rounded regions, unlike acrylic's tint layer."""
    return _apply_accent(hwnd, 3)


def animations_enabled():
    """系统"显示动画"开关（SPI_GETCLIENTAREAANIMATION）。关闭时对应 macOS 的 Reduce Motion。"""
    try:
        v = wintypes.BOOL()
        if user32.SystemParametersInfoW(0x1042, 0, ctypes.byref(v), 0):
            return bool(v.value)
    except Exception:
        pass
    return True


# ======================= 开机自启 =======================
def autostart_command():
    if getattr(sys, "frozen", False):
        return f'"{sys.executable}"'
    exe = sys.executable
    pyw = os.path.join(os.path.dirname(exe), "pythonw.exe")
    if os.path.basename(exe).lower() == "python.exe" and os.path.exists(pyw):
        exe = pyw  # 不弹控制台窗口
    return f'"{exe}" "{str(Path(__file__).resolve().parent.parent / "ftop_win.py")}"'


def autostart_enabled():
    try:
        import winreg

        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as k:
            winreg.QueryValueEx(k, "ftop")
            return True
    except Exception:
        return False


def set_autostart(on):
    try:
        import winreg

        with winreg.CreateKeyEx(
            winreg.HKEY_CURRENT_USER, RUN_KEY, 0, winreg.KEY_SET_VALUE
        ) as k:
            if on:
                winreg.SetValueEx(k, "ftop", 0, winreg.REG_SZ, autostart_command())
            else:
                try:
                    winreg.DeleteValue(k, "ftop")
                except FileNotFoundError:
                    pass
    except Exception:
        log_error("set_autostart failed:\n" + traceback.format_exc())


class _NvUtil(ctypes.Structure):
    _fields_ = [("gpu", ctypes.c_uint), ("memory", ctypes.c_uint)]


class _NvMem(ctypes.Structure):
    _fields_ = [
        ("total", ctypes.c_ulonglong),
        ("free", ctypes.c_ulonglong),
        ("used", ctypes.c_ulonglong),
    ]


class Nvml:
    """NVIDIA 显卡：温度 / 使用率 / 显存 / 功耗。没有 NVIDIA 驱动时 ok=False，面板直接不显示 GPU。"""

    def __init__(self):
        self.ok = False
        self.handle = None
        self.name = None
        self.lib = None
        for p in (
            r"C:\Windows\System32\nvml.dll",
            r"C:\Program Files\NVIDIA Corporation\NVSMI\nvml.dll",
        ):
            if not os.path.exists(p):
                continue
            try:
                self.lib = ctypes.CDLL(p)
                if self.lib.nvmlInit_v2() != 0:
                    continue
                h = ctypes.c_void_p()
                if self.lib.nvmlDeviceGetHandleByIndex_v2(0, ctypes.byref(h)) == 0:
                    self.handle, self.ok = h, True
                    buf = ctypes.create_string_buffer(96)
                    if self.lib.nvmlDeviceGetName(h, buf, 96) == 0:
                        self.name = buf.value.decode("utf-8", "replace")
                    return
            except Exception:
                pass

    def read(self):
        r = dict(
            temp=None, util=None, mem_used=None, mem_total=None, power=None, clock=None
        )
        if not self.ok:
            return r
        lib, h = self.lib, self.handle
        try:
            t = ctypes.c_uint()
            if lib.nvmlDeviceGetTemperature(h, 0, ctypes.byref(t)) == 0:
                r["temp"] = t.value
            u = _NvUtil()
            if lib.nvmlDeviceGetUtilizationRates(h, ctypes.byref(u)) == 0:
                r["util"] = u.gpu
            m = _NvMem()
            if lib.nvmlDeviceGetMemoryInfo(h, ctypes.byref(m)) == 0:
                r["mem_used"], r["mem_total"] = m.used / 1024**3, m.total / 1024**3
            clock = ctypes.c_uint()
            if lib.nvmlDeviceGetClockInfo(h, 0, ctypes.byref(clock)) == 0:
                r["clock"] = clock.value / 1000.0
            p = ctypes.c_uint()
            if lib.nvmlDeviceGetPowerUsage(h, ctypes.byref(p)) == 0:
                r["power"] = p.value / 1000.0
        except Exception:
            pass
        return r


# ======================= CPU 拓扑（P/E 核、SMT） =======================
def parse_core_records(raw, total):
    """解析 GetLogicalProcessorInformationEx(RelationProcessorCore) 返回的缓冲区。
    返回 [(效率等级, 是否SMT, [逻辑核编号...]), ...]。"""
    cores, off = [], 0
    while off + 8 <= min(total, len(raw)):
        rel, size = struct.unpack_from("<II", raw, off)
        if size < 8 or off + size > min(total, len(raw)):
            break
        if rel == 0 and size >= 32:  # RelationProcessorCore
            flags, eff = raw[off + 8], raw[off + 9]
            gcount = struct.unpack_from("<H", raw, off + 30)[0]
            idx = []
            for g in range(
                min(gcount, (size - 32) // 16)
            ):  # GROUP_AFFINITY：8 字节掩码 + 2 字节组号 + 保留
                mask, grp = struct.unpack_from("<QH", raw, off + 32 + g * 16)
                idx += [grp * 64 + b for b in range(64) if (mask >> b) & 1]
            cores.append((eff, bool(flags & 1), sorted(idx)))
        off += size
    return cores


def read_topology(n):
    """返回 (cores, 来源)。效率等级越大越强（Intel 混合架构上 P 核 > E 核）。"""
    try:
        f = kernel32.GetLogicalProcessorInformationEx
        f.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
        f.restype = wintypes.BOOL
        need = wintypes.DWORD(0)
        f(0, None, ctypes.byref(need))
        if need.value:
            buf = ctypes.create_string_buffer(need.value)
            if f(0, buf, ctypes.byref(need)):
                cores = parse_core_records(buf.raw, need.value)
                if sorted(i for c in cores for i in c[2]) == list(range(n)):
                    return cores, "GetLogicalProcessorInformationEx"
    except Exception:
        log_error("read_topology failed:\n" + traceback.format_exc(), once=True)
    return [(0, False, [i]) for i in range(n)], "fallback(flat)"


def make_layout(cores):
    """把核心排成绘制顺序：强核组在前；每个核的线程相邻。
    order 元素 = (逻辑核, 组号, 核键, 线程序号)；info[逻辑核] 供悬停提示使用。"""
    classes = sorted({c[0] for c in cores}, reverse=True)
    names = [""] if len(classes) == 1 else (["P", "E"] + ["LP"] * 8)[: len(classes)]
    order, info = [], {}
    for gi, cls in enumerate(classes):
        group = sorted((c for c in cores if c[0] == cls), key=lambda c: c[2][0])
        for cn, (_, _, idx) in enumerate(group, 1):
            for t, i in enumerate(idx, 1):
                order.append((i, gi, (gi, cn), t))
                info[i] = {
                    "name": names[gi],
                    "core": cn,
                    "thread": t,
                    "threads": len(idx),
                }
    return order, info


def nominal_ghz(n, fallback):
    """每个逻辑核的标称频率（注册表 ~MHz）；读不到就都用 fallback。"""
    try:
        import winreg

        out = []
        for i in range(n):
            with winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                rf"HARDWARE\DESCRIPTION\System\CentralProcessor\{i}",
            ) as k:
                out.append(winreg.QueryValueEx(k, "~MHz")[0] / 1000.0)
        if len(out) == n and all(v > 0 for v in out):
            return out
    except Exception:
        pass
    return [fallback] * n


# ======================= PDH 每核频率 =======================
class PDH_VALUE(ctypes.Structure):
    _fields_ = [("CStatus", wintypes.DWORD), ("v", ctypes.c_double)]


class Pdh:
    """% Processor Performance × 每核标称频率。用 English 计数器名，中文系统也可用。"""

    def __init__(self, n, nominal):
        self.n, self.nominal = n, list(nominal)
        self.avg = sum(self.nominal) / max(len(self.nominal), 1)
        self.ok = False
        self.h_tot = None
        self.h = []
        try:
            p = ctypes.windll.pdh
            self.p = p
            vp = ctypes.c_void_p
            p.PdhOpenQueryW.argtypes = [
                wintypes.LPCWSTR,
                ctypes.c_size_t,
                ctypes.POINTER(vp),
            ]
            p.PdhAddEnglishCounterW.argtypes = [
                vp,
                wintypes.LPCWSTR,
                ctypes.c_size_t,
                ctypes.POINTER(vp),
            ]
            p.PdhCollectQueryData.argtypes = [vp]
            p.PdhGetFormattedCounterValue.argtypes = [
                vp,
                wintypes.DWORD,
                ctypes.c_void_p,
                ctypes.POINTER(PDH_VALUE),
            ]
            self.q = vp()
            if p.PdhOpenQueryW(None, 0, ctypes.byref(self.q)) != 0:
                return
            t = vp()
            if (
                p.PdhAddEnglishCounterW(
                    self.q,
                    r"\Processor Information(_Total)\% Processor Performance",
                    0,
                    ctypes.byref(t),
                )
                != 0
            ):
                return
            self.h_tot = t
            for i in range(n):  # 实例名 "组,组内编号"，>64 线程的机器也能对上
                h = vp()
                st = p.PdhAddEnglishCounterW(
                    self.q,
                    rf"\Processor Information({i // 64},{i % 64})\% Processor Performance",
                    0,
                    ctypes.byref(h),
                )
                self.h.append(h if st == 0 else None)
            p.PdhCollectQueryData(self.q)
            self.ok = True
        except Exception:
            self.ok = False

    def _val(self, h, base):
        if h is None or base <= 0:
            return None
        v = PDH_VALUE()
        if self.p.PdhGetFormattedCounterValue(
            h, 0x200 | 0x8000, None, ctypes.byref(v)
        ) == 0 and v.CStatus in (0, 1):
            return round(v.v * base / 100.0, 2)
        return None

    def query(self):
        if not self.ok:
            return None, [None] * self.n
        try:
            self.p.PdhCollectQueryData(self.q)
            return (
                self._val(self.h_tot, self.avg),
                [self._val(h, b) for h, b in zip(self.h, self.nominal)],
            )
        except Exception:
            return None, [None] * self.n

    def close(self):
        if getattr(self, "q", None):
            self.p.PdhCloseQuery.argtypes = [ctypes.c_void_p]
            self.p.PdhCloseQuery(self.q)
            self.q = None
        self.ok = False


# Memory pressure uses Windows' memory resource notifications, not a RAM threshold.
_pressure_handles = None


def memory_pressure():
    global _pressure_handles
    k = kernel32
    k.CreateMemoryResourceNotification.argtypes = [ctypes.c_int]
    k.CreateMemoryResourceNotification.restype = wintypes.HANDLE
    k.QueryMemoryResourceNotification.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(wintypes.BOOL),
    ]
    if _pressure_handles is None:
        _pressure_handles = (
            k.CreateMemoryResourceNotification(0),
            k.CreateMemoryResourceNotification(1),
        )
    low, high = wintypes.BOOL(), wintypes.BOOL()
    if not all(_pressure_handles):
        return None
    if not k.QueryMemoryResourceNotification(_pressure_handles[0], ctypes.byref(low)):
        return None
    if not k.QueryMemoryResourceNotification(_pressure_handles[1], ctypes.byref(high)):
        return None
    return "critical" if low.value else "normal" if high.value else "warning"


def fully_occluded(hwnd):
    """Subtract opaque windows above us. Any uncovered sliver keeps sampling on.

    Layered windows are deliberately not subtracted: their alpha/holes cannot be
    determined from a rectangle. This can use more CPU but cannot falsely sleep.
    """
    u, g = user32, ctypes.windll.gdi32
    u.GetWindow.argtypes = [wintypes.HWND, wintypes.UINT]
    u.GetWindow.restype = wintypes.HWND
    u.IsIconic.argtypes = [wintypes.HWND]
    g.CreateRectRgn.argtypes = [ctypes.c_int] * 4
    g.CreateRectRgn.restype = wintypes.HANDLE
    g.CombineRgn.argtypes = [
        wintypes.HANDLE,
        wintypes.HANDLE,
        wintypes.HANDLE,
        ctypes.c_int,
    ]
    g.DeleteObject.argtypes = [wintypes.HANDLE]
    if not u.IsWindowVisible(hwnd) or u.IsIconic(hwnd):
        return True
    cloaked = wintypes.DWORD()
    if (
        dwmapi.DwmGetWindowAttribute(hwnd, 14, ctypes.byref(cloaked), 4) == 0
        and cloaked.value
    ):
        return True
    rect = wintypes.RECT()
    if not u.GetWindowRect(hwnd, ctypes.byref(rect)):
        return False
    region = g.CreateRectRgn(
        rect.left + 2, rect.top + 2, rect.right - 2, rect.bottom - 2
    )
    try:
        current = u.GetWindow(hwnd, 3)  # GW_HWNDPREV (higher z order)
        while current:
            if u.IsWindowVisible(current) and not u.IsIconic(current):
                ex = u.GetWindowLongW(current, GWL_EXSTYLE)
                cloak = wintypes.DWORD()
                dwmapi.DwmGetWindowAttribute(current, 14, ctypes.byref(cloak), 4)
                if not cloak.value and not (ex & (0x80000 | 0x20)):
                    cover = wintypes.RECT()
                    if u.GetWindowRect(current, ctypes.byref(cover)):
                        part = g.CreateRectRgn(
                            cover.left, cover.top, cover.right, cover.bottom
                        )
                        try:
                            if g.CombineRgn(region, region, part, 4) == 1:
                                return True
                        finally:
                            g.DeleteObject(part)
            current = u.GetWindow(current, 3)
        return False
    finally:
        g.DeleteObject(region)


def suspend_backdrop(hwnd):
    """Remove compositor blur during interactive geometry changes.

    Win10 acrylic adds multiple composition frames to each move/resize. Keep
    the Qt tinted surface, then reapply the current material after release.
    """
    backdrop = ctypes.c_int(1)  # DWMSBT_NONE on Win11; unsupported on Win10.
    dwm_ok = dwmapi.DwmSetWindowAttribute(hwnd, 38, ctypes.byref(backdrop), 4) == 0
    accent_ok = _apply_accent(hwnd, 0)
    return dwm_ok or accent_ok


def apply_backdrop(hwnd, dark):
    """Prefer Win11 DWM transient backdrop; use clipped blur behind on Win10."""
    dwmapi.DwmSetWindowAttribute.argtypes = [
        wintypes.HWND,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.DWORD,
    ]
    dwmapi.DwmGetWindowAttribute.argtypes = [
        wintypes.HWND,
        wintypes.DWORD,
        ctypes.c_void_p,
        wintypes.DWORD,
    ]
    value = ctypes.c_int(int(dark))
    dwmapi.DwmSetWindowAttribute(hwnd, 20, ctypes.byref(value), 4)
    corner = ctypes.c_int(2)
    dwmapi.DwmSetWindowAttribute(hwnd, 33, ctypes.byref(corner), 4)
    backdrop = ctypes.c_int(3)
    if dwmapi.DwmSetWindowAttribute(hwnd, 38, ctypes.byref(backdrop), 4) == 0:
        return True
    # Acrylic's compositor tint fills the rectangular bounds on this Win10
    # build even with a correct native region. Classic blur preserves the mask;
    # Qt supplies the theme tint. Failure leaves the translucent panel intact.
    return apply_blur(hwnd)


class SYSTEM_POWER_STATUS(ctypes.Structure):
    _fields_ = [
        ("ACLineStatus", wintypes.BYTE),
        ("BatteryFlag", wintypes.BYTE),
        ("BatteryLifePercent", wintypes.BYTE),
        ("SystemStatusFlag", wintypes.BYTE),
        ("BatteryLifeTime", wintypes.DWORD),
        ("BatteryFullLifeTime", wintypes.DWORD),
    ]


def battery_saver():
    status = SYSTEM_POWER_STATUS()
    kernel32.GetSystemPowerStatus.argtypes = [ctypes.POINTER(SYSTEM_POWER_STATUS)]
    return bool(
        kernel32.GetSystemPowerStatus(ctypes.byref(status)) and status.SystemStatusFlag
    )


class UNICODE_STRING(ctypes.Structure):
    _fields_ = [
        ("Length", wintypes.USHORT),
        ("MaximumLength", wintypes.USHORT),
        ("Buffer", ctypes.c_void_p),
    ]


class SYSTEM_PROCESS_INFORMATION(ctypes.Structure):
    # Windows SDK winternl.h layout. Reserved fields are kept at their actual sizes.
    _fields_ = [
        ("NextEntryOffset", wintypes.ULONG),
        ("NumberOfThreads", wintypes.ULONG),
        ("Reserved1", ctypes.c_byte * 24),
        ("CreateTime", ctypes.c_int64),
        ("UserTime", ctypes.c_int64),
        ("KernelTime", ctypes.c_int64),
        ("ImageName", UNICODE_STRING),
        ("BasePriority", wintypes.LONG),
        ("UniqueProcessId", ctypes.c_void_p),
        ("ParentProcessId", ctypes.c_void_p),
        ("HandleCount", wintypes.ULONG),
        ("SessionId", wintypes.ULONG),
        ("UniqueProcessKey", ctypes.c_size_t),
        ("PeakVirtualSize", ctypes.c_size_t),
        ("VirtualSize", ctypes.c_size_t),
        ("PageFaultCount", wintypes.ULONG),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
        ("PrivatePageCount", ctypes.c_size_t),
        ("IoCounters", ctypes.c_int64 * 6),
    ]


class ProcessTable:
    """One kernel query for the complete table. Reuses its allocated buffer.

    The documented query may evolve; callers retain a psutil fallback on failure.
    All offsets and string pointers are bounded by the returned allocation.
    """

    def __init__(self):
        self.query = ctypes.WinDLL("ntdll").NtQuerySystemInformation
        self.query.argtypes = [
            wintypes.ULONG,
            ctypes.c_void_p,
            wintypes.ULONG,
            ctypes.POINTER(wintypes.ULONG),
        ]
        self.query.restype = wintypes.LONG
        self.buffer = ctypes.create_string_buffer(256 * 1024)

    def read(self):
        length = wintypes.ULONG()
        for _ in range(8):
            status = self.query(5, self.buffer, len(self.buffer), ctypes.byref(length))
            if status == 0:
                break
            if status & 0xFFFFFFFF != 0xC0000004:
                raise OSError(f"NtQuerySystemInformation: 0x{status & 0xFFFFFFFF:08x}")
            size = max(len(self.buffer) * 2, length.value + 65536)
            if size > 128 * 1024 * 1024:
                raise OSError("Process table exceeds size limit")
            self.buffer = ctypes.create_string_buffer(size)
        else:
            raise OSError("Process table kept growing")
        base = ctypes.addressof(self.buffer)
        limit = min(length.value, len(self.buffer))
        offset, rows = 0, []
        while offset + ctypes.sizeof(SYSTEM_PROCESS_INFORMATION) <= limit:
            row = SYSTEM_PROCESS_INFORMATION.from_buffer(self.buffer, offset)
            name_ptr, name_len = row.ImageName.Buffer, row.ImageName.Length
            if name_ptr and name_len:
                if name_len % 2 or not base <= name_ptr <= base + limit - name_len:
                    raise ValueError("Invalid process name span")
                name = ctypes.wstring_at(name_ptr, name_len // 2)
            else:
                name = (
                    "System"
                    if row.UniqueProcessId == 4
                    else str(row.UniqueProcessId or 0)
                )
            rows.append(
                dict(
                    pid=int(row.UniqueProcessId or 0),
                    ppid=int(row.ParentProcessId or 0),
                    name=name,
                    created=row.CreateTime,
                    cpu_time=(row.UserTime + row.KernelTime) / 1e7,
                    mem=int(row.WorkingSetSize),
                )
            )
            step = row.NextEntryOffset
            if not step:
                break
            if (
                step < ctypes.sizeof(SYSTEM_PROCESS_INFORMATION)
                or offset + step >= limit
            ):
                raise ValueError("Invalid process table offset")
            offset += step
        if not rows:
            raise ValueError("Empty process table")
        return rows


def file_description(path):
    """Read an executable's display name once; do not load or execute the file."""
    try:
        v = ctypes.windll.version
        v.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p]
        v.GetFileVersionInfoW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            ctypes.c_void_p,
        ]
        v.VerQueryValueW.argtypes = [
            ctypes.c_void_p,
            wintypes.LPCWSTR,
            ctypes.POINTER(ctypes.c_void_p),
            ctypes.POINTER(wintypes.UINT),
        ]
        size = v.GetFileVersionInfoSizeW(path, None)
        if not 0 < size < 8 * 1024 * 1024:
            return None
        data = ctypes.create_string_buffer(size)
        if not v.GetFileVersionInfoW(path, 0, size, data):
            return None
        pointer, length = ctypes.c_void_p(), wintypes.UINT()
        if not v.VerQueryValueW(
            data,
            r"\VarFileInfo\Translation",
            ctypes.byref(pointer),
            ctypes.byref(length),
        ):
            return None
        translation = ctypes.cast(pointer, ctypes.POINTER(wintypes.WORD))
        for i in range(min(length.value // 4, 16)):
            key = f"\\StringFileInfo\\{translation[i * 2]:04x}{translation[i * 2 + 1]:04x}\\FileDescription"
            text, count = ctypes.c_void_p(), wintypes.UINT()
            if (
                v.VerQueryValueW(data, key, ctypes.byref(text), ctypes.byref(count))
                and 0 < count.value < 1024
            ):
                return ctypes.wstring_at(text, count.value).strip("\0 ")
    except (OSError, ValueError):
        pass
    return None


class MONITORINFOEXW(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.DWORD),
        ("rcMonitor", wintypes.RECT),
        ("rcWork", wintypes.RECT),
        ("dwFlags", wintypes.DWORD),
        ("szDevice", wintypes.WCHAR * 32),
    ]


def monitor_geometry(x, y):
    user32.MonitorFromPoint.argtypes = [wintypes.POINT, wintypes.DWORD]
    user32.MonitorFromPoint.restype = wintypes.HANDLE
    user32.GetMonitorInfoW.argtypes = [wintypes.HANDLE, ctypes.c_void_p]
    monitor = user32.MonitorFromPoint(wintypes.POINT(x, y), 2)
    info = MONITORINFOEXW()
    info.cbSize = ctypes.sizeof(info)
    if monitor and user32.GetMonitorInfoW(monitor, ctypes.byref(info)):
        return info.szDevice, info.rcMonitor.left, info.rcMonitor.top
    return None
