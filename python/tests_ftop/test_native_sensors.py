import os
import time
from unittest.mock import patch
import psutil
from ftop_windows import native
from ftop_windows.core import normalize
from ftop_windows.sensors import Sampler, ProcessReader


def test_missing_frequency_is_not_nominal_frequency():
    pdh = object.__new__(native.Pdh)
    pdh.ok = False
    pdh.n = 2
    pdh.nominal = [2.6, 2.6]
    pdh.avg = 2.6
    assert pdh.query() == (None, [None, None])
    assert pdh._val(None, 2.6) is None


def test_bulk_table_matches_current_process():
    rows = native.ProcessTable().read()
    me = next(r for r in rows if r["pid"] == os.getpid())
    p = psutil.Process()
    ct = p.cpu_times()
    assert abs(me["cpu_time"] - ct.user - ct.system) < 0.1
    assert abs(me["mem"] - p.memory_info().rss) < 10 * 1048576
    assert me["ppid"] == p.ppid() and me["created"] > 0
    assert len(rows) > 10


def test_process_reader_pid_reuse_never_inherits_cpu_or_path():
    reader = ProcessReader()
    row = dict(pid=123456, ppid=0, name="x.exe", created=100, cpu_time=10, mem=100)
    with (
        patch.object(reader.table, "read", return_value=[row]),
        patch("psutil.Process") as proc,
    ):
        proc.return_value.exe.return_value = "C:\\x.exe"
        reader.sample(1)
        reader.table.read.return_value = [dict(row, created=200, cpu_time=20)]
        group = next(iter(reader.sample(2).values()))
        assert group["cpu"] is None
        assert proc.return_value.exe.call_count == 2


def test_hidden_only_calls_total_cpu():
    s = Sampler(normalize({}), start=False)
    s.mode = "idle"
    with (
        patch("psutil.virtual_memory", side_effect=AssertionError("RAM in idle")),
        patch("psutil.cpu_times", return_value=psutil.cpu_times()) as cpu,
        patch.object(
            s._processes, "sample", side_effect=AssertionError("processes in idle")
        ),
    ):
        snap = s.sample()
        assert snap["cpu"] is None
        assert cpu.call_count == 1
        assert s.pdh is None and s.nvml is None


def test_hidden_without_tray_does_not_sample_cpu():
    s = Sampler(normalize(dict(menuBar=False)), start=False)
    s.mode = "idle"
    with patch("psutil.cpu_times", side_effect=AssertionError("CPU without tray")):
        s.sample()


def test_disabled_modules_do_not_call_providers():
    s = Sampler(normalize(dict(modules=["cpu"], hidden=["gpu", "power"])), start=False)
    s.mode = "light"
    with (
        patch("psutil.virtual_memory", side_effect=AssertionError("RAM disabled")),
        patch("psutil.net_io_counters", side_effect=AssertionError("network disabled")),
    ):
        s.sample()
        assert not s.snap["errors"]
        assert s.nvml is None


def test_read_failure_clears_stale_values():
    s = Sampler(normalize({}), start=False)
    s.mode = "light"
    s.snap["memory"] = 1000
    with patch("psutil.virtual_memory", side_effect=OSError("test unavailability")):
        s.sample()
        assert s.snap["memory"] is None
        assert "memory" in s.snap["errors"]
    if s.nvml and s.nvml.ok:
        s.nvml.lib.nvmlShutdown()


def test_topology_is_complete_and_honest():
    n = psutil.cpu_count()
    cores, source = native.read_topology(n)
    assert sorted(i for c in cores for i in c[2]) == list(range(n))
    assert native.parse_core_records(b"\0" * 8, 8) == []


def test_sampler_thread_stops_and_releases_handles():
    s = Sampler(normalize(dict(modules=["cpu"], hidden=["gpu", "power"])))
    time.sleep(0.15)
    s.stop()
    assert not s._thread.is_alive()
