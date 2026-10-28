import itertools
from ftop_windows.core import (
    ConfigStore,
    normalize,
    LayoutEngine,
    SteadyRanking,
    group_processes,
    rate,
    memory,
    Follow,
)
from ftop_windows.sensors import NetworkDelta


def test_config_first_write_and_read(tmp_path):
    store = ConfigStore(tmp_path / "config.json5")
    assert store.error is None
    assert store.save(dict(appearance="dark", language="zh", motion=False))
    assert ConfigStore(store.path).data["appearance"] == "dark"
    assert ConfigStore(store.path).data["motion"] is False


def test_json5_hot_reload_invalid_preserves_valid_config(tmp_path):
    store = ConfigStore(tmp_path / "config.json5")
    store.path.write_text("// comment\n{palette:'warm', interval:2,}", encoding="utf-8")
    assert store.reload() and store.data["palette"] == "warm"
    store.path.write_text("{palette: ", encoding="utf-8")
    assert not store.reload() and store.data["palette"] == "warm"
    assert not store.save({"floating": True})
    assert store.path.read_text() == "{palette: "
    store.path.write_text('{palette:"sea"}', encoding="utf-8")
    assert store.reload() and store.error is None


def test_legacy_config_migration(tmp_path):
    (tmp_path / "config.json").write_text(
        '{"is_topmost":true,"x":-200,"width":380}', encoding="utf-8"
    )
    store = ConfigStore(tmp_path / "config.json5")
    assert store.data["floating"] is True and store.data["x"] == -200
    assert store.data["width"] == 380
    assert store.data["appearance"] == "dark"
    assert (tmp_path / "config.json").exists()


def test_config_validation():
    c = normalize(
        dict(
            interval=float("nan"),
            maxProcesses=999,
            modules=["cpu", "invalid", "cpu"],
            motion="false",
            hidden=17,
        )
    )
    assert c["interval"] == 1 and c["maxProcesses"] == 24 and c["modules"] == ["cpu"]
    assert c["motion"] is True and c["hidden"] == []


def test_ranking_small_swaps_rest_new_busy_entry_rises():
    r = SteadyRanking()
    assert r.step([("a", 50), ("b", 49)], 1) == ["a", "b"]
    assert r.step([("a", 49), ("b", 50)], 1) == ["a", "b"]
    assert r.step([("a", 49), ("b", 50), ("c", 90)], 1) == ["c", "a", "b"]


def test_ranking_decay_hold_exit_and_duplicate():
    r = SteadyRanking()
    r.step([("a", 80), ("b", 20)], 1)
    assert r.step([("a", 0), ("b", 30)], 1) == ["a", "b"]
    assert r.step([("a", 0), ("b", 90)], 1, hold=True) == ["a", "b"]
    assert r.step([("b", 90), ("b", 1)], 1) == ["b"]
    assert r.levels["b"] >= 90


def rec(pid, exe, ppid=0, cpu=1, mem=100):
    return dict(
        pid=pid,
        ppid=ppid,
        exe=exe,
        name=exe.split("\\")[-1],
        cpu=cpu,
        mem=mem,
        created=1,
    )


def test_grouping_paths_and_helpers_without_system_process_merging():
    records = [
        rec(1, r"C:\App\app.exe"),
        rec(2, r"C:\App\app.exe", 1),
        rec(3, r"C:\App\app-helper.exe", 1),
        rec(4, r"D:\Other\app.exe"),
        rec(5, r"C:\Windows\System32\services.exe"),
        rec(6, r"C:\Windows\System32\svchost.exe", 5),
    ]
    groups = group_processes(records)
    assert len(groups) == 4
    app = groups[r"c:\app\app.exe"]
    assert app["count"] == 3 and app["cpu"] == 3 and app["mem"] == 300
    assert r"c:\windows\system32\svchost.exe" in groups


def test_group_unknowns_are_not_conflated_or_reported_zero():
    a = rec(1, "", cpu=None, mem=None)
    b = rec(2, "", cpu=None, mem=None)
    groups = group_processes([a, b])
    assert len(groups) == 2
    assert all(g["cpu"] is None and g["mem"] is None for g in groups.values())


def test_network_first_sample_reset_interface_change_and_monotonic():
    r = NetworkDelta()
    assert r.step({"eth": (1000, 500)}, 1) == (None, None)
    assert r.step({"eth": (3000, 1500)}, 3) == (1000, 500)
    assert r.step({"eth": (200, 100), "wifi": (10**9, 10**9)}, 4) == (0, 0)
    assert r.step({"wifi": (10**9 + 100, 10**9 + 50)}, 5) == (100, 50)
    assert r.step({"wifi": (0, 0)}, 4) == (None, None)


def test_format_upstream_thresholds_and_unknowns():
    assert rate(999500) == ("1.0", "MB/s")
    assert rate(1e9) == ("1.0", "GB/s")
    assert rate(-10) == ("0", "KB/s")
    assert memory(1000 * 1048576) == ("1.0", "G")
    assert memory(None) == ("—", "")


def test_follow_additive_continuity_and_end():
    f = Follow()
    f.set("a", 0, 0)
    f.set("a", 100, 1)
    before = f.value("a", 2)
    f.set("a", 50, 2)
    assert abs(f.value("a", 2) - before) < 1e-6
    assert f.value("a", 20) == 50 and not f.active()
    f.set("a", None, 21)
    assert f.value("a", 21) is None


def test_layout_size_sweep_and_modules():
    checked = 0
    for n in (1, 6, 12, 32, 64, 128):
        for mask in (0, 1, 3, 7, 15, 31):
            optional = ["memory", "network", "processes", "gpu", "power"]
            on = {v for i, v in enumerate(optional) if mask & (1 << i)}
            c = normalize(
                dict(
                    modules=["cpu"] + [x for x in optional[:3] if x in on],
                    hidden=[x for x in optional[3:] if x not in on],
                )
            )
            engine = LayoutEngine(n, c)
            for w, h in itertools.product(
                (48, 64, 96, 180, 280, 300, 480, 660, 1000, 1400),
                (20, 30, 54, 120, 230, 420, 820),
            ):
                layout = engine.choose(w, h)
                assert layout.width * layout.scale <= w + 1e-6, (n, w, h, layout)
                assert layout.height * layout.scale <= h + 1e-6, (n, w, h, layout)
                assert layout.scale <= 1.3
                for b in layout.blocks:
                    assert b.x >= 0 and b.y >= 0 and b.w > 0 and b.h > 0
                    assert (
                        b.x + b.w <= layout.width + 0.01
                        and b.y + b.h <= layout.height + 0.01
                    )
                checked += 1
    assert checked == 2520


def test_layout_independent_of_readings():
    engine = LayoutEngine(12, normalize({}))
    a = engine.choose(1000, 500)
    assert a.candidate.wide and a.candidate.count >= 7
    assert a == engine.choose(1000, 500)
