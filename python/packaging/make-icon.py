"""Render the monitor's own capsule-bar motif as its executable icon."""

from pathlib import Path
from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QImage, QPainter, QColor

target = Path(__file__).resolve().parents[1] / "ftop_windows/assets/ftop.ico"
target.parent.mkdir(parents=True, exist_ok=True)
image = QImage(256, 256, QImage.Format.Format_ARGB32)
image.fill(Qt.GlobalColor.transparent)
p = QPainter(image)
p.setRenderHint(QPainter.RenderHint.Antialiasing)
p.setPen(Qt.PenStyle.NoPen)
p.setBrush(QColor("#1b1e29"))
p.drawRoundedRect(QRectF(0, 0, 256, 256), 56, 56)
for x, height, color in (
    (47, 83, "#5B9BFF"),
    (105, 153, "#45D1BD"),
    (163, 117, "#5B9BFF"),
):
    p.setBrush(QColor("#303642"))
    p.drawRoundedRect(QRectF(x, 40, 44, 176), 20, 20)
    p.setBrush(QColor(color))
    p.drawRoundedRect(QRectF(x, 216 - height, 44, height), 20, 20)
p.end()
assert image.save(str(target), "ICO")
print(target)
