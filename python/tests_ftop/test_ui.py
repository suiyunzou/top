import pytest
from PySide6.QtWidgets import QApplication, QWidget
from PySide6.QtCore import QPoint, Qt
from PySide6.QtTest import QTest
from PySide6.QtGui import QFontMetricsF
from ftop_windows.core import ConfigStore
from ftop_windows.app import FixtureSampler
from ftop_windows.ui import Panel, font
from ftop_windows import native


@pytest.fixture(scope="module")
def app():
    instance = QApplication.instance() or QApplication([])
    instance.setQuitOnLastWindowClosed(False)
    return instance


@pytest.fixture
def panel(tmp_path, app):
    store = ConfigStore(tmp_path / "config.json5")
    store.save(dict(motion=False, appearance="dark", language="zh"))
    panel = Panel(store, FixtureSampler(store.data), testing=True)
    panel.move(100, 100)
    panel.show()
    app.processEvents()
    panel.accept_snapshot()
    yield panel
    panel.card.hide()
    panel.tip.hide()
    panel.controls.hide()
    panel.hide()
    panel.deleteLater()
    app.processEvents()


def test_theme_palette_and_module_changes_apply(panel):
    assert panel.theme.dark
    panel.change_config(
        {"appearance": "light", "palette": "warm", "hidden": ["gpu", "power"]}
    )
    assert not panel.theme.dark and panel.theme.cpu == "#DC4A2B"
    assert not any(h[2] == "gpu" for h in panel.hits if h[1] == "detail")


def test_narrow_text_elision_never_overflows():
    metrics = QFontMetricsF(font(12))
    text = metrics.elidedText("chrome.exe ×100", Qt.TextElideMode.ElideRight, 15)
    assert metrics.horizontalAdvance(text) <= 15


def test_process_card_is_separate_window_and_tracks_identity(panel, app):
    rect, _, key = next(h for h in panel.hits if h[1] == "process")
    pos = (rect.center() * panel.layout.scale).toPoint()
    QTest.mouseClick(panel, Qt.MouseButton.LeftButton, pos=pos)
    app.processEvents()
    assert panel.card.isVisible() and panel.card.isWindow() and panel.card.key == key
    assert any("单核 100%" in line for line in panel.card.lines)
    assert any("PID" in line for line in panel.card.lines)
    QTest.keyClick(panel, Qt.Key.Key_Escape)
    assert not panel.card.isVisible()


def test_large_list_sort_is_one_continuous_ranking(panel, app):
    panel.resize(1000, 500)
    panel.relayout(snap=True)
    panel.accept_snapshot()
    app.processEvents()
    assert panel.layout.candidate.wide
    rect, _, _ = next(h for h in panel.hits if h[1:] == ("sort", "memory"))
    QTest.mouseClick(
        panel,
        Qt.MouseButton.LeftButton,
        pos=(rect.center() * panel.layout.scale).toPoint(),
    )
    app.processEvents()
    assert panel.store.data["processSort"] == "memory"
    keys = [h[2] for h in panel.hits if h[1] == "process"]
    assert keys == panel.data["memory_order"][: len(keys)]
    assert len(keys) == len(set(keys))


@pytest.mark.parametrize(
    "point,delta",
    [
        (QPoint(1, 100), QPoint(-40, 0)),
        (QPoint(299, 100), QPoint(60, 0)),
        (QPoint(100, 1), QPoint(0, -30)),
        (QPoint(100, 419), QPoint(0, 50)),
    ],
)
def test_resize_from_every_edge(panel, app, point, delta):
    panel.resize(300, 420)
    app.processEvents()
    old = panel.geometry()
    QTest.mousePress(panel, Qt.MouseButton.LeftButton, pos=point)
    assert panel.resize_edges
    QTest.mouseMove(panel, point + delta, delay=20)
    QTest.mouseRelease(panel, Qt.MouseButton.LeftButton, pos=point + delta)
    app.processEvents()
    assert panel.geometry() != old
    assert not panel.drag
    assert panel.width() >= 40 and panel.height() >= 16


