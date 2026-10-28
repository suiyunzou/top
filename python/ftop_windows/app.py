"""CLI, single-instance transport, and reproducible render/performance tools."""

import argparse
import json
import os
from pathlib import Path
import sys
import tempfile
import time

from . import VERSION, UPSTREAM
from .core import ConfigStore


def doctor():
    import psutil
    from .sensors import Sampler
    from .core import normalize

    sampler = Sampler(normalize({}), start=False)
    try:
        sampler.sample()
        time.sleep(1.05)
        s = sampler.sample()
        print(f"ftop Windows {VERSION}\nReference: Nongfsq/ftop@{UPSTREAM}")
        print(
            f"CPU: {psutil.cpu_count(logical=False)} physical / {sampler.n} logical · {sampler.topology_source}"
        )
        print(f"RAM: {psutil.virtual_memory().total / 1024**3:.1f} GiB")
        freqs = [x for x in s["freqs"] if x is not None]
        print(
            f"PDH frequency: {len(freqs)}/{sampler.n} available (estimated; not an absolute clock sensor)"
        )
        print(
            f"GPU: {s['gpu_name'] or 'unavailable (NVML required; Intel/AMD backend not implemented)'}"
        )
        print(
            f"GPU usage={s['gpu']}%, memory={s['gpu_memory']} GiB, temperature={s['gpu_temp']} C, power={s['gpu_power']} W"
        )
        print(f"Memory pressure: {s['pressure']} (Windows resource notification)")
        print(
            "CPU temperature / machine power / compressed bytes: unavailable via current backend"
        )
        print(
            f"Process groups: {len(s['groups'])}; CPU uses 100% per core; RAM is working set"
        )
        print("Sampler cost (ms):", json.dumps(sampler.cost))
        print("Updates: manual (upstream releases contain macOS binaries)")
        if s["errors"]:
            print("Sensor errors:", s["errors"])
    finally:
        if sampler.pdh:
            sampler.pdh.close()
        if sampler.nvml and sampler.nvml.ok:
            sampler.nvml.lib.nvmlShutdown()


class FixtureSampler:
    def __init__(self, config):
        from .sensors import sample_fixture
        from .native import make_layout

        self.n = 12
        self.cores = [(0, True, [i, i + 1]) for i in range(0, 12, 2)]
        self.order, self.info = make_layout(self.cores)
        self.snap = sample_fixture()
        self.mode = "full"
        self.hold = False

    def configure(self, c):
        pass

    def set_mode(self, m):
        self.mode = m

    def stop(self):
        pass


def render(out):
    from PySide6.QtWidgets import QApplication
    from .ui import Panel

    app = QApplication.instance() or QApplication([])
    directory = Path(out)
    directory.mkdir(parents=True, exist_ok=True)
    entries = []
    with tempfile.TemporaryDirectory(prefix="ftop-render-") as temp:
        store = ConfigStore(Path(temp) / "config.json5")
        store.save(dict(appearance="dark", language="zh", motion=False, opacity=1.0))
        panel = Panel(store, FixtureSampler(store.data), testing=True)
        for appearance in ("dark", "light"):
            panel.change_config({"appearance": appearance})
            for w, h in [
                (96, 32),
                (400, 30),
                (760, 30),
                (94, 330),
                (280, 150),
                (300, 420),
                (480, 300),
                (660, 230),
                (1000, 500),
                (1400, 800),
            ]:
                panel.resize(w, h)
                panel.relayout(snap=True)
                panel.show()
                app.processEvents()
                panel.accept_snapshot()
                app.processEvents()
                name = f"{w}x{h}-{panel.layout.candidate.tier}-{appearance}.png"
                panel.grab().save(str(directory / name))
                entries.append(
                    dict(
                        request=[w, h],
                        actual=[panel.width(), panel.height()],
                        tier=panel.layout.candidate.tier,
                        columns=panel.layout.candidate.columns,
                        rows=panel.layout.candidate.count,
                        scale=panel.layout.scale,
                        path=name,
                    )
                )
            panel.controls.more = True
            panel.controls.rebuild()
            panel.controls.show()
            app.processEvents()
            panel.controls.grab().save(str(directory / f"settings-{appearance}.png"))
            panel.controls.hide()
        panel.card.present(panel.card_lines("0"), panel.pos(), "0")
        app.processEvents()
        panel.card.grab().save(str(directory / "process-card.png"))
        panel.hide()
        panel.card.hide()
        (directory / "renders.json").write_text(
            json.dumps(entries, indent=2), encoding="utf-8"
        )
    print(
        f"Rendered {len(entries)} panels + settings + process card to {directory.resolve()}"
    )


