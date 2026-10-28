import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import psutil


def test_single_instance_show_toggle_quit(tmp_path):
    script = Path(__file__).resolve().parents[1] / "ftop_win.py"
    config = tmp_path / "config.json5"
    config.write_text(
        json.dumps(
            dict(
                width=300,
                height=420,
                x=50,
                y=50,
                motion=False,
                menuBar=False,
                floating=True,
            )
        ),
        encoding="utf-8",
    )
    packaged = os.environ.get("FTOP_TEST_EXE")
    cmd = [packaged] if packaged else [sys.executable, str(script)]
    child_env = os.environ.copy()
    if packaged:
        # A frozen build must not discover the development Python environment.
        child_env.pop("PYTHONHOME", None)
        child_env.pop("PYTHONPATH", None)
        windows = os.environ["SystemRoot"]
        child_env["PATH"] = os.path.join(windows, "System32") + os.pathsep + windows
        child_env["APPDATA"] = str(tmp_path / "appdata")
    started = time.perf_counter()
    process = subprocess.Popen(
        cmd + ["show", "--config", str(config)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=subprocess.CREATE_NO_WINDOW,
        env=child_env,
    )
    u = ctypes.windll.user32
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    u.GetWindowThreadProcessId.argtypes = [
        wintypes.HWND,
        ctypes.POINTER(wintypes.DWORD),
    ]
    u.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    u.IsWindowVisible.argtypes = [wintypes.HWND]

    gui_pids = set()

    def visible():
        found = []
        pids = {process.pid}
        try:
            pids.update(
                p.pid for p in psutil.Process(process.pid).children(recursive=True)
            )
        except psutil.Error:
            pass

        def visit(hwnd, _):
            pid = wintypes.DWORD()
            u.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value in pids:
                title = ctypes.create_unicode_buffer(128)
                u.GetWindowTextW(hwnd, title, 128)
                if title.value == "ftop":
                    found.append(bool(u.IsWindowVisible(hwnd)))
                    gui_pids.add(pid.value)
            return True

        u.EnumWindows(callback_type(visit), 0)
        return any(found)

    def wait_for(expected):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if visible() == expected:
                return
            if process.poll() is not None:
                raise AssertionError(process.communicate())
            time.sleep(0.1)
        raise AssertionError(f"Visibility did not become {expected}")

    def command(verb):
        result = subprocess.run(
            cmd + [verb, "--config", str(config)],
            capture_output=True,
            timeout=15,
            env=child_env,
        )
        assert result.returncode == 0, result.stderr

    try:
        wait_for(True)
        startup_seconds = time.perf_counter() - started
        python_dlls = []
        if packaged:
            for pid in gui_pids:
                python_dlls.extend(
                    m.path
                    for m in psutil.Process(pid).memory_maps()
                    if Path(m.path).name.lower() == "python312.dll"
                )
            assert python_dlls
            assert all(
                "_internal" in path.lower() or "_mei" in path.lower()
                for path in python_dlls
            ), python_dlls
        command("toggle")
        wait_for(False)
        command("show")
        wait_for(True)
        command("show")
        assert process.poll() is None
        command("quit")
        process.wait(timeout=10)
        assert process.returncode == 0
        stdout, stderr = process.communicate()
        assert b"Traceback" not in stderr, stderr.decode(errors="replace")
        if packaged and os.environ.get("FTOP_TEST_REPORT"):
            Path(os.environ["FTOP_TEST_REPORT"]).write_text(
                json.dumps(
                    dict(
                        executable=packaged,
                        startup_seconds=round(startup_seconds, 3),
                        python_dlls=python_dlls,
                        minimal_path=True,
                        checks=[
                            "visible window",
                            "toggle hides",
                            "show restores",
                            "single instance",
                            "quit exits",
                        ],
                    ),
                    indent=2,
                ),
                encoding="utf-8",
            )
    finally:
        if process.poll() is None:
            try:
                command("quit")
                process.wait(timeout=5)
            except (subprocess.TimeoutExpired, AssertionError):
                descendants = psutil.Process(process.pid).children(recursive=True)
                for child in descendants:
                    child.terminate()
                process.terminate()
                process.wait(timeout=5)
