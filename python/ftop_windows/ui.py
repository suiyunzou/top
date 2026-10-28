"""Native Qt panel. Cached sample artwork; only bars/arcs animate between samples."""

import locale
import math
import os
import time

from PySide6.QtCore import Qt, QTimer, QRectF, QPointF, QPoint, QFileInfo, QEvent
from PySide6.QtGui import (
    QColor,
    QPainter,
    QPen,
    QFont,
    QFontMetricsF,
    QPixmap,
    QIcon,
    QPainterPath,
    QRegion,
    QCursor,
)
from PySide6.QtWidgets import (
    QApplication,
    QWidget,
    QSystemTrayIcon,
    QFileIconProvider,
    QVBoxLayout,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QComboBox,
    QPushButton,
    QAbstractButton,
    QScrollArea,
    QFrame,
)

from .core import LayoutEngine, Follow, shown, number, rate, memory
from . import native


PALETTES = {
    "sea": (("#1F5FE0", "#0B8F7E", "#7A4FD6"), ("#5B9BFF", "#45D1BD", "#B294FF")),
    "graphite": (("#1F2937", "#6B7280", "#8A6D2F"), ("#F3F4F6", "#9AA1AD", "#C9B98F")),
    "warm": (("#DC4A2B", "#B9770A", "#B83280"), ("#FF8468", "#FFC857", "#FF8FC4")),
}


def font(size, strong=False):
    f = QFont("Segoe UI")
    f.setPixelSize(max(7, round(size)))
    f.setWeight(QFont.Weight.DemiBold if strong else QFont.Weight.Normal)
    return f


def alpha(color, value):
    c = QColor(color)
    c.setAlphaF(value)
    return c


def round_window(widget, radius):
    # Clip the native surface too: Win10 Acrylic ignores painted alpha corners.
    path = QPainterPath()
    radius = min(radius, widget.width() / 2, widget.height() / 2)
    path.addRoundedRect(QRectF(widget.rect()), radius, radius)
    widget.setMask(QRegion(path.toFillPolygon().toPolygon()))


def glyph(p, name, rect, color):
    """One 16-unit vector family; no platform-dependent emoji/font glyphs."""
    p.save()
    p.translate(rect.x(), rect.y())
    p.scale(rect.width() / 16, rect.height() / 16)
    p.setPen(
        QPen(
            QColor(color),
            1.5,
            Qt.PenStyle.SolidLine,
            Qt.PenCapStyle.RoundCap,
            Qt.PenJoinStyle.RoundJoin,
        )
    )
    p.setBrush(Qt.BrushStyle.NoBrush)

    def line(x, y, a, b):
        p.drawLine(QPointF(x, y), QPointF(a, b))

    if name in ("cpu", "memory"):
        p.drawRoundedRect(QRectF(3, 4, 10, 8), 1, 1)
        for x in (5, 8, 11):
            line(x, 2, x, 4)
            line(x, 12, x, 14)
        if name == "cpu":
            p.drawRect(QRectF(5, 6, 6, 4))
            line(1, 6, 3, 6)
            line(13, 6, 15, 6)
            line(1, 10, 3, 10)
            line(13, 10, 15, 10)
    elif name == "gpu":
        for y in (2, 6, 10):
            path = QPainterPath()
            path.moveTo(2, y + 2)
            path.lineTo(8, y - 1)
            path.lineTo(14, y + 2)
            path.lineTo(8, y + 5)
            path.closeSubpath()
            p.drawPath(path)
    elif name in ("down", "up", "network"):
        if name == "network":
            line(4, 2, 4, 14)
            line(1, 5, 4, 2)
            line(4, 2, 7, 5)
            line(12, 2, 12, 14)
            line(9, 11, 12, 14)
            line(12, 14, 15, 11)
        else:
            y, end = (2, 13) if name == "down" else (14, 3)
            leg = end - 5 if name == "down" else end + 5
            line(8, y, 8, end)
            line(3, leg, 8, end)
            line(8, end, 13, leg)
    elif name == "temperature":
        p.drawRoundedRect(QRectF(6, 1, 4, 10), 2, 2)
        p.drawEllipse(QRectF(4.5, 8, 7, 7))
        line(8, 5, 8, 11)
    elif name in ("power", "machine"):
        p.drawRect(QRectF(3, 4, 10, 8))
        line(1, 13, 15, 13)
    elif name == "pin":
        line(4, 2, 12, 2)
        line(6, 2, 6, 7)
        line(10, 2, 10, 7)
        line(6, 7, 3, 11)
        line(3, 11, 13, 11)
        line(13, 11, 10, 7)
        line(8, 11, 8, 15)
    elif name == "hide":
        p.drawEllipse(QRectF(1, 4, 14, 8))
        line(2, 2, 14, 14)
    elif name == "quit":
        p.drawArc(QRectF(2, 2, 12, 12), 130 * 16, 280 * 16)
        line(8, 1, 8, 8)
    elif name == "motion":
        path = QPainterPath()
        path.moveTo(1, 9)
        path.cubicTo(5, -1, 9, 17, 15, 7)
        p.drawPath(path)
    elif name == "tray":
        p.drawRoundedRect(QRectF(1, 3, 14, 11), 2, 2)
        line(1, 6, 15, 6)
    elif name == "more":
        line(2, 5, 8, 11)
        line(8, 11, 14, 5)
    elif name == "less":
        line(2, 11, 8, 5)
        line(8, 5, 14, 11)
    elif name == "processes":
        for y in (3, 8, 13):
            p.drawEllipse(QRectF(1, y - 1, 2, 2))
            line(6, y, 15, y)
    else:
        p.drawEllipse(QRectF(5, 5, 6, 6))
        for i in range(8):
            a = i * math.pi / 4
            line(
                8 + 5 * math.cos(a),
                8 + 5 * math.sin(a),
                8 + 7 * math.cos(a),
                8 + 7 * math.sin(a),
            )
    p.restore()


class Theme:
    def __init__(self, config):
        self.dark = (
            config["appearance"] == "dark"
            or config["appearance"] == "auto"
            and not system_light()
        )
        self.cpu, self.eff, self.gpu = PALETTES[config["palette"]][int(self.dark)]
        self.ink = "#F3F4F6" if self.dark else "#111827"
        self.secondary = "#A5A8B3" if self.dark else "#656A74"
        self.bg = "#191C27" if self.dark else "#EFF1F7"
        self.card = "#292D38" if self.dark else "#F8F9FC"
        self.green = "#4FD17F" if self.dark else "#1C9A50"
        self.warning = "#F0B54A" if self.dark else "#B26A00"
        self.critical = "#FF6B5E" if self.dark else "#D23F31"

    def pressure(self, state):
        return {
            "normal": self.green,
            "warning": self.warning,
            "critical": self.critical,
        }.get(state, self.secondary)


def system_light(*, taskbar=False):
    try:
        import winreg

        with winreg.OpenKey(
            winreg.HKEY_CURRENT_USER,
            r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
        ) as k:
            setting = "SystemUsesLightTheme" if taskbar else "AppsUseLightTheme"
            return winreg.QueryValueEx(k, setting)[0] == 1
    except OSError:
        return False


