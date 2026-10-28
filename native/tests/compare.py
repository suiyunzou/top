"""Sequential A/B measurement with matching modules, geometry and refresh rate."""

from pathlib import Path
import json
import os
import subprocess
import time
import psutil

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "scratch" / "evidence"
results = []
for name, exe in [
    ("native", ROOT / "dist/top.exe"),
    ("python-qt", ROOT.parent / "python/dist-ftop/ftop-windows/ftop.exe"),
]:
    config = OUT / (name + "-comparison.json")
    settings = dict(
        width=527,
        height=265,
        x=80,
        y=80,
        floating=True,
        menuBar=False,
        appearance="dark",
        motion=True,
        interval=1,
        modules=["cpu", "memory", "network", "processes"],
        hidden=[],
        maxProcesses=24,
        processSort="cpu",
    )
    config.write_text(json.dumps(settings), encoding="utf8")
    cmd = [str(exe), "--config", str(config)]
    env = os.environ.copy()
    env["APPDATA"] = str(OUT / "comparison-appdata")
    env["PATH"] = (
        os.environ["SystemRoot"] + ";" + os.environ["SystemRoot"] + "\\System32"
    )
    p = subprocess.Popen(cmd, env=env, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        time.sleep(6)
        assert p.poll() is None, (name, p.returncode)
        proc = psutil.Process(p.pid)
        readings = []
        for state in ("animated", "still", "hidden"):
            if state == "still":
                settings["motion"] = False
                config.write_text(json.dumps(settings), encoding="utf8")
                time.sleep(3)
            elif state == "hidden":
                subprocess.run(
                    cmd + ["toggle"],
                    check=True,
                    timeout=10,
                    env=env,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                time.sleep(3)
            before = proc.cpu_times()
            started = time.perf_counter()
            memory = []
            for _ in range(20):
                time.sleep(1)
                m = proc.memory_info()
                memory.append((m.rss / 1048576, m.private / 1048576))
            after = proc.cpu_times()
            elapsed = time.perf_counter() - started
            one = (
                (after.user + after.system - before.user - before.system)
                / elapsed
                * 100
            )
            item = dict(
                state=state,
                seconds=round(elapsed, 2),
                cpuOneCorePercent=round(one, 3),
                cpuWholeMachinePercent=round(one / psutil.cpu_count(), 3),
                workingSetMB=round(sum(x[0] for x in memory) / len(memory), 2),
                peakWorkingSetMB=round(max(x[0] for x in memory), 2),
                privateMB=round(sum(x[1] for x in memory) / len(memory), 2),
            )
            readings.append(item)
            print(name, json.dumps(item), flush=True)
        results.append(
            dict(implementation=name, exeBytes=exe.stat().st_size, readings=readings)
        )
    finally:
        if p.poll() is None:
            subprocess.run(
                cmd + ["quit"],
                timeout=10,
                env=env,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
            p.wait(timeout=10)
(OUT / "comparison.json").write_text(
    json.dumps(
        dict(
            logicalCPUs=psutil.cpu_count(),
            conditions="527x265 DIP, 1 second refresh, all modules, dark, no tray; sequential 20 seconds per state",
            results=results,
        ),
        indent=2,
    ),
    encoding="utf8",
)
