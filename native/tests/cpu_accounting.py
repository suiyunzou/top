"""One busy Python thread should occupy about 1/N of the whole-machine capacity."""
from pathlib import Path
import json
import os
import statistics
import subprocess
import sys
import time
import psutil

root = Path(__file__).resolve().parents[1]
exe = Path(os.environ.get("FTOP_TEST_EXE", root / "dist/top.exe"))
output = root / "scratch/evidence/cpu-accounting.json"
flags = subprocess.CREATE_NO_WINDOW
# A Windows venv python.exe can be a launcher whose own CPU time stays zero.
worker = subprocess.Popen([sys._base_executable, "-c",
    "import time\nend=time.monotonic()+18\nwhile time.monotonic()<end: pass"], creationflags=flags)
try:
    process = psutil.Process(worker.pid)
    time.sleep(.5)
    initial = process.cpu_times()
    start = time.perf_counter()
    subprocess.run([str(exe), "--sample-report", str(output), "10"],
                   creationflags=flags, check=True, timeout=30)
    elapsed = time.perf_counter() - start
    end = process.cpu_times()
    measured = (end.user + end.system - initial.user - initial.system) / elapsed * 100 / psutil.cpu_count()
    samples = json.loads(output.read_text())
    observed = statistics.median(x["processMax"] for x in samples[2:])
    report = dict(expectedPercent=100 / psutil.cpu_count(), independentPercent=measured,
                  ftopMedianTopPercent=observed, samples=samples)
    output.write_text(json.dumps(report, indent=2), encoding="utf8")
    assert 0 < measured <= 100 / psutil.cpu_count() * 1.05, report
    assert abs(observed - measured) < 1.5, (observed, measured)
    assert all(0 <= x["processMax"] <= 100 for x in samples)
    print(json.dumps({k:v for k,v in report.items() if k != "samples"}, indent=2))
finally:
    if worker.poll() is None:
        worker.terminate()
    worker.wait(timeout=5)