class DetailPopup(QWidget):
    def __init__(self, panel, card=False):
        super().__init__(
            panel,
            Qt.WindowType.Tool
            | Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowStaysOnTopHint,
        )
        self.panel, self.card = panel, card
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.lines = []
        self.key = None
        self._outside_since = None
        self._dismiss_timer = QTimer(self)
        self._dismiss_timer.setInterval(60)
        self._dismiss_timer.timeout.connect(self.check_pointer)

    def present(self, lines, anchor, key=None):
        if not lines:
            self.hide()
            return
        self.lines, self.key = lines, key
        width = min(
            460,
            max(
                QFontMetricsF(font(12, i == 0)).horizontalAdvance(s)
                for i, s in enumerate(lines)
            )
            + 28,
        )
        self.resize(math.ceil(width), 24 + len(lines) * 22)
        screen = QApplication.screenAt(anchor) or QApplication.primaryScreen()
        area = screen.availableGeometry()
        x, y = anchor.x() + 12, anchor.y() + 12
        if x + self.width() > area.right():
            x = anchor.x() - self.width() - 12
        if y + self.height() > area.bottom():
            y = anchor.y() - self.height() - 12
        self.move(
            max(area.left(), min(x, area.right() - self.width())),
            max(area.top(), min(y, area.bottom() - self.height())),
        )
        self.show()
        if self.card and not self._dismiss_timer.isActive():
            self._outside_since = None
            self._dismiss_timer.start()
        self.update()

    def check_pointer(self):
        pos = QCursor.pos()
        source = self.panel.hit(self.panel.mapFromGlobal(pos))
        if self.geometry().contains(pos) or source == ("process", self.key):
            self._outside_since = None
            return
        now = time.monotonic()
        if self._outside_since is None:
            self._outside_since = now
        elif now - self._outside_since >= 0.12:
            # Allow crossing the small gap from the row into its card.
            self.hide()

    def hideEvent(self, event):
        self._dismiss_timer.stop()
        self._outside_since = None
        if self.card:
            self.panel.sampler.hold = False
        super().hideEvent(event)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        t = self.panel.theme
        p.setPen(QPen(alpha(t.ink, 0.15), 1))
        p.setBrush(QColor(t.card))
        p.drawRoundedRect(QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12)
        for i, s in enumerate(self.lines):
            p.setFont(font(12, i == 0))
            p.setPen(QColor(t.ink if i < 2 else t.secondary))
            text = QFontMetricsF(p.font()).elidedText(
                s, Qt.TextElideMode.ElideRight, self.width() - 28
            )
            p.drawText(
                QRectF(14, 12 + i * 22, self.width() - 28, 22),
                Qt.AlignmentFlag.AlignVCenter,
                text,
            )

    def mousePressEvent(self, _):
        self.hide()
        self.panel.sampler.hold = False


class DiscButton(QAbstractButton):
    def __init__(
        self, panel, glyph_name, label, callback, active=False, palette=None, dim=False
    ):
        super().__init__()
        self.panel = panel
        self.glyph_name = glyph_name
        self.label = label
        self.active = active
        self.palette = palette
        self.dim = dim
        self.setFixedSize(54, 64)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.clicked.connect(callback)
        self.setAccessibleName(label)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        t = self.panel.theme
        box = QRectF(12, 3, 30, 30)
        if self.isDown():
            box = box.adjusted(1.5, 1.5, -1.5, -1.5)
        p.setPen(Qt.PenStyle.NoPen)
        if self.palette:
            a, b, _ = PALETTES[self.palette][int(t.dark)]
            p.setBrush(QColor(a))
            p.drawPie(box, 45 * 16, 180 * 16)
            p.setBrush(QColor(b))
            p.drawPie(box, 225 * 16, 180 * 16)
            if self.active:
                p.setPen(QPen(QColor(t.ink), 2))
                p.setBrush(Qt.BrushStyle.NoBrush)
                p.drawEllipse(box.adjusted(-3, -3, 3, 3))
        else:
            p.setBrush(
                QColor("#879BFA")
                if self.active
                else alpha(t.ink, 0.14 if self.underMouse() else 0.10)
            )
            p.drawEllipse(box)
            color = t.bg if self.active else t.secondary
            if self.dim:
                p.setOpacity(0.4)
            glyph(
                p,
                "less"
                if self.glyph_name == "more" and self.active
                else self.glyph_name,
                QRectF(box.center().x() - 7.5, box.center().y() - 7.5, 15, 15),
                color,
            )
            p.setOpacity(1)
        p.setPen(QColor(t.ink if self.active else t.secondary))
        p.setFont(font(10))
        p.drawText(
            QRectF(0, 40, 54, 20),
            Qt.AlignmentFlag.AlignHCenter | Qt.AlignmentFlag.AlignTop,
            self.label,
        )

    def enterEvent(self, e):
        self.update()
        super().enterEvent(e)

    def leaveEvent(self, e):
        self.update()
        super().leaveEvent(e)


class Controls(QWidget):
    def __init__(self, panel):
        super().__init__(panel, Qt.WindowType.Popup | Qt.WindowType.FramelessWindowHint)
        self.panel = panel
        self.more = False
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.outer = QVBoxLayout(self)
        self.outer.setContentsMargins(19, 18, 19, 18)
        self.outer.setSpacing(10)
        self.rebuild()

    def rebuild(self):
        while self.outer.count():
            item = self.outer.takeAt(0)
            if item.widget():
                item.widget().hide()
                item.widget().deleteLater()
        self.extra_scroll = None
        panel = self.panel
        c = panel.store.data
        tr = panel.tr
        body = QWidget()
        grid = QGridLayout(body)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setSpacing(0)
        labels = [
            ("memory", "内存", "Memory"),
            ("network", "网络", "Network"),
            ("gpu", "GPU", "GPU"),
            ("power", "功耗", "Power"),
            ("processes", "进程", "Apps"),
        ]
        for i, (key, zh, en) in enumerate(labels):
            dim = key == "power" or key == "gpu" and panel.data["gpu"] is None
            b = DiscButton(
                panel,
                key,
                tr(zh, en),
                lambda _=False, k=key: self.toggle_module(k),
                key in shown(c),
                dim=dim,
            )
            b.setToolTip(panel.detail(key) if dim else tr(zh, en))
            grid.addWidget(b, 0, i)
        actions = [
            (
                "pin",
                tr("置顶", "On top"),
                lambda: self.set("floating", not c["floating"]),
                c["floating"],
            ),
            (
                "tray",
                tr("托盘", "Tray"),
                lambda: self.set("menuBar", not c["menuBar"]),
                c["menuBar"],
            ),
            (
                "motion",
                tr("平滑", "Smooth"),
                lambda: self.set("motion", not c["motion"]),
                c["motion"],
            ),
            ("hide", tr("隐藏", "Hide"), self.hide_panel, False),
            ("quit", tr("退出", "Quit"), panel.quit, False),
        ]
        for i, (g, label, fn, on) in enumerate(actions):
            grid.addWidget(DiscButton(panel, g, label, fn, on), 1, i)
        for i, (pal, zh, en) in enumerate(
            [
                ("sea", "海", "Sea"),
                ("graphite", "石墨", "Graphite"),
                ("warm", "暖", "Warm"),
            ]
        ):
            grid.addWidget(
                DiscButton(
                    panel,
                    "",
                    tr(zh, en),
                    lambda _=False, k=pal: self.set("palette", k),
                    c["palette"] == pal,
                    palette=pal,
                ),
                2,
                i,
            )
        grid.addWidget(
            DiscButton(panel, "more", tr("更多", "More"), self.expand, self.more), 2, 4
        )
        self.outer.addWidget(body)
        if self.more:
            extra = QWidget()
            rows = QVBoxLayout(extra)
            rows.setContentsMargins(2, 0, 2, 0)

            def combo(label, key, choices):
                row = QWidget()
                line = QHBoxLayout(row)
                line.setContentsMargins(0, 2, 0, 2)
                text = QLabel(label)
                text.setFont(font(12))
                line.addWidget(text)
                line.addStretch()
                box = QComboBox()
                box.setFont(font(11))
                for title, value in choices:
                    box.addItem(title, value)
                box.setCurrentIndex(max(0, box.findData(c[key])))
                box.currentIndexChanged.connect(
                    lambda i: self.set(key, box.itemData(i))
                )
                line.addWidget(box)
                rows.addWidget(row)

            combo(
                tr("刷新间隔", "Update every"),
                "interval",
                [(f"{v:g} s", v) for v in (0.5, 1.0, 2.0, 5.0, 10.0)],
            )
            combo(
                tr("最多进程行", "Rows per column"),
                "maxProcesses",
                [(str(v), v) for v in (3, 5, 7, 9, 12, 16, 24)],
            )
            combo(
                tr("进程排序", "Sort by"),
                "processSort",
                [("CPU", "cpu"), (tr("内存", "Memory"), "memory")],
            )
            combo(
                tr("语言", "Language"),
                "language",
                [(tr("跟随系统", "System"), "auto"), ("中文", "zh"), ("English", "en")],
            )
            combo(
                tr("外观", "Appearance"),
                "appearance",
                [
                    (tr("跟随系统", "System"), "auto"),
                    (tr("深色", "Dark"), "dark"),
                    (tr("浅色", "Light"), "light"),
                ],
            )
            row = QPushButton(
                tr("开机启动：", "Start at login: ")
                + (
                    tr("开启", "On")
                    if native.autostart_enabled()
                    else tr("关闭", "Off")
                )
            )
            row.clicked.connect(
                lambda: (
                    native.set_autostart(not native.autostart_enabled()),
                    self.rebuild(),
                )
            )
            rows.addWidget(row)
            button = QPushButton(tr("打开设置文件 ↗", "Open settings file ↗"))
            button.clicked.connect(lambda: os.startfile(panel.store.path))
            rows.addWidget(button)
            note = QLabel(
                tr("Windows 移植版 · 手动更新", "Windows port · manual updates")
            )
            note.setFont(font(10))
            rows.addWidget(note)
            extra.setMinimumHeight(extra.sizeHint().height())
            self.extra_scroll = QScrollArea()
            self.extra_scroll.setFrameShape(QFrame.Shape.NoFrame)
            self.extra_scroll.setWidgetResizable(True)
            self.extra_scroll.setHorizontalScrollBarPolicy(
                Qt.ScrollBarPolicy.ScrollBarAlwaysOff
            )
            self.extra_scroll.setWidget(extra)
            extra.setAutoFillBackground(False)
            self.extra_scroll.viewport().setAutoFillBackground(False)
            self.extra_scroll.setFixedHeight(extra.sizeHint().height())
            self.outer.addWidget(self.extra_scroll)
        t = panel.theme
        self.setStyleSheet(
            f"QLabel {{color:{t.ink};}} QComboBox, QPushButton {{color:{t.ink};background:{t.card};border:0;padding:5px;border-radius:5px;}} QComboBox QAbstractItemView {{color:{t.ink};background:{t.card};selection-background-color:{t.cpu};}}"
        )
        # Children added to a visible popup otherwise stay layout-hidden until
        # the next event turn, so adjustSize would still use the collapsed size.
        for i in range(self.outer.count()):
            self.outer.itemAt(i).widget().show()
        self.fit_to_screen()

    def fit_to_screen(self, anchor=None):
        anchor = self.pos() if anchor is None else anchor
        screen = QApplication.screenAt(anchor) or QApplication.primaryScreen()
        area = screen.availableGeometry().adjusted(6, 6, -6, -6)
        self.ensurePolished()
        if self.extra_scroll:
            natural = self.extra_scroll.widget().sizeHint().height()
            self.extra_scroll.setFixedHeight(natural)
            self.outer.invalidate()
            self.outer.activate()
            base = self.sizeHint().height() - natural
            self.extra_scroll.setFixedHeight(max(1, min(natural, area.height() - base)))
        self.outer.invalidate()
        self.outer.activate()
        self.adjustSize()
        self.move(
            max(area.left(), min(anchor.x(), area.right() - self.width() + 1)),
            max(area.top(), min(anchor.y(), area.bottom() - self.height() + 1)),
        )
        round_window(self, 18)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        round_window(self, 18)

    def set(self, key, value):
        self.panel.change_config({key: value})
        self.rebuild()

    def toggle_module(self, key):
        c = self.panel.store.data
        target = "hidden" if key in ("gpu", "power") else "modules"
        values = c[target][:]
        if key in values:
            values.remove(key)
        else:
            values.append(key)
        self.set(target, values)

    def expand(self):
        self.more = not self.more
        self.rebuild()

    def hide_panel(self):
        self.hide()
        self.panel.hide_panel()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(QPen(alpha(self.panel.theme.ink, 0.16), 1))
        p.setBrush(QColor(self.panel.theme.bg))
        p.drawRoundedRect(QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5), 18, 18)