def benchmark(out, seconds):
    import psutil
    from PySide6.QtWidgets import QApplication
    from .ui import Panel
    from .sensors import Sampler

    app = QApplication.instance() or QApplication([])
    process = psutil.Process()
    result = dict(
        version=VERSION,
        python=sys.version,
        logical_cpus=psutil.cpu_count(),
        duration_per_state=seconds,
        states=[],
    )
    with tempfile.TemporaryDirectory(prefix="ftop-perf-") as temp:
        store = ConfigStore(Path(temp) / "config.json5")
        store.save(
            dict(
                appearance="dark",
                motion=True,
                interval=1.0,
                width=660,
                height=400,
                floating=True,
            )
        )
        before = time.perf_counter()
        sampler = Sampler(store.data)
        panel = Panel(store, sampler, testing=True)
        panel.show()
        panel.heartbeat.start()
        from . import native

        result["backdrop_available"] = native.apply_backdrop(
            int(panel.winId()), panel.theme.dark
        )
        app.processEvents()
        result["panel_init_ms"] = (time.perf_counter() - before) * 1000

        def pump(duration):
            end = time.monotonic() + duration
            while time.monotonic() < end:
                app.processEvents()
                time.sleep(0.005)

        pump(3)
        for state in ("full-motion", "full-no-motion", "strip", "hidden", "covered"):
            if state == "full-no-motion":
                panel.change_config({"motion": False})
                native.apply_backdrop(int(panel.winId()), panel.theme.dark)
            if state == "strip":
                panel.resize(400, 30)
                panel.relayout(snap=True)
            if state == "hidden":
                panel.hide_panel()
            cover = None
            if state == "covered":
                from PySide6.QtWidgets import QWidget
                from PySide6.QtCore import Qt

                panel.show_panel()
                panel.resize(660, 400)
                panel.relayout(snap=True)
                cover = QWidget(
                    None,
                    Qt.WindowType.Tool
                    | Qt.WindowType.FramelessWindowHint
                    | Qt.WindowType.WindowStaysOnTopHint,
                )
                cover.setStyleSheet("background:#345678")
                cover.setGeometry(panel.geometry().adjusted(-25, -25, 25, 25))
                cover.show()
                cover.raise_()
            pump(2)
            start = time.perf_counter()
            ct = process.cpu_times()
            before_cpu = ct.user + ct.system
            pump(seconds)
            elapsed = time.perf_counter() - start
            ct = process.cpu_times()
            cpu = (ct.user + ct.system - before_cpu) / elapsed * 100
            mem = process.memory_info()
            result["states"].append(
                dict(
                    state=state,
                    cpu_one_core_percent=round(cpu, 3),
                    cpu_task_manager_percent=round(cpu / (psutil.cpu_count() or 1), 3),
                    working_set_mb=round(mem.rss / 1048576, 1),
                    private_mb=round(getattr(mem, "private", 0) / 1048576, 1),
                    sampler_mode=sampler.mode,
                    occluded=panel._occluded,
                    sampler_cost_ms=sampler.cost.copy(),
                )
            )
            if cover:
                cover.hide()
                cover.deleteLater()
        panel.hide()
        panel.tray.hide()
        sampler.stop()
        panel.heartbeat.stop()
        panel.frame.stop()
    path = Path(out)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    from . import native
    import traceback

    previous_hook = sys.excepthook

    def report_exception(kind, value, tb):
        native.log_error("".join(traceback.format_exception(kind, value, tb)))
        if sys.stderr:
            previous_hook(kind, value, tb)

    sys.excepthook = report_exception
    parser = argparse.ArgumentParser(
        description="ftop Windows — floating system monitor"
    )
    parser.add_argument(
        "command",
        nargs="?",
        default="show",
        choices=(
            "show",
            "toggle",
            "quit",
            "doctor",
            "version",
            "update",
            "render",
            "benchmark",
        ),
    )
    parser.add_argument("--config", help="Isolated configuration path")
    parser.add_argument(
        "--output",
        default=str((Path(sys.executable).parent if getattr(sys, "frozen", False)
                     else Path(__file__).resolve().parents[1]) / "scratch" / "evidence" / "renders"),
    )
    parser.add_argument("--seconds", type=float, default=12)
    args = parser.parse_args()
    if args.command == "version":
        print(f"ftop Windows {VERSION} (upstream {UPSTREAM[:7]})")
        return 0
    if args.command == "update":
        print(
            "Windows port uses manual updates. Upstream macOS packages cannot update this Windows application."
        )
        return 0
    if args.command == "doctor":
        doctor()
        return 0
    if args.command == "render":
        render(args.output)
        return 0
    if args.command == "benchmark":
        benchmark(args.output, max(2, args.seconds))
        return 0
    from PySide6.QtWidgets import QApplication
    from PySide6.QtCore import QLockFile
    from PySide6.QtNetwork import QLocalServer, QLocalSocket
    import hashlib

    config_path = (
        Path(args.config or os.environ.get("FTOP_CONFIG"))
        if args.config or os.environ.get("FTOP_CONFIG")
        else Path(os.environ.get("APPDATA", str(Path.home()))) / "ftop" / "config.json5"
    )
    identity = hashlib.sha256(
        str(config_path.resolve()).casefold().encode()
    ).hexdigest()[:20]
    name = "ftop-win-" + identity
    app = QApplication(sys.argv)
    from PySide6.QtGui import QIcon

    app.setApplicationName("ftop")
    app.setWindowIcon(QIcon(str(Path(__file__).with_name("assets") / "ftop.ico")))
    app.setQuitOnLastWindowClosed(False)
    sock = QLocalSocket()
    sock.connectToServer(name)
    if sock.waitForConnected(400):
        sock.write(args.command.encode() + b"\n")
        sock.waitForBytesWritten(1000)
        sock.disconnectFromServer()
        return 0
    if args.command == "quit":
        return 0
    lock = QLockFile(str(Path(tempfile.gettempdir()) / (name + ".lock")))
    if not lock.tryLock(100):
        # The first instance may still be creating the server.
        for _ in range(20):
            sock.abort()
            sock.connectToServer(name)
            if sock.waitForConnected(100):
                sock.write(args.command.encode() + b"\n")
                sock.waitForBytesWritten(1000)
                return 0
            time.sleep(0.05)
        print("ftop is already starting; try again.")
        return 1
    QLocalServer.removeServer(name)
    server = QLocalServer()
    server.setSocketOptions(QLocalServer.SocketOption.UserAccessOption)
    if not server.listen(name):
        raise RuntimeError(server.errorString())
    from .sensors import Sampler
    from .ui import Panel

    store = ConfigStore(config_path)
    sampler = Sampler(store.data)
    panel = Panel(store, sampler)
    clients = []

    def receive():
        while server.hasPendingConnections():
            client = server.nextPendingConnection()
            clients.append(client)

            def read(c=client):
                if not c.canReadLine():
                    return
                command = bytes(c.readLine()).decode("ascii", "ignore").strip()
                if command == "show":
                    panel.show_panel()
                elif command == "toggle":
                    panel.toggle()
                elif command == "quit":
                    panel.quit()
                c.disconnectFromServer()

            client.readyRead.connect(read)
            client.disconnected.connect(
                lambda c=client: (
                    clients.remove(c) if c in clients else None,
                    c.deleteLater(),
                )
            )
            read()

    server.newConnection.connect(receive)
    app.aboutToQuit.connect(sampler.stop)
    panel.show_panel()
    try:
        return app.exec()
    finally:
        server.close()
        lock.unlock()