def test_all_layout_content_regions_fit(panel, app):
    for w, h in [
        (48, 20),
        (96, 32),
        (400, 30),
        (760, 30),
        (94, 330),
        (280, 150),
        (300, 420),
        (480, 300),
        (660, 230),
        (1000, 500),
    ]:
        panel.resize(w, h)
        panel.relayout(snap=True)
        panel.accept_snapshot()
        app.processEvents()
        l = panel.layout
        for rect, kind, arg in panel.hits:
            # A process's hover padding intentionally exceeds its artwork by four pixels.
            assert rect.left() >= 0 and rect.top() >= 0, (w, h, kind, rect)
            assert (
                rect.right() <= l.width + 0.01 and rect.bottom() <= l.height + 0.01
            ), (w, h, kind, rect, l)
        for rect, *_ in panel.bars:
            assert rect.height() > 0 and rect.right() <= l.width


def test_hidden_stops_animation_and_sampling(panel):
    panel.hide_panel()
    assert panel.sampler.mode == "idle" and not panel.frame.isActive()
    panel.show_panel()
    assert panel.isVisible()


def test_full_occlusion_and_partial_visibility(panel, app):
    panel.change_config({"floating": True})
    panel.show()
    panel.raise_()
    app.processEvents()
    cover = QWidget(
        None,
        Qt.WindowType.Tool
        | Qt.WindowType.FramelessWindowHint
        | Qt.WindowType.WindowStaysOnTopHint,
    )
    cover.setStyleSheet("background:#345678")
    cover.setGeometry(panel.geometry().adjusted(-20, -20, 20, 20))
    cover.show()
    cover.raise_()
    app.processEvents()
    QTest.qWait(200)
    try:
        assert native.fully_occluded(int(panel.winId()))
        cover.move(panel.x() + panel.width() // 2, panel.y())
        app.processEvents()
        QTest.qWait(100)
        assert not native.fully_occluded(int(panel.winId()))
    finally:
        cover.hide()
        cover.deleteLater()


def test_right_click_settings_module_toggle_and_sort_control(panel, app):
    from ftop_windows.ui import DiscButton
    from PySide6.QtWidgets import QComboBox

    QTest.mouseClick(panel, Qt.MouseButton.RightButton, pos=QPoint(70, 60))
    app.processEvents()
    assert panel.controls.isVisible()
    button = next(
        b for b in panel.controls.findChildren(DiscButton) if b.glyph_name == "gpu"
    )
    QTest.mouseClick(button, Qt.MouseButton.LeftButton)
    app.processEvents()
    assert "gpu" in panel.store.data["hidden"]
    assert "gpu" in ConfigStore(panel.store.path).data["hidden"]
    button = next(
        b
        for b in panel.controls.findChildren(DiscButton)
        if b.glyph_name == "more" and b.isVisible()
    )
    QTest.mouseClick(button, Qt.MouseButton.LeftButton)
    app.processEvents()
    boxes = panel.controls.findChildren(QComboBox)
    interval = next(b for b in boxes if b.findData(10.0) >= 0)
    interval.setCurrentIndex(interval.findData(2.0))
    app.processEvents()
    assert panel.store.data["interval"] == 2.0


def test_window_restored_fully_onscreen(panel):
    panel.move(30000, 30000)
    panel.ensure_onscreen()
    assert any(
        screen.availableGeometry().contains(panel.geometry())
        for screen in QApplication.screens()
    )


@pytest.mark.parametrize("size", [(300, 420), (546, 305), (660, 400)])
def test_process_cache_matches_destination_pixels(panel, app, size):
    import math

    panel.resize(*size)
    panel.relayout(snap=True)
    panel.accept_snapshot()
    app.processEvents()
    density = panel.devicePixelRatioF() * panel.layout.scale
    assert panel._row_pixmaps
    for key, pix in panel._row_pixmaps.items():
        rect = panel._row_rects[key]
        assert pix.devicePixelRatioF() == pytest.approx(density)
        assert pix.width() == math.ceil(rect.width() * density)
        assert pix.height() == math.ceil(rect.height() * density)


def test_hover_preserves_other_process_text_contrast(panel):
    from PySide6.QtCore import QRectF

    keys = list(panel._row_rects)
    assert len(keys) > 1
    rect = panel._row_rects[keys[1]]
    region = QRectF(
        rect.topLeft() * panel.layout.scale, rect.size() * panel.layout.scale
    ).toAlignedRect()
    panel.hover = None
    before = panel.grab(region).toImage()
    panel.hover = ("process", keys[0])
    after = panel.grab(region).toImage()
    assert before == after


@pytest.mark.parametrize("app_light,taskbar_light", [(1, 0), (0, 1)])
def test_tray_uses_taskbar_theme_not_app_theme(
    panel, monkeypatch, app_light, taskbar_light
):
    import winreg

    values = {"AppsUseLightTheme": app_light, "SystemUsesLightTheme": taskbar_light}
    monkeypatch.setattr(
        winreg, "QueryValueEx", lambda key, name: (values[name], winreg.REG_DWORD)
    )
    panel._last_env = 0
    panel.tick()
    icon = panel.tray.icon().pixmap(32, 32).toImage()
    colors = {
        icon.pixelColor(x, y).name()
        for x in range(icon.width())
        for y in range(icon.height())
        if icon.pixelColor(x, y).alpha() > 250
    }
    assert colors == {"#121826" if taskbar_light else "#ffffff"}


def test_tray_recolors_without_cpu_change_even_while_hidden(panel, monkeypatch):
    import winreg

    values = {"AppsUseLightTheme": 1, "SystemUsesLightTheme": 0}
    monkeypatch.setattr(
        winreg, "QueryValueEx", lambda key, name: (values[name], winreg.REG_DWORD)
    )
    panel.hide_panel()
    panel._last_env = 0
    panel.tick()
    dark = panel.tray.icon().pixmap(32, 32).toImage()
    values["SystemUsesLightTheme"] = 1
    panel._last_env = 0
    panel.tick()
    light = panel.tray.icon().pixmap(32, 32).toImage()
    assert dark != light


@pytest.mark.parametrize("resize", [False, True])
def test_drag_suspends_composition_before_geometry_and_restores_after_release(
    panel, app, monkeypatch, resize
):
    calls = []
    monkeypatch.setattr(
        native,
        "suspend_backdrop",
        lambda hwnd: calls.append(("suspend", panel.geometry())) or True,
        raising=False,
    )
    monkeypatch.setattr(
        native,
        "apply_backdrop",
        lambda hwnd, dark: calls.append(("restore", panel.geometry())) or True,
    )
    panel.testing = False
    panel.resize(300, 420)
    app.processEvents()
    old = panel.geometry()
    point = QPoint(299, 100) if resize else QPoint(70, 35)
    QTest.mousePress(panel, Qt.MouseButton.LeftButton, pos=point)
    assert not calls
    QTest.mouseMove(panel, point + QPoint(30, 0), delay=20)
    assert panel.dragged and panel.geometry() != old
    assert calls == [("suspend", old)]
    # Theme/config polling must not switch the expensive material back on mid-drag.
    panel._last_env = 0
    panel.tick()
    assert len(calls) == 1
    QTest.mouseRelease(panel, Qt.MouseButton.LeftButton, pos=point + QPoint(30, 0))
    assert [call[0] for call in calls] == ["suspend", "restore"]
    assert not panel.drag and panel.sampler.mode == "full"


@pytest.mark.parametrize("cancel", ["capture-lost", "hidden"])
def test_interrupted_drag_resets_interaction_state(panel, app, monkeypatch, cancel):
    from PySide6.QtCore import QEvent

    calls = []
    monkeypatch.setattr(native, "suspend_backdrop", lambda hwnd: True)
    monkeypatch.setattr(
        native, "apply_backdrop", lambda hwnd, dark: calls.append("restore") or True
    )
    panel.testing = False
    QTest.mousePress(panel, Qt.MouseButton.LeftButton, pos=QPoint(70, 35))
    QTest.mouseMove(panel, QPoint(100, 35), delay=20)
    assert panel.dragged and panel._backdrop_suspended
    if cancel == "hidden":
        panel.hide_panel()
    else:
        QApplication.sendEvent(panel, QEvent(QEvent.Type.UngrabMouse))
    assert panel.drag is None and not panel.dragged and not panel._backdrop_suspended
    assert panel.sampler.mode == ("idle" if cancel == "hidden" else "full")
    if cancel == "capture-lost":
        assert calls == ["restore"]
    else:
        calls.clear()
        panel.show_panel()
        assert calls == ["restore"]
    QTest.mouseRelease(panel, Qt.MouseButton.LeftButton, pos=QPoint(70, 35))


def test_more_settings_stay_on_screen_after_expanding_and_collapsing(panel, app):
    from ftop_windows.ui import DiscButton
    from PySide6.QtWidgets import QPushButton

    area = QApplication.primaryScreen().availableGeometry()
    panel.show_controls(area.bottomRight() - QPoint(5, 5))
    app.processEvents()
    controls = panel.controls
    collapsed_height = controls.height()
    more = next(
        b
        for b in controls.findChildren(DiscButton)
        if b.glyph_name == "more" and b.isVisible()
    )
    QTest.mouseClick(more, Qt.MouseButton.LeftButton)
    app.processEvents()
    assert controls.more and controls.height() > collapsed_height
    assert area.contains(controls.geometry())
    button = next(
        b
        for b in controls.findChildren(QPushButton)
        if "打开设置文件" in b.text() and b.isVisible()
    )
    assert controls.rect().contains(button.mapTo(controls, button.rect().bottomRight()))
    more = next(
        b
        for b in controls.findChildren(DiscButton)
        if b.glyph_name == "more" and b.isVisible()
    )
    QTest.mouseClick(more, Qt.MouseButton.LeftButton)
    app.processEvents()
    assert not controls.more and controls.height() == collapsed_height
    assert area.contains(controls.geometry())


def test_panel_round_corners_clip_native_backdrop_after_resize(panel, app):
    import ctypes
    from ctypes import wintypes

    g = ctypes.windll.gdi32
    g.CreateRectRgn.restype = wintypes.HANDLE
    g.PtInRegion.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_int]
    g.DeleteObject.argtypes = [wintypes.HANDLE]
    native.user32.GetWindowRgn.argtypes = [wintypes.HWND, wintypes.HANDLE]
    region = g.CreateRectRgn(0, 0, 0, 0)
    try:
        for w, h in ((546, 305), (400, 30), (300, 420)):
            panel.resize(w, h)
            app.processEvents()
            native.apply_backdrop(int(panel.winId()), True)
            assert native.user32.GetWindowRgn(int(panel.winId()), region) > 0
            assert not g.PtInRegion(region, 0, 0)
            assert g.PtInRegion(
                region,
                round(w * panel.devicePixelRatioF() / 2),
                round(h * panel.devicePixelRatioF() / 2),
            )
            native.suspend_backdrop(int(panel.winId()))
    finally:
        g.DeleteObject(region)


