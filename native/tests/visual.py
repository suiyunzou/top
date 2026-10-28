"""Desktop-compositor checks with a controlled green backdrop, plus layout captures."""

from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import json
import subprocess
from PySide6.QtWidgets import QApplication, QWidget
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "scratch" / "evidence"
EXE = ROOT / "dist/top.exe"
u = C.windll.user32
u.SetProcessDpiAwarenessContext.argtypes = [C.c_void_p]
u.SetProcessDpiAwarenessContext(C.c_void_p(-4))
u.FindWindowW.argtypes = [W.LPCWSTR, W.LPCWSTR]
u.FindWindowW.restype = W.HWND
u.GetWindowRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
u.SetWindowPos.argtypes = [W.HWND, W.HWND, C.c_int, C.c_int, C.c_int, C.c_int, W.UINT]
u.SendMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
u.SetForegroundWindow.argtypes = [W.HWND]
app = QApplication([])
bg = QWidget(
    None, Qt.WindowType.FramelessWindowHint | Qt.WindowType.WindowStaysOnTopHint
)
bg.setStyleSheet("background:#00aa66;")
bg.setGeometry(50, 50, 1000, 700)
bg.show()
QTest.qWait(200)
config = OUT / "visual-config.json"
config.write_text(
    json.dumps(
        dict(
            width=527,
            height=265,
            x=80,
            y=80,
            floating=True,
            appearance="dark",
            menuBar=False,
            motion=True,
        )
    ),
    encoding="utf8",
)
cmd = [str(EXE), "--config", str(config)]
p = subprocess.Popen(cmd, creationflags=subprocess.CREATE_NO_WINDOW)
rows = []


def command(*args):
    subprocess.run(
        cmd + list(map(str, args)),
        check=True,
        timeout=10,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )


def state():
    command("--inspect", OUT / "visual-state.json")
    return json.loads((OUT / "visual-state.json").read_text())


def desktop(name):
    QTest.qWait(250)
    C.windll.dwmapi.DwmFlush()
    s = state()
    r = s["panel"]["rect"]
    dpi = s["panel"]["dpi"]
    screen = app.primaryScreen()
    swatch = screen.grabWindow(0, 60, 60, 1, 1).toImage().pixelColor(0, 0).name()
    pix = screen.grabWindow(
        0,
        round(r[0] / dpi),
        round(r[1] / dpi),
        round((r[2] - r[0]) / dpi),
        round((r[3] - r[1]) / dpi),
    )
    im = pix.toImage()
    corners = [
        im.pixelColor(x, y).name()
        for x, y in [
            (2, 2),
            (im.width() - 3, 2),
            (2, im.height() - 3),
            (im.width() - 3, im.height() - 3),
        ]
    ]
    rows.append(dict(state=name, background=swatch, corners=corners))
    pix.save(str(OUT / ("desktop-" + name + ".png")))
    assert swatch == "#00aa66", "Test backdrop obscured"
    assert corners == ["#00aa66"] * 4, (name, corners)


try:
    QTest.qWait(3000)
    hwnd = u.FindWindowW(None, "top")
    assert hwnd and p.poll() is None
    desktop("idle")
    u.SendMessageW(hwnd, 0x231, 0, 0)
    u.SetWindowPos(hwnd, None, 125, 125, 660, 331, 0x14)
    desktop("moving")
    u.SendMessageW(hwnd, 0x232, 0, 0)
    desktop("released")
    bg.activateWindow()
    QTest.qWait(200)
    # Keep the native panel above the backdrop while it is inactive.
    u.SetWindowPos(hwnd, W.HWND(-1), 0, 0, 0, 0, 0x13)
    desktop("inactive")
    command("hide")
    command("show")
    desktop("reshown")
    command("settings")
    s = state()
    sh = u.FindWindowW(None, "top settings")
    hit = next(x for x in s["settings"]["hits"] if x["id"] == "more")
    x = int((hit["rect"][0] + hit["rect"][2]) / 2 * s["settings"]["dpi"])
    y = int((hit["rect"][1] + hit["rect"][3]) / 2 * s["settings"]["dpi"])
    u.SendMessageW(sh, 0x202, 0, (y << 16) | x)
    QTest.qWait(200)
    s = state()
    r = s["settings"]["rect"]
    dpi = s["settings"]["dpi"]
    app.primaryScreen().grabWindow(
        0,
        round(r[0] / dpi),
        round(r[1] / dpi),
        round((r[2] - r[0]) / dpi),
        round((r[3] - r[1]) / dpi),
    ).save(str(OUT / "native-settings.png"))
    u.SendMessageW(sh, 0x100, 0x1B, 0)
    for name, w, h in [
        ("wide", 950, 450),
        ("compact", 700, 350),
        ("corner", 425, 263),
        ("strip", 575, 80),
        ("vertical", 150, 400),
        ("micro", 95, 35),
    ]:
        u.SetWindowPos(hwnd, None, 100, 100, w, h, 0x14)
        QTest.qWait(350)
        command("--capture", OUT / (name + ".bmp"))
        s = state()
        r = s["panel"]["rect"]
        assert (r[2] - r[0], r[3] - r[1]) == (w, h), (name, r)
        expected = {
            "wide": "full",
            "compact": "full",
            "corner": "small",
            "strip": "strip",
            "vertical": "micro",
            "micro": "micro",
        }
        assert s["layout"]["tier"] == expected[name], (name, s["layout"])
        rows.append(dict(state=name, layout=s["layout"]))
    # Light palette, through the same hot-reload path as an editor.
    data = json.loads(config.read_text())
    data["appearance"] = "light"
    config.write_text(json.dumps(data), encoding="utf8")
    u.SetWindowPos(hwnd, None, 100, 100, 660, 331, 0x14)
    QTest.qWait(1500)
    command("--capture", OUT / "native-light.bmp")
    (OUT / "visual-checks.json").write_text(json.dumps(rows, indent=2), encoding="utf8")
    print(
        "Desktop corners passed in five states; six layout captures and light/settings captures saved."
    )
finally:
    if p.poll() is None:
        command("quit")
        p.wait(timeout=10)
    bg.close()
