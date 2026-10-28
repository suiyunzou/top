"""One sampling thread. Idle reads only tray CPU; disabled modules do no work."""

import threading
import time
import traceback
import psutil
from .core import SteadyRanking, group_processes, shown
from . import native


def empty_snapshot(n):
    return dict(
        sequence=0,
        timestamp=0,
        cpu=None,
        cores=[None] * n,
        freqs=[None] * n,
        memory=None,
        total=None,
        available=None,
        compressed=None,
        swap=None,
        swap_total=None,
        pressure=None,
        down=None,
        up=None,
        gpu=None,
        gpu_name=None,
        gpu_memory=None,
        gpu_total=None,
        gpu_temp=None,
        gpu_clock=None,
        gpu_power=None,
        power=None,
        temperature=None,
        groups={},
        cpu_order=[],
        memory_order=[],
        errors={},
    )


class NetworkDelta:
    def __init__(self):
        self.previous = None

    def step(self, counters, now):
        previous, self.previous = self.previous, (dict(counters), now)
        if previous is None or now <= previous[1]:
            return None, None
        old, when = previous
        # A newly connected interface never injects its lifetime total into one sample.
        common = counters.keys() & old.keys()
        if not common:
            return None, None
        rx = sum(max(0, counters[k][0] - old[k][0]) for k in common)
        tx = sum(max(0, counters[k][1] - old[k][1]) for k in common)
        return rx / (now - when), tx / (now - when)


class ProcessReader:
    def __init__(self):
        self.previous = {}
        self.paths = {}
        self.names = {}
        self.table = native.ProcessTable()
        self.source = "NtQuerySystemInformation"

    def reset(self):
        self.previous.clear()

    def sample(self, now):
        try:
            return self._bulk(now)
        except (OSError, ValueError, AttributeError) as exc:
            native.log_error(f"Process table fallback: {exc}", once=True)
            self.source = "psutil fallback"
            return self._fallback(now)

    def _bulk(self, now):
        records, previous, paths = [], {}, {}
        for r in self.table.read():
            if r["pid"] == 0:
                continue
            identity = r["pid"], r["created"]
            before = self.previous.get(identity)
            r["cpu"] = (
                max(0, (r["cpu_time"] - before[0]) / (now - before[1]) * 100)
                if before and now > before[1]
                else None
            )
            previous[identity] = r["cpu_time"], now
            if identity in self.paths:
                exe = self.paths[identity]
            else:
                try:
                    exe = psutil.Process(r["pid"]).exe()
                except (psutil.Error, OSError):
                    exe = None
            paths[identity] = exe
            r["exe"] = exe
            if exe:
                if exe not in self.names:
                    self.names[exe] = native.file_description(exe) or r["name"]
                r["app_name"] = self.names[exe]
            records.append(r)
        self.previous, self.paths = previous, paths
        active = set(paths.values())
        self.names = {k: v for k, v in self.names.items() if k in active}
        return group_processes(records)

    def _fallback(self, now):
        records, next_previous = [], {}
        # Bulk Windows process data; use CPU times rather than psutil's thread-local
        # percentage baseline. PID + creation time rejects recycled process ids.
        for p in psutil.process_iter(
            ["pid", "ppid", "name", "exe", "create_time", "cpu_times", "memory_info"],
            ad_value=None,
        ):
            i = p.info
            if i["pid"] == 0:
                continue
            key = (i["pid"], i["create_time"])
            cpu = None
            ct = i["cpu_times"]
            if ct is not None:
                total = ct.user + ct.system
                before = self.previous.get(key)
                if before and now > before[1]:
                    cpu = max(0.0, (total - before[0]) / (now - before[1]) * 100.0)
                next_previous[key] = (total, now)
            mi = i["memory_info"]
            # RSS matches the previous port; it is working set, not private bytes.
            records.append(
                dict(
                    pid=i["pid"],
                    ppid=i["ppid"],
                    name=i["name"] or str(i["pid"]),
                    exe=i["exe"],
                    created=i["create_time"],
                    cpu=cpu,
                    mem=mi.rss if mi else None,
                )
            )
        self.previous = next_previous
        return group_processes(records)


