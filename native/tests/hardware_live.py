"""One short elevated run: raw-counter arithmetic, OS references and the real UI.

Uses the installed signed driver. Does not install software or change driver policy.
Run with the test venv Python from an elevated terminal. Leaves the normal UI open.
"""
from pathlib import Path
import ctypes
import json
import math
import statistics
import subprocess
import sys
import threading
import time
import psutil

root = Path(__file__).resolve().parents[1]
exe = root / 'dist/top.exe'
evidence = root / 'scratch/evidence'
flags = subprocess.CREATE_NO_WINDOW

def save(name, data):
    (evidence / name).write_text(json.dumps(data, indent=2), encoding='utf8')

def command(*args):
    return subprocess.run([str(exe), *map(str, args)], creationflags=flags,
                          check=True, timeout=12)

def close(a, b, tolerance=1e-7):
    assert math.isfinite(a) and abs(a-b) <= tolerance, (a, b)

def busy():
    time.sleep(5)
    subprocess.run([sys._base_executable, '-c',
        'import time\nend=time.perf_counter()+7\nwhile time.perf_counter()<end: pass'],
        creationflags=flags, check=True, timeout=10)

def main():
    assert ctypes.windll.shell32.IsUserAnAdmin(), 'Driver needs elevation'
    worker = threading.Thread(target=busy)
    worker.start()
    refs = []
    probe = subprocess.Popen([str(exe), '--sample-report', str(evidence / 'hardware-samples.json'), '20'],
                             creationflags=flags)
    start = time.perf_counter()
    while probe.poll() is None:
        vm = psutil.virtual_memory()
        refs.append(dict(when=time.perf_counter(), total=vm.total, used=vm.total-vm.available))
        if time.perf_counter()-start > 40:
            probe.terminate()
            raise TimeoutError('Sample probe exceeded 40 seconds')
        time.sleep(.25)
    assert probe.returncode == 0, probe.returncode
    worker.join(timeout=10)
    samples = json.loads((evidence / 'hardware-samples.json').read_text())
    assert len(samples) == 20
    assert samples[0]['sensorState'] == 'warming_up'
    memory_errors = []
    core_errors = []
    for n, s in enumerate(samples):
        assert s['sensorError'] == 0
        assert s['thermalRaw'] & (1 << 31)
        close(s['cpuPackageCelsius'], ((s['targetRaw'] >> 16) & 255) - ((s['thermalRaw'] >> 16) & 127))
        nearest = min(refs, key=lambda r: abs(r['when']-s['energyTime']))
        close(s['memoryTotal'], nearest['total'])
        memory_errors.append(abs(s['memoryUsed']-nearest['used']) / 1048576)
        assert memory_errors[-1] < 256, memory_errors[-1]
        assert s['pagefileUsed'] >= 0 and s['pagefileUsed'] % 4096 == 0
        assert 0 <= s['gpu'] <= 100 and 0 < s['gpuCelsius'] < 125
        assert 0 <= s['gpuMemory'] <= s['gpuTotal']
        assert all(v == -1 or 0 <= v <= 100 for v in s['cores'])
        assert all(v == -1 or 0 <= v <= 6 for v in s['ghz'])
        if not n:
            assert s['cpu'] == s['cpuPackageWatts'] == s['networkDown'] == -1
            continue
        assert s['sensorState'] == 'ready'
        p = samples[n-1]
        joules = ((s['energyRaw']-p['energyRaw']) % (2**32)) * 2**(-((s['unitRaw'] >> 8) & 31))
        close(s['cpuPackageWatts'], joules/(s['energyTime']-p['energyTime']))
        delta = [a-b for a, b in zip(s['cpuRaw'], p['cpuRaw'])]
        close(s['cpu'], 100*(1-delta[0]/(delta[1]+delta[2])))
        for v, a, b in zip(s['cores'], s['coresRaw'], p['coresRaw']):
            d = [x-y for x, y in zip(a, b)]
            close(v, 100*(1-d[0]/(d[1]+d[2])))
        core_errors.append(abs(s['cpu']-statistics.mean(s['cores'])))
        old = {row[0]: row[1:] for row in p['networkRaw']}
        delta_net = [0, 0]
        for row in s['networkRaw']:
            if row[0] in old:
                for k in range(2):
                    delta_net[k] += max(0, row[k+1]-old[row[0]][k])
        dt = s['networkTime']-p['networkTime']
        close(s['networkDown'], delta_net[0]/dt)
        close(s['networkUp'], delta_net[1]/dt)
    assert max(core_errors) < 2, core_errors
    save('hardware-memory-reference.json', refs)
    # The existing NVIDIA command is an independent front end to the installed driver.
    gpu_ref = subprocess.run(['nvidia-smi', '--query-gpu=temperature.gpu,memory.total,memory.used,power.draw,clocks.gr',
                              '--format=csv,noheader,nounits'], capture_output=True, text=True,
                             creationflags=flags, check=True, timeout=10).stdout.strip()
    ui = subprocess.Popen([str(exe), 'show'], creationflags=flags)
    ui_path = evidence / 'hardware-ui.json'
    deadline = time.monotonic()+15
    while time.monotonic() < deadline:
        time.sleep(1)
        command('--inspect', ui_path)
        state = json.loads(ui_path.read_text())
        if state['sensorState'] == 'ready':
            break
    assert state['visible'] and state['sensorState'] == 'ready', state['sensorState']
    command('--capture', evidence / 'hardware-ui.bmp')
    loaded = [Path(m.path).name.lower() for m in psutil.Process(state['pid']).memory_maps()]
    assert 'pawniolib.dll' not in loaded
    report = dict(samples=len(samples), readySamples=sum(s['sensorState']=='ready' for s in samples),
                  checks=['temperature from raw MSR', 'power from raw energy and QPC',
                          'total and per-core CPU from raw OS counters', 'network from interface counters',
                          'physical memory against psutil', 'first sample unknown', 'elevated UI ready',
                          'PawnIOLib.dll not loaded'],
                  temperatureRange=[min(s['cpuPackageCelsius'] for s in samples), max(s['cpuPackageCelsius'] for s in samples)],
                  wattsRange=[min(s['cpuPackageWatts'] for s in samples[1:]), max(s['cpuPackageWatts'] for s in samples[1:])],
                  sensorMillisecondsRange=[min(s['sensorMs'] for s in samples[1:]), max(s['sensorMs'] for s in samples[1:])],
                  maxMemoryDifferenceMiB=max(memory_errors), maxCpuCoreMeanDifference=max(core_errors),
                  nvidiaSmi=gpu_ref, ui={k:state[k] for k in ['cpu','cpuPackageCelsius','cpuPackageWatts','sensorState',
                    'memoryTotal','memoryUsed','pagefileUsed','gpu','gpuMemory','gpuTotal','gpuCelsius','gpuWatts','gpuGHz','workingSetMB']},
                  scope='This i7-10750H / GTX 1650 machine. RAPL package estimate; no external calibration.')
    save('hardware-validation.json', report)
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        save('hardware-validation-error.json', dict(error=repr(e)))
        raise