def test_more_settings_scroll_on_short_screen(panel, app, monkeypatch):
    from PySide6.QtCore import QRect
    from PySide6.QtWidgets import QPushButton

    area = QRect(0, 0, 500, 360)

    class ShortScreen:
        def availableGeometry(self):
            return area

    monkeypatch.setattr(QApplication, "screenAt", lambda point: ShortScreen())
    panel.show_controls(QPoint(480, 340))
    panel.controls.expand()
    app.processEvents()
    controls = panel.controls
    assert area.contains(controls.geometry())
    scroll = controls.extra_scroll
    assert scroll.verticalScrollBar().maximum() > 0
    button = next(
        b
        for b in controls.findChildren(QPushButton)
        if "打开设置文件" in b.text() and b.isVisible()
    )
    scroll.ensureWidgetVisible(button)
    app.processEvents()
    assert (
        scroll.viewport()
        .rect()
        .contains(button.mapTo(scroll.viewport(), button.rect().center()))
    )


def test_process_card_dismisses_after_pointer_leaves_row_and_card(
    panel, app, monkeypatch
):
    from PySide6.QtGui import QCursor

    rect, _, key = next(h for h in panel.hits if h[1] == "process")
    local = (rect.center() * panel.layout.scale).toPoint()
    pointer = [panel.mapToGlobal(local)]
    monkeypatch.setattr(QCursor, "pos", lambda: pointer[0])
    QTest.mouseClick(panel, Qt.MouseButton.LeftButton, pos=local)
    QTest.qWait(250)
    assert panel.card.isVisible() and panel.sampler.hold
    pointer[0] = panel.card.geometry().center()
    QTest.qWait(250)
    assert panel.card.isVisible()
    pointer[0] = QPoint(-10000, -10000)
    QTest.qWait(300)
    assert not panel.card.isVisible()
    assert not panel.sampler.hold
    assert not panel.card._dismiss_timer.isActive()