class Sampler:
    def __init__(self, config, start=True):
        self.n = psutil.cpu_count() or 1
        self.cores, self.topology_source = native.read_topology(self.n)
        self.order, self.info = native.make_layout(self.cores)
        self.config = dict(config)
        self.mode = "full"
        self.hold = False
        self.snap = empty_snapshot(self.n)
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._thread = None
        self._cpu_previous = None
        self._full_previous = None
        self._net = NetworkDelta()
        self._processes = ProcessReader()
        self._cpu_rank = SteadyRanking()
        self._mem_rank = SteadyRanking(0.04, 24 * 1048576)
        self._last_process = 0
        self.pdh = self.nvml = None
        self.cost = {}
        self._last_mode = None
        self._nic_names = []
        self._nic_time = 0
        if start:
            self._thread = threading.Thread(
                target=self._run, name="ftop-sampler", daemon=True
            )
            self._thread.start()

    def configure(self, config):
        self.config = dict(config)
        self._wake.set()

    def set_mode(self, mode):
        if mode != self.mode:
            self.mode = mode
            self._wake.set()

    def stop(self):
        self._stop.set()
        self._wake.set()
        if self._thread:
            self._thread.join(timeout=4)

    @staticmethod
    def cpu_delta(old, new):
        if old is None:
            return None
        total = sum(new) - sum(old)
        idle = new.idle - old.idle
        return (
            min(100.0, max(0.0, (total - idle) / total * 100.0)) if total > 0 else 0.0
        )

    def sample(self):
        now = time.monotonic()
        mode, config = self.mode, self.config
        mods = shown(config)
        s = dict(self.snap)
        s["errors"] = {}

        def attempt(label, fn):
            started = time.perf_counter()
            try:
                fn()
            except Exception as exc:
                s["errors"][label] = str(exc)
                native.log_error(f"{label}: {exc}", once=True)
                unavailable = {
                    "cpu": ("cpu",),
                    "cores": ("cores", "freqs"),
                    "memory": (
                        "memory",
                        "total",
                        "available",
                        "swap",
                        "swap_total",
                        "pressure",
                    ),
                    "network": ("down", "up"),
                    "gpu": (
                        "gpu",
                        "gpu_memory",
                        "gpu_total",
                        "gpu_temp",
                        "gpu_clock",
                        "gpu_power",
                    ),
                }
                for key in unavailable.get(label, ()):
                    s[key] = [None] * self.n if key in ("cores", "freqs") else None
                if label == "processes":
                    s.update(groups={}, cpu_order=[], memory_order=[])
            self.cost[label] = (time.perf_counter() - started) * 1000

        def cpu():
            current = psutil.cpu_times()
            s["cpu"] = self.cpu_delta(self._cpu_previous, current)
            self._cpu_previous = current

        if mode != "idle" or config["menuBar"]:
            attempt("cpu", cpu)
        if mode != self._last_mode:
            self._full_previous = None
            self._net.previous = None
            self._processes.reset()
            self._last_process = 0
        self._last_mode = mode
        if mode != "idle":

            def cores():
                current = psutil.cpu_times(percpu=True)
                previous = self._full_previous
                s["cores"] = [
                    self.cpu_delta(
                        previous[i] if previous and i < len(previous) else None, c
                    )
                    for i, c in enumerate(current)
                ]
                self._full_previous = current
                if mode == "full":
                    if self.pdh is None:
                        nominal = native.nominal_ghz(self.n, 0.0)
                        self.pdh = native.Pdh(self.n, nominal)
                    _, s["freqs"] = self.pdh.query()

            attempt("cores", cores)
            if "memory" in mods:

                def mem():
                    vm, sw = psutil.virtual_memory(), psutil.swap_memory()
                    s.update(
                        memory=vm.total - vm.available,
                        available=vm.available,
                        total=vm.total,
                        swap=sw.used,
                        swap_total=sw.total,
                        pressure=native.memory_pressure(),
                    )
                    # A missing Memory Compression process is not zero compressed memory.
                    s["compressed"] = None

                attempt("memory", mem)
            if "network" in mods:

                def network():
                    if now - self._nic_time > 15:
                        stats = psutil.net_if_stats()
                        skip = (
                            "loopback",
                            "vethernet",
                            "vmware",
                            "virtualbox",
                            "teredo",
                            "isatap",
                            "bluetooth",
                            "tun",
                            "tap",
                            "clash",
                            "mihomo",
                        )
                        self._nic_names = [
                            k
                            for k, v in stats.items()
                            if v.isup and not any(x in k.casefold() for x in skip)
                        ]
                        self._nic_time = now
                    counters = psutil.net_io_counters(pernic=True, nowrap=True)
                    # Include Ethernet/Wi-Fi adapters. Exclude loopback and virtual overlay
                    # interfaces to avoid counting the same packet twice.
                    active = {
                        k: (v.bytes_recv, v.bytes_sent)
                        for k, v in counters.items()
                        if k in self._nic_names
                    }
                    s["down"], s["up"] = self._net.step(active, now)

                attempt("network", network)
            if "gpu" in mods:

                def gpu():
                    if self.nvml is None:
                        self.nvml = native.Nvml()
                    g = self.nvml.read()
                    s.update(
                        gpu=g["util"],
                        gpu_name=self.nvml.name,
                        gpu_memory=g["mem_used"],
                        gpu_total=g["mem_total"],
                        gpu_temp=g["temp"],
                        gpu_clock=g["clock"],
                        gpu_power=g["power"],
                    )

                attempt("gpu", gpu)
            if (
                mode == "full"
                and "processes" in mods
                and now - self._last_process >= max(1.0, config["interval"])
            ):

                def procs():
                    groups = self._processes.sample(now)
                    elapsed = now - self._last_process if self._last_process else 1
                    s["groups"] = groups
                    s["cpu_order"] = self._cpu_rank.step(
                        [(k, g["cpu"] or 0) for k, g in groups.items()],
                        elapsed,
                        self.hold,
                    )
                    s["memory_order"] = self._mem_rank.step(
                        [(k, g["mem"] or 0) for k, g in groups.items()],
                        elapsed,
                        self.hold,
                    )
                    self._last_process = now

                attempt("processes", procs)
        s["timestamp"] = now
        s["sequence"] = s["sequence"] + 1
        self.snap = (
            s  # Publish one complete snapshot; UI never sees a half-written sample.
        )
        return s

    def _run(self):
        try:
            while not self._stop.is_set():
                started = time.monotonic()
                self._wake.clear()
                self.sample()
                interval = 2 if self.mode == "idle" else self.config["interval"]
                self._wake.wait(max(0.05, interval - (time.monotonic() - started)))
        except Exception:
            native.log_error("sampler: " + traceback.format_exc())
        finally:
            if self.pdh:
                self.pdh.close()
            if self.nvml and self.nvml.ok:
                self.nvml.lib.nvmlShutdown()


