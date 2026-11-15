import math

from PySide6.QtCore import Property, Signal, QRectF
from PySide6.QtGui import QColor, QPainterPath, QPen
from PySide6.QtQuick import QQuickPaintedItem


class PythonGraph(QQuickPaintedItem):
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.values = dict(phase=0.0, amplitude=0.7, samples=256, tint=QColor("#8fc8b5"))
        self.setAntialiasing(True)

    def put(self, key, value):
        if self.values[key] != value:
            self.values[key] = value
            self.changed.emit()
            self.update()

    phase = Property(float, lambda s: s.values["phase"], lambda s, v: s.put("phase", v), notify=changed)
    amplitude = Property(float, lambda s: s.values["amplitude"], lambda s, v: s.put("amplitude", v), notify=changed)
    samples = Property(int, lambda s: s.values["samples"], lambda s, v: s.put("samples", v), notify=changed)
    tint = Property(QColor, lambda s: s.values["tint"], lambda s, v: s.put("tint", v), notify=changed)

    def paint(self, painter):
        count = max(16, min(4096, self.values["samples"]))
        path = QPainterPath()
        for i in range(count):
            x = i / (count - 1)
            wave = math.sin(x * 12 + self.values["phase"]) * 0.65 + math.sin(x * 27 - self.values["phase"] * 0.7) * 0.25
            y = self.height() * (0.5 + wave * 0.44 * self.values["amplitude"])
            if i:
                path.lineTo(x * self.width(), y)
            else:
                path.moveTo(x * self.width(), y)
        painter.setPen(QPen(self.values["tint"], 2))
        painter.drawPath(path)