class Panel(QWidget):
    def __init__(self, store, sampler, testing=False):
        super().__init__(None, Qt.WindowType.Tool | Qt.WindowType.FramelessWindowHint)
        self.store, self.sampler, self.testing = store, sampler, testing
        self.data = sampler.snap
        self.theme = Theme(store.data)
        self.setWindowTitle("ftop")
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setMouseTracking(True)
        self.setMinimumSize(40, 16)
        self.follow = Follow()
        self.layout = None
        self.cache = None
        self.arcs = []
        self.bars = []
        self.hits = []
        self.hover = None
        self.pressed = None
        self.drag = None
        self.resize_edges = None
        self.dragged = False
        self.top_controls = False
        self._last_sequence = -1
        self._last_env = 0
        self._last_visibility = 0
        self._occluded = False
        self._rebuilding = False
        self._backdrop_suspended = False
        self._quitting = False
        self.icon_provider = QFileIconProvider()
        self.icons = {}
        self._tray_key = None
        self._taskbar_light = system_light(taskbar=True)
        self._capsules = {}
        self._row_previous = {}
        self._row_moves = []
        self._row_pixmaps = {}
        self._row_rects = {}
        self._row_old_pixmaps = {}
        self._row_old_rects = {}
        self._transition_start = 0
        self._layout_signature = None
        self.tip = DetailPopup(self)
        self.card = DetailPopup(self, True)
        self.controls = Controls(self)
        self.tray = QSystemTrayIcon(self)
        self.tray.setToolTip("ftop")
        self.tray.activated.connect(self.tray_action)
        self.tray.setIcon(self.tray_icon(0))
        self.tray.setVisible(store.data["menuBar"] and not testing)
        self.setWindowFlag(Qt.WindowType.WindowStaysOnTopHint, store.data["floating"])
        self.resize(round(store.data["width"]), round(store.data["height"]))
        area = QApplication.primaryScreen().availableGeometry()
        self.move(
            store.data.get("x", area.right() - self.width() - 32),
            store.data.get("y", area.top() + 50),
        )
        if store.data.get("positionUnits") == "physical":
            monitor = native.monitor_geometry(self.x(), self.y())
            if monitor:
                name, left, top = monitor
                screen = next(
                    (s for s in QApplication.screens() if s.name() == name),
                    QApplication.primaryScreen(),
                )
                factor = screen.devicePixelRatio()
                origin = screen.geometry().topLeft()
                self.move(
                    origin.x() + round((self.x() - left) / factor),
                    origin.y() + round((self.y() - top) / factor),
                )
        self.ensure_onscreen()
        self.relayout(snap=True)
        if store.data.get("positionUnits") == "physical":
            self.save_geometry()
        self.frame = QTimer(self)
        self.frame.setInterval(16)
        self.frame.timeout.connect(self.animation_frame)
        self.heartbeat = QTimer(self)
        self.heartbeat.setInterval(150)
        self.heartbeat.timeout.connect(self.tick)
        if not testing:
            self.heartbeat.start()
        self.accept_snapshot()

    def tr(self, zh, en):
        lang = self.store.data["language"]
        chinese = (
            lang == "zh"
            or lang == "auto"
            and (locale.getlocale()[0] or "").lower().startswith(("zh", "chinese"))
        )
        return zh if chinese else en

    def ensure_onscreen(self):
        screens = QApplication.screens()

        def overlap(screen):
            rect = screen.availableGeometry().intersected(self.geometry())
            return max(0, rect.width()) * max(0, rect.height())

        screen = max(screens, key=overlap)
        area = screen.availableGeometry()
        self.resize(min(self.width(), area.width()), min(self.height(), area.height()))
        self.move(
            max(area.left(), min(self.x(), area.right() - self.width() + 1)),
            max(area.top(), min(self.y(), area.bottom() - self.height() + 1)),
        )

    def showEvent(self, e):
        super().showEvent(e)
        round_window(self, 16)
        if not self.testing and not self._backdrop_suspended:
            native.apply_backdrop(int(self.winId()), self.theme.dark)

    def closeEvent(self, e):
        if not self._quitting:
            e.ignore()
            self.hide_panel()
        else:
            e.accept()

    def change_config(self, changes):
        if not self.store.save(changes):
            self.tip.present(
                [
                    self.tr(
                        "设置文件格式有误，请先修复。",
                        "Invalid settings file; repair it first.",
                    ),
                    self.store.error or "",
                ],
                self.mapToGlobal(QPoint(10, 10)),
            )
            return
        self.apply_config()

    def apply_config(self):
        c = self.store.data
        self.theme = Theme(c)
        self.sampler.configure(c)
        visible = self.isVisible()
        self.setWindowFlag(Qt.WindowType.WindowStaysOnTopHint, c["floating"])
        if visible:
            self.show()
        self.tray.setVisible(c["menuBar"] and not self.testing)
        if not c["motion"]:
            self.follow.moves.clear()
        self._layout_signature = None
        self.relayout(snap=True)
        self.build_cache()
        self.update()
        if not self.testing and not self._backdrop_suspended:
            native.apply_backdrop(int(self.winId()), self.theme.dark)

    def save_geometry(self):
        if not self.testing:
            self.store.save(
                dict(
                    x=self.x(),
                    y=self.y(),
                    width=self.width(),
                    height=self.height(),
                    positionUnits="logical",
                )
            )

    def relayout(self, snap=False):
        self.layout = LayoutEngine(self.sampler.n, self.store.data).choose(
            self.width(), self.height()
        )
        if snap:
            w, h = (
                round(self.layout.width * self.layout.scale),
                round(self.layout.height * self.layout.scale),
            )
            if (w, h) != (self.width(), self.height()):
                self._rebuilding = True
                self.resize(w, h)
                self._rebuilding = False
        self.build_cache()

    def resizeEvent(self, e):
        super().resizeEvent(e)
        round_window(self, 16)
        if hasattr(self, "cache") and not self._rebuilding:
            self.relayout()
            self.tip.hide()
            self.card.hide()

    def event(self, event):
        result = super().event(event)
        if event.type() in (QEvent.Type.UngrabMouse, QEvent.Type.Hide) and getattr(
            self, "drag", None
        ):
            self.drag = None
            self.resize_edges = None
            self.dragged = False
            self.pressed = None
            self.restore_backdrop()
            self.sampler.set_mode("full" if self.isVisible() else "idle")
            self.update()
        if event.type() == QEvent.Type.DevicePixelRatioChange and getattr(
            self, "layout", None
        ):
            round_window(self, 16)
            self._capsules.clear()
            self.build_cache()
        return result

    def text(
        self,
        p,
        rect,
        text,
        size=12,
        strong=False,
        color=None,
        align=Qt.AlignmentFlag.AlignLeft,
    ):
        p.setFont(font(size, strong))
        p.setPen(QColor(color or self.theme.ink))
        rendered = QFontMetricsF(p.font()).elidedText(
            str(text), Qt.TextElideMode.ElideRight, max(0, rect.width())
        )
        p.drawText(rect, align | Qt.AlignmentFlag.AlignVCenter, rendered)

    def badge(
        self, p, kind, x, y, value=None, color=None, key=None, icon=None, on=False
    ):
        t = self.theme
        color = color or t.secondary
        rect = QRectF(x, y, 20, 20)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(color) if on else alpha(t.ink, 0.11 if t.dark else 0.08))
        p.drawEllipse(rect)
        if icon and not icon.isNull():
            # Shell icons have a different source size from the destination on
            # fractional-DPI screens. Filter this resample, not the row's text.
            p.save()
            p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
            p.drawPixmap(QRectF(x + 3, y + 3, 14, 14), icon, QRectF(icon.rect()))
            p.restore()
        else:
            glyph(
                p, kind, QRectF(x + 4.75, y + 4.75, 10.5, 10.5), t.bg if on else color
            )
        if value is not None:
            if key:
                self.arcs.append((rect, key, color))
            else:
                self.draw_arc(p, rect, value, color)

    @staticmethod
    def draw_arc(p, rect, value, color):
        if value is None:
            return
        p.setPen(
            QPen(QColor(color), 2.2, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap)
        )
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawArc(
            rect.adjusted(1.1, 1.1, -1.1, -1.1),
            90 * 16,
            -round(min(1, max(0, value)) * 360 * 16),
        )

    def figure(self, p, kind, x, y, width=100, big=False, short=False):
        d, t = self.data, self.theme
        value, unit, color, fraction = "—", "", t.secondary, None
        key = None
        if kind == "cpu":
            value, unit, color = number(d["cpu"]), "%", t.cpu
            fraction = None if d["cpu"] is None else d["cpu"] / 100
            key = "cpu"
        elif kind == "gpu":
            value, unit, color = number(d["gpu"]), "%", t.gpu
            fraction = None if d["gpu"] is None else d["gpu"] / 100
            key = "gpu"
        elif kind == "memory":
            fraction = d["memory"] / d["total"] if d["total"] else None
            value, unit, color = (
                number(None if fraction is None else fraction * 100),
                "%",
                t.pressure(d["pressure"]),
            )
            key = "memory"
        elif kind in ("down", "up"):
            value, unit = rate(d[kind])
            unit = unit[:1] if short else unit
        elif kind == "vram":
            value, unit, color = number(d["gpu_memory"], 1), "GB", t.gpu
        elif kind == "temperature":
            value, unit = (
                number(d["temperature"]),
                "°" if d["temperature"] is not None else "",
            )
        elif kind == "power":
            value, unit = number(d["power"], 1), "W"
        self.badge(p, "memory" if kind == "vram" else kind, x, y, fraction, color, key)
        size = 17 if big else 12
        p.setFont(font(size, True))
        tw = QFontMetricsF(p.font()).horizontalAdvance(value)
        self.text(p, QRectF(x + 26, y, width - 26, 20), value, size, True)
        if unit:
            self.text(
                p,
                QRectF(x + 29 + tw, y + 1, max(0, width - 29 - tw), 19),
                unit,
                10.5,
                color=t.secondary,
            )
        self.hits.append((QRectF(x, y, width, 20), "detail", kind))

    def core_block(self, p, b, mini=False, corner=False):
        c = self.layout.candidate
        t = self.theme
        numbers = c.tier == "detailed" and not mini
        if not mini:
            self.figure(p, "cpu", b.x, b.y, 86, True)
            if corner or c.tier == "compact":
                if "gpu" in shown(self.store.data):
                    self.figure(p, "gpu", b.x + b.w / 3, b.y, b.w / 3, True)
                self.figure(p, "temperature", b.x + 2 * b.w / 3, b.y, b.w / 3, True)
            elif "power" not in shown(self.store.data):
                self.figure(p, "temperature", b.x + b.w - 78, b.y, 78, True)
        top = b.y if mini else b.y + 28
        bh = b.h if mini else b.h - 28 - (30 if numbers else 0)
        order = self.sampler.order
        gap = 1.5 if mini else 4
        groups = sum(a[1] != z[1] for a, z in zip(order, order[1:]))
        extra = 2 if mini else 10
        # Wide machines reduce gaps before column width; never overflow.
        gap = min(gap, b.w / max(1, len(order) * 3))
        extra = min(extra, b.w / max(1, len(order)))
        bw = max(0.25, (b.w - gap * (len(order) - 1) - groups * extra) / len(order))
        x = b.x
        for pos, (idx, gi, ck, th) in enumerate(order):
            if pos:
                x += gap + (extra if gi != order[pos - 1][1] else 0)
            color = t.eff if gi > 0 else t.cpu
            box = QRectF(x, top, bw, bh)
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(alpha(t.ink, 0.08))
            radius = min(8, bw / 2)
            p.drawRoundedRect(box, radius, radius)
            self.bars.append((box, idx, color, mini, pos))
            self.hits.append(
                (QRectF(x, top, bw, bh + (30 if numbers else 0)), "core", idx)
            )
            if numbers:
                self.text(
                    p,
                    QRectF(x - 2, top + bh + 3, bw + 4, 13),
                    number(self.data["cores"][idx]),
                    10,
                    True,
                    align=Qt.AlignmentFlag.AlignHCenter,
                )
                self.text(
                    p,
                    QRectF(x - 2, top + bh + 17, bw + 4, 12),
                    number(self.data["freqs"][idx], 2),
                    8.5,
                    color=t.secondary,
                    align=Qt.AlignmentFlag.AlignHCenter,
                )
            x += bw

    def memory_block(self, p, b, meter_only=False):
        d, t = self.data, self.theme
        color = t.pressure(d["pressure"])
        frac = d["memory"] / d["total"] if d["total"] else None
        y = b.y if meter_only else b.y + 28
        side = 90
        if not meter_only:
            self.badge(p, "memory", b.x, b.y, frac, color, "memory")
            value = number(None if d["memory"] is None else d["memory"] / 1024**3, 1)
            self.text(p, QRectF(b.x + 28, b.y, b.w - 28, 20), value, 17, True)
            advance = QFontMetricsF(font(17, True)).horizontalAdvance(value)
            self.text(
                p,
                QRectF(b.x + 33 + advance, b.y + 1, b.w - 33 - advance - 44, 20),
                f"/ {number(None if d['total'] is None else d['total'] / 1024**3, 1).removesuffix('.0')} GB",
                10.5,
                color=t.secondary,
            )
            pressure = {
                "normal": self.tr("正常", "Normal"),
                "warning": self.tr("偏高", "Elevated"),
                "critical": self.tr("紧张", "Critical"),
            }.get(d["pressure"], "—")
            if self.layout.candidate.tier != "compact":
                self.text(
                    p,
                    QRectF(b.x + b.w - 66, b.y, 66, 20),
                    pressure,
                    10.5,
                    color=t.secondary,
                    align=Qt.AlignmentFlag.AlignRight,
                )
        width = b.w if meter_only else b.w - side - 7
        rect = QRectF(b.x, y, width, 5 if meter_only else 6)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(alpha(t.ink, 0.13))
        p.drawRoundedRect(rect, 3, 3)
        if frac is not None:
            p.setBrush(QColor(color))
            p.drawRoundedRect(
                QRectF(b.x, y, max(0, width * min(1, frac)), rect.height()), 3, 3
            )
            if d["compressed"] is not None and d["total"]:
                compressed = min(frac, d["compressed"] / d["total"])
                p.setBrush(alpha(t.bg, 0.55))
                p.drawRect(
                    QRectF(
                        b.x + width * (frac - compressed),
                        y,
                        width * compressed,
                        rect.height(),
                    )
                )
        if not meter_only:
            swap = QRectF(b.x + b.w - side, y, side, 6)
            p.setPen(QPen(alpha(t.ink, 0.45), 1))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRoundedRect(swap, 3, 3)
            if d["swap"] is not None and d["swap_total"]:
                p.setPen(Qt.PenStyle.NoPen)
                p.setBrush(alpha(t.ink, 0.65))
                p.drawRoundedRect(
                    QRectF(swap.x(), y, side * min(1, d["swap"] / d["swap_total"]), 6),
                    3,
                    3,
                )
            if self.layout.candidate.tier != "compact":
                comp = number(
                    None if d["compressed"] is None else d["compressed"] / 1024**3, 1
                )
                sv = number(None if d["swap"] is None else d["swap"] / 1024**3, 1)
                self.text(
                    p,
                    QRectF(b.x, y + 12, width, 16),
                    self.tr("压缩 ", "Compressed ")
                    + comp
                    + (" GB" if comp != "—" else ""),
                    10.5,
                    color=t.secondary,
                )
                self.text(
                    p,
                    QRectF(swap.x(), y + 12, side, 16),
                    self.tr("交换 ", "Swap ") + sv + " GB",
                    10.5,
                    color=t.secondary,
                )
        self.hits.append((QRectF(b.x, b.y, b.w, b.h), "detail", "memory"))

    def get_icon(self, exe):
        if not exe:
            return None
        if exe not in self.icons:
            if len(self.icons) > 300:
                self.icons.clear()
            self.icons[exe] = (
                self.icon_provider.icon(QFileInfo(exe)).pixmap(32, 32)
                if os.path.isfile(exe)
                else QPixmap()
            )
        return self.icons[exe]

    def process_block(self, p, b):
        c = self.layout.candidate
        sort = self.store.data["processSort"]
        header = 28 if c.wide else 0
        if c.wide:
            for i, k in enumerate(("cpu", "memory")):
                self.badge(
                    p,
                    k,
                    b.x + i * 26,
                    b.y,
                    color=self.theme.cpu if k == "cpu" else self.theme.green,
                    on=sort == k,
                )
                self.hits.append((QRectF(b.x + i * 26, b.y, 23, 22), "sort", k))
        order = self.data[sort + "_order"][: c.count * (2 if c.wide else 1)]
        per = max(1, min(c.count, (len(order) + 1) // 2)) if c.wide else c.count
        width = (b.w - 22) / 2 if c.wide else b.w
        total = (
            max(1, sum((g["cpu"] or 0) for g in self.data["groups"].values()))
            if sort == "cpu"
            else max(
                1,
                self.data["memory"]
                or sum((g["mem"] or 0) for g in self.data["groups"].values()),
            )
        )
        for i, key in enumerate(order):
            g = self.data["groups"][key]
            col, row = divmod(i, per)
            x, y = b.x + col * (width + 22), b.y + header + row * 28
            rect = QRectF(x, y, width, 20)
            # Include both screen DPI and layout zoom. Fixed 2x artwork is
            # resampled again at 125% DPI, destroying small glyph strokes.
            density = self.devicePixelRatioF() * self.layout.scale
            pix = QPixmap(math.ceil(width * density), math.ceil(20 * density))
            pix.setDevicePixelRatio(density)
            pix.fill(Qt.GlobalColor.transparent)
            rp = QPainter(pix)
            rp.setRenderHint(QPainter.RenderHint.Antialiasing)
            value, unit = (number(g["cpu"]), "%") if sort == "cpu" else memory(g["mem"])
            self.badge(
                rp,
                "process",
                0,
                0,
                (g["cpu"] or 0) / total if sort == "cpu" else (g["mem"] or 0) / total,
                self.theme.cpu if sort == "cpu" else self.theme.green,
                icon=self.get_icon(g["exe"]),
            )
            # One figure, no count suffix, matching upstream. All members remain in the card.
            self.text(rp, QRectF(28, 0, width - 28 - 66, 20), g["name"], 12)
            self.text(
                rp,
                QRectF(width - 65, 0, 65, 20),
                value + unit,
                12,
                True,
                align=Qt.AlignmentFlag.AlignRight,
            )
            rp.end()
            self._row_pixmaps[key] = pix
            self._row_rects[key] = rect
            self.hits.append((rect.adjusted(0, -4, 0, 4), "process", key))
        if not order:
            self.text(
                p,
                QRectF(b.x, b.y + header, b.w, 20),
                "—",
                12,
                color=self.theme.secondary,
            )

    def build_cache(self):
        if self.layout is None:
            return
        self.hits = []
        self.bars = []
        self.arcs = []
        self._row_pixmaps = {}
        self._row_rects = {}
        dpr = self.devicePixelRatioF()
        size = self.size() * dpr
        self.cache = QPixmap(size)
        self.cache.setDevicePixelRatio(dpr)
        self.cache.fill(Qt.GlobalColor.transparent)
        p = QPainter(self.cache)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(QPen(alpha(self.theme.ink, 0.16), 1))
        p.setBrush(alpha(self.theme.bg, self.store.data["opacity"]))
        radius = min(16, self.height() / 2)
        p.drawRoundedRect(
            QRectF(self.rect()).adjusted(0.5, 0.5, -0.5, -0.5), radius, radius
        )
        p.scale(self.layout.scale, self.layout.scale)
        mods = shown(self.store.data)
        for b in self.layout.blocks:
            if b.kind in ("cpu", "mini", "cornercpu"):
                self.core_block(p, b, b.kind == "mini", b.kind == "cornercpu")
            elif b.kind in ("memory", "meter"):
                self.memory_block(p, b, b.kind == "meter")
            elif b.kind == "network":
                self.figure(
                    p,
                    "down",
                    b.x,
                    b.y,
                    b.w - 100,
                    short=self.layout.candidate.tier == "compact",
                )
                self.figure(
                    p,
                    "up",
                    b.x + b.w - 100,
                    b.y,
                    100,
                    short=self.layout.candidate.tier == "compact",
                )
            elif b.kind == "pairs":
                y = b.y
                if "gpu" in mods:
                    self.figure(p, "gpu", b.x, y, b.w - 100)
                    self.figure(p, "vram", b.x + b.w - 100, y, 100)
                    y += 28
                if "power" in mods:
                    self.figure(p, "power", b.x, y, b.w - 100)
                    self.figure(p, "temperature", b.x + b.w - 100, y, 100)
            elif b.kind == "processes":
                self.process_block(p, b)
            elif b.kind == "cornerbottom":
                kinds = (["down", "up"] if "network" in mods else []) + (
                    ["power"] if "power" in mods else []
                )
                for i, k in enumerate(kinds):
                    self.figure(
                        p,
                        k,
                        b.x + i * b.w / max(1, len(kinds)),
                        b.y,
                        b.w / max(1, len(kinds)),
                        short=True,
                    )
            elif b.kind == "micro":
                kinds = ["cpu"] + (["gpu"] if "gpu" in mods else []) + ["memory"]
                for i, k in enumerate(kinds):
                    value = (
                        self.data["memory"] / self.data["total"]
                        if k == "memory" and self.data["total"]
                        else self.data.get(k)
                    )
                    if k != "memory" and value is not None:
                        value /= 100
                    col = (
                        self.theme.cpu
                        if k == "cpu"
                        else self.theme.gpu
                        if k == "gpu"
                        else self.theme.pressure(self.data["pressure"])
                    )
                    self.badge(p, k, b.x + i * 26, b.y, value, col, k)
                    self.hits.append((QRectF(b.x + i * 26, b.y, 20, 20), "detail", k))
            elif b.kind.startswith("fig:"):
                k = b.kind[4:]
                if k == "top":
                    order = self.data["cpu_order"]
                    if order:
                        g = self.data["groups"][order[0]]
                        self.text(
                            p,
                            QRectF(b.x, b.y, b.w, 20),
                            g["name"] + " " + number(g["cpu"]) + "%",
                        )
                        self.hits.append(
                            (QRectF(b.x, b.y, b.w, 20), "process", order[0])
                        )
                else:
                    self.figure(p, k, b.x, b.y, b.w, short=True)
        p.end()
        self.update()

    def capsule(self, p, rect, color):
        """Reuse antialiased end caps. Rasterizing 24 rounded paths every frame was
        the main animation cost on the i7-10750H; a rectangle and two blits suffice.
        """
        radius = min(8, rect.width() / 2, rect.height() / 2)
        factor = self.layout.scale * self.devicePixelRatioF()
        pw = max(1, math.ceil(rect.width() * factor))
        ph = max(2, math.ceil(radius * 2 * factor))
        key = (pw, ph, color)
        cap = self._capsules.get(key)
        if cap is None:
            if len(self._capsules) > 256:
                self._capsules.clear()
            cap = QPixmap(pw, ph)
            cap.fill(Qt.GlobalColor.transparent)
            cp = QPainter(cap)
            cp.setRenderHint(QPainter.RenderHint.Antialiasing)
            cp.setPen(Qt.PenStyle.NoPen)
            cp.setBrush(QColor(color))
            cp.drawRoundedRect(QRectF(0, 0, pw, ph), min(pw / 2, ph / 2), ph / 2)
            cp.end()
            self._capsules[key] = cap
        p.drawPixmap(
            QRectF(rect.x(), rect.y(), rect.width(), radius),
            cap,
            QRectF(0, 0, pw, ph / 2),
        )
        p.fillRect(
            QRectF(
                rect.x(),
                rect.y() + radius,
                rect.width(),
                max(0, rect.height() - 2 * radius),
            ),
            QColor(color),
        )
        p.drawPixmap(
            QRectF(rect.x(), rect.bottom() - radius, rect.width(), radius),
            cap,
            QRectF(0, ph / 2, pw, ph / 2),
        )

    def paintEvent(self, _):
        if not self.layout:
            return
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        if self.cache:
            p.drawPixmap(0, 0, self.cache)
        p.scale(self.layout.scale, self.layout.scale)
        now = time.monotonic()
        for box, idx, color, mini, pos in self.bars:
            dim = self.hover and self.hover[0] == "core" and self.hover[1] != idx
            p.setOpacity(0.35 if dim else 1)
            usage = self.follow.value(("core", idx), now)
            if usage is not None:
                height = max(
                    min(4, box.height()), box.height() * min(100, max(0, usage)) / 100
                )
                fill = QRectF(box.x(), box.bottom() - height, box.width(), height)
                self.capsule(p, fill, color)
            freq = self.follow.value(("freq", idx), now)
            if not mini and freq is not None:
                top = max(
                    5.0,
                    max((x for x in self.data["freqs"] if x is not None), default=5.0),
                )
                y = box.bottom() - box.height() * min(1, max(0, freq / top))
                p.setPen(
                    QPen(
                        QColor(self.theme.ink),
                        1.5,
                        Qt.PenStyle.SolidLine,
                        Qt.PenCapStyle.RoundCap,
                    )
                )
                p.drawLine(
                    QPointF(box.x() + box.width() * 0.2, y),
                    QPointF(box.right() - box.width() * 0.2, y),
                )
            p.setOpacity(1)
        for rect, key, color in self.arcs:
            self.draw_arc(p, rect, self.follow.value(key, now), color)
        progress = min(1, max(0, (now - self._transition_start) / 0.36))
        for key, pix in self._row_pixmaps.items():
            rect = self._row_rects[key]
            old = self._row_old_rects.get(key)
            target = QRectF(rect)
            opacity = 1.0
            if self.motion and old is not None and progress < 1 and old != rect:
                if abs(old.y() - rect.y()) <= 28.1 and old.x() == rect.x():
                    eased = 1 - (1 - progress) ** 3
                    target.moveTop(old.y() + (rect.y() - old.y()) * eased)
                else:
                    opacity = min(1, progress * 1.8)
                    target.translate(0, 6 * (1 - progress))
            elif self.motion and old is None and progress < 1:
                opacity = progress
            p.setOpacity(opacity)
            if self.hover == ("process", key):
                p.setPen(Qt.PenStyle.NoPen)
                p.setBrush(alpha(self.theme.ink, 0.08))
                p.drawRoundedRect(target.adjusted(-3, -2, 3, 2), 6, 6)
            if self.pressed == ("process", key):
                target.translate(0, 1)
            # Match cached pixels to physical pixels, including during motion.
            density = self.devicePixelRatioF() * self.layout.scale
            origin = QPointF(
                round(target.x() * density) / density,
                round(target.y() * density) / density,
            )
            p.drawPixmap(origin, pix)
            p.setOpacity(1)
        if self.top_controls and self.width() >= 100 and self.height() >= 30:
            for i, k in enumerate(("pin", "hide")):
                box = QRectF(self.width() / self.layout.scale - 50 + i * 24, 3, 20, 20)
                p.setPen(Qt.PenStyle.NoPen)
                p.setBrush(QColor(self.theme.card))
                p.drawEllipse(box)
                glyph(
                    p,
                    k,
                    box.adjusted(5, 5, -5, -5),
                    self.theme.cpu
                    if k == "pin" and self.store.data["floating"]
                    else self.theme.ink,
                )
        p.end()

    @property
    def motion(self):
        return self.store.data["motion"] and getattr(self, "system_motion", True)

    def accept_snapshot(self):
        self.data = self.sampler.snap
        now = time.monotonic()
        c = self.store.data
        self._last_sequence = self.data["sequence"]
        for kind in ("cpu", "gpu"):
            v = self.data[kind]
            self.follow.set(
                kind,
                None if v is None else v / 100,
                now,
                c["interval"],
                animate=self.motion,
            )
        frac = self.data["memory"] / self.data["total"] if self.data["total"] else None
        self.follow.set("memory", frac, now, c["interval"], animate=self.motion)
        for pos, (idx, _, _, _) in enumerate(self.sampler.order):
            for name, field in (("core", "cores"), ("freq", "freqs")):
                self.follow.set(
                    (name, idx),
                    self.data[field][idx],
                    now,
                    c["interval"],
                    pos * 0.018,
                    self.motion,
                )
        self._row_old_rects = self._row_rects.copy()
        self._row_old_pixmaps = self._row_pixmaps.copy()
        self._transition_start = now
        self.build_cache()
        if self.card.isVisible() and self.card.key:
            key = self.card.key
            if key in self.data["groups"]:
                self.card.present(
                    self.card_lines(key), self.card.pos() - QPoint(12, 12), key
                )
            else:
                self.card.hide()
        if self.motion and self.isVisible() and not self._occluded:
            self.frame.start()

    def animation_frame(self):
        if not self.isVisible() or self._occluded or self.drag:
            self.frame.stop()
            return
        self.update()
        if not self.follow.active() and time.monotonic() - self._transition_start > 0.4:
            self.frame.stop()

    def tick(self):
        if self.dragged:
            # Leave mouse handling the UI thread during a gesture. Config,
            # theme checks, icons and fresh samples can wait until release.
            return
        now = time.monotonic()
        if now - self._last_env > 2:
            self._last_env = now
            self._taskbar_light = system_light(taskbar=True)
            self.system_motion = (
                native.animations_enabled() and not native.battery_saver()
            )
            changed = self.store.reload()
            if changed or Theme(self.store.data).dark != self.theme.dark:
                self.apply_config()
        if not self.isVisible():
            self.sampler.set_mode("idle")
            self.frame.stop()
        else:
            if now - self._last_visibility > 0.75:
                self._last_visibility = now
                self._occluded = native.fully_occluded(int(self.winId()))
            c = self.layout.candidate
            mode = (
                "idle"
                if self._occluded or self.drag
                else "full"
                if c.count or c.tier in ("detailed", "full", "compact", "corner")
                else "light"
            )
            self.sampler.set_mode(mode)
            if (
                not self._occluded
                and not self.drag
                and self.sampler.snap["sequence"] != self._last_sequence
            ):
                self.accept_snapshot()
        value = self.sampler.snap["cpu"]
        v = None if value is None else round(value)
        tray_key = (v, self._taskbar_light)
        if tray_key != self._tray_key:
            self._tray_key = tray_key
            self.tray.setIcon(self.tray_icon(v))
            self.tray.setToolTip(f"ftop · CPU {number(v)}%")
        # A hidden monitor has no 60 fps animation and wakes only twice a second.
        self.heartbeat.setInterval(
            500 if not self.isVisible() or self._occluded else 150
        )

    def tray_icon(self, value):
        icon = QIcon()
        # Windows can mix light apps with a dark taskbar. Its shell preference
        # owns this icon, independently of the panel's selected appearance.
        color = QColor("#121826" if self._taskbar_light else "#FFFFFF")
        # Native sizes prevent Explorer resampling tiny numerals at 125/150%.
        for size in (16, 20, 24, 32, 40, 48, 64):
            pix = QPixmap(size, size)
            pix.fill(Qt.GlobalColor.transparent)
            p = QPainter(pix)
            p.setRenderHint(QPainter.RenderHint.Antialiasing)
            p.setPen(color)
            text = number(value)
            face = font(size * 1.2, True)
            # Fit visible ink, not the font's line box. Single digits can fill
            # the tray height; two/three digits shrink only enough to fit.
            while True:
                bounds = QFontMetricsF(face).tightBoundingRect(text)
                if (
                    bounds.width() <= size - 4 and bounds.height() <= size - 4
                ) or face.pixelSize() <= 1:
                    break
                face.setPixelSize(face.pixelSize() - 1)
            p.setFont(face)
            p.drawText(
                QPointF(
                    round((size - bounds.width()) / 2 - bounds.left()),
                    round((size - bounds.height()) / 2 - bounds.top()),
                ),
                text,
            )
            p.end()
            icon.addPixmap(pix)
        return icon

    def tray_action(self, reason):
        if reason == QSystemTrayIcon.ActivationReason.Trigger:
            self.toggle()
        elif reason == QSystemTrayIcon.ActivationReason.Context:
            self.show_controls(self.cursor().pos())

    def show_controls(self, pos):
        self.tip.hide()
        self.controls.more = False
        self.controls.rebuild()
        self.controls.fit_to_screen(pos)
        self.controls.show()

    def show_panel(self):
        self.show()
        self.raise_()
        self._occluded = False
        self._last_visibility = 0
        self.sampler.set_mode("full")
        self.tick()

    def hide_panel(self):
        self.save_geometry()
        self.tip.hide()
        self.card.hide()
        self.controls.hide()
        self.hide()
        self.sampler.set_mode("idle")
        self.frame.stop()

    def toggle(self):
        self.hide_panel() if self.isVisible() else self.show_panel()

    def quit(self):
        self.save_geometry()
        self._quitting = True
        self.heartbeat.stop()
        self.frame.stop()
        self.sampler.stop()
        self.tray.hide()
        QApplication.instance().quit()

    def detail(self, kind):
        d = self.data
        tr = self.tr
        if kind == "cpu":
            return tr("处理器总占用 ", "Total CPU ") + number(d["cpu"]) + "%"
        if kind == "memory":
            gb = lambda x: number(None if x is None else x / 1024**3, 1)
            return (
                tr("内存 · Windows 工作集口径", "Memory · Windows working set")
                + f"\n{gb(d['memory'])} / {gb(d['total'])} GB"
                + tr("\n可用 ", "\nAvailable ")
                + gb(d["available"])
                + " GB"
                + tr(
                    "\n压力：Windows 内存资源通知",
                    "\nPressure: Windows memory resource notification",
                )
                + tr("\n压缩量：当前接口不可用", "\nCompressed bytes: unavailable")
            )
        if kind in ("gpu", "vram"):
            if d["gpu_name"] is None:
                return tr(
                    "GPU 不可用：未检测到 NVML。Intel / AMD 尚无采样后端。",
                    "GPU unavailable: NVML not found. Intel / AMD backend not implemented.",
                )
            return (
                f"{d['gpu_name']}\n{number(d['gpu'])}% · {number(d['gpu_memory'], 1)} / {number(d['gpu_total'])} GB\n{number(d['gpu_clock'], 2)} GHz · {number(d['gpu_power'], 1)} W · {number(d['gpu_temp'])}°C"
                + tr(
                    "\n功耗与温度均来自 GPU 传感器",
                    "\nPower and temperature are GPU sensor readings",
                )
            )
        if kind == "power":
            return tr(
                "整机功耗不可用\nWindows 通用接口不提供可靠的整机瓦数。",
                "Whole-machine power unavailable\nWindows has no universal whole-machine watt sensor.",
            )
        if kind == "temperature":
            return tr(
                "CPU 温度不可用\n不以 GPU 温度或 ACPI 热区值替代。",
                "CPU temperature unavailable\nGPU or ACPI thermal-zone readings are not substituted.",
            )
        if kind in ("down", "up"):
            v, u = rate(d[kind])
            return (
                tr(
                    "下载" if kind == "down" else "上传",
                    "Download" if kind == "down" else "Upload",
                )
                + f" {v} {u}"
            )
        return ""

    def card_lines(self, key):
        g = self.data["groups"].get(key)
        if not g:
            return [self.tr("进程已退出", "Process exited")]
        m, u = memory(g["mem"])
        lines = [
            g["name"],
            f"CPU {number(g['cpu'], 1)}%    " + self.tr("内存 ", "Memory ") + m + u,
            self.tr(
                "CPU 单核 100% · 内存为工作集",
                "CPU: 100% per core · Memory: working set",
            ),
        ]
        metric = "cpu" if self.store.data["processSort"] == "cpu" else "mem"
        members = sorted(g["members"], key=lambda r: r.get(metric) or 0, reverse=True)
        if len(members) > 1:
            lines.append(
                self.tr(
                    f"内部进程 · 共 {len(members)} 个",
                    f"Processes · {len(members)} total",
                )
            )
            for r in members[:4]:
                v, u = (number(r["cpu"]), "%") if metric == "cpu" else memory(r["mem"])
                lines.append(f"{r['name']} · PID {r['pid']}   {v}{u}")
        return lines

    def hit(self, pos):
        point = QPointF(pos) / self.layout.scale
        if self.top_controls and point.y() < 24 and self.width() >= 100:
            for i, k in enumerate(("pin", "hide")):
                if QRectF(
                    self.width() / self.layout.scale - 50 + i * 24, 3, 20, 20
                ).contains(point):
                    return k, None
        for rect, kind, arg in reversed(self.hits):
            if rect.contains(point):
                return kind, arg
        return None

    def edges(self, pos):
        margin = 5
        edges = Qt.Edge(0)
        if pos.x() < margin:
            edges |= Qt.Edge.LeftEdge
        elif pos.x() > self.width() - margin:
            edges |= Qt.Edge.RightEdge
        if pos.y() < margin:
            edges |= Qt.Edge.TopEdge
        elif pos.y() > self.height() - margin:
            edges |= Qt.Edge.BottomEdge
        return edges

    def mousePressEvent(self, e):
        if e.button() == Qt.MouseButton.RightButton:
            self.show_controls(e.globalPosition().toPoint())
            return
        if e.button() != Qt.MouseButton.LeftButton:
            return
        self.tip.hide()
        self.pressed = self.hit(e.position())
        self.dragged = False
        self.resize_edges = self.edges(e.position())
        self.drag = (e.globalPosition().toPoint(), self.geometry())
        self.update()

    def restore_backdrop(self):
        if self._backdrop_suspended:
            self._backdrop_suspended = False
            if not self.testing and self.isVisible():
                native.apply_backdrop(int(self.winId()), self.theme.dark)

    def mouseMoveEvent(self, e):
        if self.drag and e.buttons() & Qt.MouseButton.LeftButton:
            delta = e.globalPosition().toPoint() - self.drag[0]
            if delta.manhattanLength() < 3 and not self.dragged:
                return
            if not self.dragged:
                self.dragged = True
                self.frame.stop()
                self.sampler.set_mode("idle")
                self.card.hide()
                if not self.testing:
                    self._backdrop_suspended = True
                    native.suspend_backdrop(int(self.winId()))
            rect = self.drag[1]
            ed = self.resize_edges
            if ed:
                left = rect.left() + delta.x() if ed & Qt.Edge.LeftEdge else rect.left()
                right = (
                    rect.right() + delta.x() if ed & Qt.Edge.RightEdge else rect.right()
                )
                top = rect.top() + delta.y() if ed & Qt.Edge.TopEdge else rect.top()
                bottom = (
                    rect.bottom() + delta.y()
                    if ed & Qt.Edge.BottomEdge
                    else rect.bottom()
                )
                if right - left + 1 < self.minimumWidth():
                    if ed & Qt.Edge.LeftEdge:
                        left = right - self.minimumWidth() + 1
                    else:
                        right = left + self.minimumWidth() - 1
                if bottom - top + 1 < self.minimumHeight():
                    if ed & Qt.Edge.TopEdge:
                        top = bottom - self.minimumHeight() + 1
                    else:
                        bottom = top + self.minimumHeight() - 1
                self.setGeometry(left, top, right - left + 1, bottom - top + 1)
            else:
                self.move(rect.topLeft() + delta)
            return
        self.top_controls = e.position().y() < 28
        hit = self.hit(e.position())
        edges = self.edges(e.position())
        if edges:
            diagonal = (
                bool(edges & Qt.Edge.LeftEdge) or bool(edges & Qt.Edge.RightEdge)
            ) and (bool(edges & Qt.Edge.TopEdge) or bool(edges & Qt.Edge.BottomEdge))
            cursor = (
                Qt.CursorShape.SizeFDiagCursor
                if diagonal
                and (bool(edges & Qt.Edge.LeftEdge) == bool(edges & Qt.Edge.TopEdge))
                else Qt.CursorShape.SizeBDiagCursor
                if diagonal
                else Qt.CursorShape.SizeHorCursor
                if edges & (Qt.Edge.LeftEdge | Qt.Edge.RightEdge)
                else Qt.CursorShape.SizeVerCursor
            )
        else:
            cursor = (
                Qt.CursorShape.PointingHandCursor
                if hit and hit[0] in ("process", "sort", "pin", "hide")
                else Qt.CursorShape.ArrowCursor
            )
        self.setCursor(cursor)
        if hit != self.hover:
            self.hover = hit
            self.tip.hide()
            self.sampler.hold = (
                bool(hit and hit[0] == "process") or self.card.isVisible()
            )
            if hit and hit[0] in ("detail", "core") and not self.card.isVisible():
                if hit[0] == "core":
                    idx = hit[1]
                    info = self.sampler.info[idx]
                    lines = [
                        self.tr("核心 ", "Core ")
                        + str(info["core"])
                        + f" · {info['name']} "
                        + self.tr("线程 ", "Thread ")
                        + str(info["thread"]),
                        number(self.data["cores"][idx])
                        + "% · "
                        + (
                            number(self.data["freqs"][idx], 2) + " GHz"
                            if self.data["freqs"][idx] is not None
                            else self.tr("频率不可用", "Frequency unavailable")
                        ),
                        self.tr(
                            "频率为 Windows PDH 性能计数器估计",
                            "Frequency estimated from Windows PDH performance counter",
                        ),
                    ]
                else:
                    lines = self.detail(hit[1]).splitlines()
                self.tip.present(lines, e.globalPosition().toPoint())
        self.update()

    def mouseReleaseEvent(self, e):
        if e.button() != Qt.MouseButton.LeftButton:
            return
        press = self.pressed
        self.pressed = None
        if self.dragged:
            self.relayout(snap=True)
            area = (
                QApplication.screenAt(self.geometry().center())
                or QApplication.primaryScreen()
            ).availableGeometry()
            x, y = self.x(), self.y()
            if abs(x - area.left()) < 16:
                x = area.left() + 8
            if abs(y - area.top()) < 16:
                y = area.top() + 8
            if abs(x + self.width() - area.right()) < 16:
                x = area.right() - self.width() - 8
            if abs(y + self.height() - area.bottom()) < 16:
                y = area.bottom() - self.height() - 8
            self.move(x, y)
            self.save_geometry()
        elif press and press == self.hit(e.position()):
            kind, arg = press
            if kind == "sort":
                self.change_config({"processSort": arg})
            elif kind == "pin":
                self.change_config({"floating": not self.store.data["floating"]})
            elif kind == "hide":
                self.hide_panel()
            elif kind == "process":
                if self.card.isVisible() and self.card.key == arg:
                    self.card.hide()
                else:
                    self.card.present(
                        self.card_lines(arg), e.globalPosition().toPoint(), arg
                    )
        else:
            self.card.hide()
        self.drag = None
        self.resize_edges = None
        self.dragged = False
        self.restore_backdrop()
        self.sampler.hold = self.card.isVisible()
        if self.isVisible():
            self.sampler.set_mode("full")
        self.update()

    def leaveEvent(self, e):
        if not self.drag:
            self.top_controls = False
            self.hover = None
            self.tip.hide()
            self.sampler.hold = self.card.isVisible()
            self.update()
        super().leaveEvent(e)

    def keyPressEvent(self, e):
        if e.key() == Qt.Key.Key_Escape:
            if self.card.isVisible():
                self.card.hide()
                self.sampler.hold = False
            else:
                self.hide_panel()
        elif e.key() == Qt.Key.Key_Space:
            self.show_controls(self.mapToGlobal(QPoint(10, 10)))
        else:
            super().keyPressEvent(e)