def sample_fixture(n=12):
    s = empty_snapshot(n)
    s.update(
        sequence=1,
        cpu=47.0,
        cores=[47, 53, 68, 48, 85, 76, 39, 17, 51, 61, 4, 19][:n],
        freqs=[2.50, 2.71, 3.18, 2.54, 3.71, 3.44, 2.23, 1.54, 2.64, 2.96, 0.91, 1.21][
            :n
        ],
        memory=int(8.9 * 1024**3),
        total=16 * 1024**3,
        available=int(7.1 * 1024**3),
        compressed=None,
        swap=int(0.8 * 1024**3),
        swap_total=8 * 1024**3,
        pressure="normal",
        down=2.9e6,
        up=432000,
        gpu=37,
        gpu_name="NVIDIA GeForce RTX",
        gpu_memory=1.4,
        gpu_total=8,
        gpu_temp=56,
        gpu_clock=1.4,
        gpu_power=31.0,
    )
    names = [
        "Visual Studio Code",
        "Google Chrome",
        "Windows Terminal",
        "explorer.exe",
        "Python",
        "Figma",
        "Cursor",
        "Slack",
        "System",
        "Notepad",
        "SearchHost.exe",
        "Docker",
    ]
    for i in range(48):
        name = names[i % len(names)] + (
            f" {i // len(names) + 1}" if i >= len(names) else ""
        )
        key = str(i)
        members = [
            dict(
                pid=1000 + i * 10 + j,
                name=name,
                cpu=max(0, 189 - i * 4) / (j + 1),
                mem=(512 - i * 4) * 1048576,
                exe=None,
            )
            for j in range(3)
        ]
        s["groups"][key] = dict(
            key=key,
            name=name,
            exe=None,
            cpu=max(0, 189 - i * 4),
            mem=(2048 - i * 30) * 1048576,
            count=3,
            members=members,
        )
    s["cpu_order"] = list(s["groups"])
    s["memory_order"] = list(reversed(s["cpu_order"]))
    return s
