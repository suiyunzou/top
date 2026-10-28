"""Exercise the real native EXE using isolated config and its own windows only."""

from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json
import os
import subprocess
import sys
import time
import psutil

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get("FTOP_TEST_EXE", str(ROOT / "dist/top.exe")))
OUT = ROOT / "scratch" / "evidence"
OUT.mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT.parent))

u = C.windll.user32
u.SetProcessDpiAwarenessContext.argtypes = [C.c_void_p]
u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]
u.FindWindowW.restype = W.HWND
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SendMessageW.restype = W.LPARAM
u.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
u.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.GetGuiResources.argtypes = [W.HANDLE, W.DWORD]
u.GetGuiResources.restype = W.DWORD


def run(*args):
    r = subprocess.run(
        [str(EXE), *map(str, args)],
        timeout=12,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    assert r.returncode == 0, (args, r.returncode)


def layouts():
    cases = []
    for config in (
        {},
        {"modules": ["cpu"], "hidden": ["gpu", "power"]},
        {"maxProcesses": 5},
        {"modules": ["cpu", "memory"], "hidden": ["power"]},
    ):
        for n in (2, 8, 12, 24, 64):
            for w in (40, 90, 180, 280, 400, 600, 900):
                for h in (20, 40, 100, 200, 400, 700):
                    cases.append(dict(config=config, n=n, width=w, height=h))
    inp, out = OUT / "layout-input.json", OUT / "layout-output.json"
    inp.write_text(json.dumps(cases), encoding="utf8")
    run("--layout-report", inp, out)
    actual = json.loads(out.read_text())
    assert len(actual) == len(cases)
    for case, a in zip(cases, actual):
        # The approved responsive design intentionally replaces upstream's
        # candidate ranking. Verify its contract rather than old pixel parity.
        assert a["width"] * a["scale"] <= case["width"] + .01
        assert a["height"] * a["scale"] <= case["height"] + .01
        blocks = a["blocks"]
        for kind, x, y, w, h in blocks:
            assert min(x, y) >= 0 and min(w, h) > 0, (case, kind)
            assert x + w <= a["width"] + .01 and y + h <= a["height"] + .01
        if any(b[0] == "cpu" for b in blocks):
            slots = a["slots"]
            assert len(slots) == case["n"], (case, "lost logical processor")
            cpu = next(b for b in blocks if b[0] == "cpu")
            for i, (x, y, w, h) in enumerate(slots):
                assert w > 0 and h > 0
                assert x >= cpu[1] - .01 and y >= cpu[2] + 31 - .01
                assert x + w <= cpu[1] + cpu[3] + .01
                assert y + h <= cpu[2] + cpu[4] - 27 + .01
                for qx, qy, qw, qh in slots[:i]:
                    assert x+w <= qx+.01 or qx+qw <= x+.01 or y+h <= qy+.01 or qy+qh <= y+.01
    return len(cases)


def integration():
    config = OUT / "测试 native config.json"
    original = dict(
        width=527,
        height=265,
        x=80,
        y=80,
        appearance="dark",
        floating=True,
        menuBar=False,
        motion=True,
        customField="preserved",
    )
    config.write_text(json.dumps(original), encoding="utf8")
    env = os.environ.copy()
    env["PATH"] = (
        os.environ["SystemRoot"] + ";" + os.environ["SystemRoot"] + "\\System32"
    )
    env.pop("PYTHONPATH", None)
    env.pop("PYTHONHOME", None)
    start = time.perf_counter()
    p = subprocess.Popen(
        [str(EXE), "--config", str(config)],
        env=env,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    cursor = W.POINT()
    u.GetCursorPos(C.byref(cursor))
    checks = []
    report = {}

    def own_window(title):
        result = []
        callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)

        @callback_type
        def collect(hwnd, _):
            pid = W.DWORD()
            u.GetWindowThreadProcessId(hwnd, C.byref(pid))
            if pid.value == p.pid:
                text = C.create_unicode_buffer(256)
                u.GetWindowTextW(hwnd, text, len(text))
                if text.value == title:
                    result.append(hwnd)
            return True

        u.EnumWindows(collect, 0)
        return result[0] if result else None

    def command(*args):
        run("--config", config, *args)

    def state():
        file = OUT / "state.json"
        command("--inspect", file)
        return json.loads(file.read_text())

    def click(which, action):
        s = state()
        title = "top" + ("" if which == "panel" else " " + which)
        hwnd = own_window(title)
        h = next(h for h in s[which]["hits"] if h["id"] == action)
        x1, y1, x2, y2 = h["rect"]
        factor = s[which]["dpi"] * (s["layout"]["scale"] if which == "panel" else 1)
        x, y = int((x1 + x2) / 2 * factor), int((y1 + y2) / 2 * factor)
        u.SendMessageW(hwnd, 0x202, 0, (y << 16) | x)
        time.sleep(0.12)

    try:
        deadline = time.monotonic() + 10
        hwnd = None
        while time.monotonic() < deadline:
            assert p.poll() is None, p.returncode
            hwnd = own_window("top")
            if hwnd and u.IsWindowVisible(hwnd):
                break
            time.sleep(0.03)
        assert hwnd
        report["startupSeconds"] = round(time.perf_counter() - start, 3)
        checks.append("minimal PATH startup; Unicode config path")
        time.sleep(4)
        s = state()
        assert len(s["cores"]) == psutil.cpu_count()
        assert 0 <= s["cpu"] <= 100 and s["memoryUsed"] > 0
        assert s["processGroups"] > 5 and s["networkDown"] >= 0
        assert all(v >= 0 for v in s["cores"])
        checks.append("live CPU, per-core, memory, network, process sampling")
        report["initial"] = s
        modules = [m.path for m in psutil.Process(p.pid).memory_maps()]
        assert not any("python3" in x.lower() or "qt6" in x.lower() for x in modules)
        report["modules"] = modules
        checks.append("no Python or Qt loaded")
        command("show")
        assert state()["pid"] == p.pid
        command("toggle")
        assert not state()["visible"]
        command("show")
        assert state()["visible"]
        checks.append("single instance; hide and restore")

        # Use the actual cursor for the regression where details survived Leave.
        s = state()
        h = next(h for h in s["panel"]["hits"] if h["id"] == "process")
        scale = s["panel"]["dpi"] * s["layout"]["scale"]
        rect = s["panel"]["rect"]
        x = rect[0] + int((h["rect"][0] + 20) * scale)
        y = rect[1] + int((h["rect"][1] + 10) * scale)
        u.SetCursorPos(x, y)
        time.sleep(0.9)
        assert state()["detailVisible"], "Hover did not open details"
        d = state()["detail"]["rect"]
        u.SetCursorPos(d[0] + 20, d[1] + 20)
        time.sleep(0.3)
        assert state()["detailVisible"], "Moving into details closed them"
        u.SetCursorPos(5, 5)
        time.sleep(0.35)
        assert not state()["detailVisible"], "Details remained after leaving"
        checks.append("hover row -> detail -> outside auto-dismiss")

        command("settings")
        assert state()["settingsVisible"]
        click("settings", "more")
        s = state()
        assert s["more"] and s["settings"]["rect"][3] - s["settings"]["rect"][1] > 400
        assert any(h["id"] == "openConfig" for h in s["settings"]["hits"])
        click("settings", "appearance")
        assert json.loads(config.read_text())["appearance"] == "light"
        assert json.loads(config.read_text())["customField"] == "preserved"
        click("settings", "appearance")
        click("settings", "appearance")
        checks.append("More expands; settings persist without dropping unknown fields")
        shwnd = own_window("top settings")
        u.SendMessageW(shwnd, 0x100, 0x1B, 0)

        # Invalid edits must not reset settings or overwrite damaged input.
        valid = config.read_text()
        config.write_text("{broken", encoding="utf8")
        time.sleep(1.3)
        assert state()["visible"]
        assert config.read_text() == "{broken"
        config.write_text(valid, encoding="utf8")
        time.sleep(1.2)
        checks.append("invalid config preserved; valid edit reloads")

        elapsed = []
        u.SendMessageW(hwnd, 0x231, 0, 0)
        for i in range(30):
            t = time.perf_counter()
            u.SetWindowPos(hwnd, None, 100 + i, 100 + i, 660 + i, 331 + i, 0x14)
            C.windll.dwmapi.DwmFlush()
            elapsed.append((time.perf_counter() - t) * 1000)
        u.SendMessageW(hwnd, 0x232, 0, 0)
        report["moveResizeMs"] = dict(
            median=sorted(elapsed)[len(elapsed) // 2], maximum=max(elapsed)
        )
        assert max(elapsed) < 250, elapsed
        checks.append("native move/resize responsiveness")

        # Restore a common physical geometry for screenshots and performance.
        u.SetWindowPos(hwnd, None, 100, 100, 660, 331, 0x14)
        time.sleep(0.2)
        command("--capture", OUT / "native-dark.bmp")
        process = psutil.Process(p.pid)
        stats = []
        for name in ("animated", "still", "hidden"):
            if name == "still":
                data = json.loads(config.read_text())
                data["motion"] = False
                config.write_text(json.dumps(data), encoding="utf8")
                time.sleep(1.3)
            elif name == "hidden":
                command("hide")
                time.sleep(1)
            before = process.cpu_times()
            t = time.perf_counter()
            time.sleep(8)
            after = process.cpu_times()
            seconds = time.perf_counter() - t
            one = (
                (after.user + after.system - before.user - before.system)
                / seconds
                * 100
            )
            memory = process.memory_info()
            stats.append(
                dict(
                    state=name,
                    seconds=seconds,
                    cpuOneCorePercent=one,
                    cpuWholeMachinePercent=one / psutil.cpu_count(),
                    workingSetMB=memory.rss / 1048576,
                    privateMB=memory.private / 1048576,
                )
            )
        report["performance"] = stats
        command("show")
        command("quit")
        p.wait(timeout=10)
        assert p.returncode == 0
        checks.append("clean quit")
        report["checks"] = checks
        (OUT / "verification.json").write_text(
            json.dumps(report, indent=2), encoding="utf8"
        )
        print(
            json.dumps(
                {k: v for k, v in report.items() if k not in ("modules", "initial")},
                indent=2,
            )
        )
    finally:
        u.SetCursorPos(cursor.x, cursor.y)
        if p.poll() is None:
            try:
                command("quit")
                p.wait(timeout=8)
            except Exception:
                p.terminate()
                p.wait(timeout=5)


if __name__ == "__main__":
    print("Responsive layout cases:", layouts(), flush=True)
    integration()
