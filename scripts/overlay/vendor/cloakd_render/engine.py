import math
import json
import copy
from pathlib import Path

from PySide6.QtCore import QObject, Property, Signal, Slot, QUrl, QThread
from PySide6.QtGui import QGuiApplication, QColor
from PySide6.QtQml import QQmlComponent, QQmlEngine, qmlRegisterType
from PySide6.QtQuick import QQuickItem, QQuickWindow
from shiboken6 import isValid

from .python_graph import PythonGraph

ROOT = Path(__file__).resolve().parent
DEFAULTS = dict(kind="text", text="Hello", x=0., y=0., width=400., height=240.,
                color="#e7f1ed", background="#192923", accent="#8fc8b5", radius=12.,
                fontSize=24., opacity=1., visible=True, renderer="python", shader="none",
                shaderPath="", strength=.5, speed=1., amplitude=.7, samples=256)
DEFAULTS.update(value=0., minimum=0., maximum=1., checked=False, placeholder="",
                imagePath="", qmlPath="", customProperties={}, borderColor="#456253",
                borderWidth=0., rotation=0.)
KINDS = ("text", "panel", "line", "circle", "image", "graph", "shader",
         "button", "checkbox", "slider", "input", "progress", "custom")
_registered = False


def validated(spec):
    if set(spec) != set(DEFAULTS):
        raise ValueError("Unknown render properties")
    if spec["kind"] not in KINDS:
        raise ValueError("Unknown element kind")
    for key in ("text", "shaderPath", "qmlPath", "imagePath", "placeholder"):
        if not isinstance(spec[key], str):
            raise ValueError(key + " must be a string")
    for key in ("color", "background", "accent", "borderColor"):
        if not isinstance(spec[key], str) or not QColor(spec[key]).isValid():
            raise ValueError("Invalid color: " + key)
    for key, low, high in (("x", -100000, 100000), ("y", -100000, 100000),
                           ("width", 1, 16384), ("height", 1, 16384), ("radius", 0, 1000),
                           ("fontSize", 1, 512), ("opacity", 0, 1), ("strength", 0, 1),
                           ("speed", 0, 10), ("amplitude", 0, 1)):
        value = spec[key]
        if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError(f"{key}: expected finite number in [{low}, {high}]")
    for key in ("value", "minimum", "maximum", "borderWidth", "rotation"):
        if type(spec[key]) not in (int, float) or not math.isfinite(spec[key]):
            raise ValueError(key + " must be a finite number")
    if spec["maximum"] <= spec["minimum"] or not 0 <= spec["borderWidth"] <= 100 or type(spec["checked"]) is not bool:
        raise ValueError("Invalid range, borderWidth or checked")
    if spec["kind"] in ("slider", "progress") and not spec["minimum"] <= spec["value"] <= spec["maximum"]:
        raise ValueError("value must be inside minimum..maximum")
    if not isinstance(spec["customProperties"], dict) or len(json.dumps(spec["customProperties"], allow_nan=False)) > 1048576:
        raise ValueError("customProperties must be a JSON object up to 1 MiB")
    if type(spec["visible"]) is not bool or type(spec["samples"]) is not int or not 16 <= spec["samples"] <= 4096:
        raise ValueError("Invalid visible or samples")
    if spec["renderer"] not in ("python", "native") or spec["shader"] not in ("none", "aurora", "grid", "custom"):
        raise ValueError("Unknown renderer or shader")
    if spec["renderer"] == "native" and not (ROOT / "qml/Cloakd/Native/libcloakdnativeplugin.so").is_file():
        raise ValueError("Native plugin is missing; run build.sh or use renderer='python'")
    if spec["shader"] == "custom" and not Path(spec["shaderPath"]).is_file():
        raise ValueError("Custom shader file does not exist")
    if spec["kind"] == "custom" and (not spec["qmlPath"].endswith('.qml') or not Path(spec["qmlPath"]).is_file()):
        raise ValueError("Custom QML file does not exist")
    return copy.deepcopy(spec)


class RenderObject(QObject):
    """Keep this object alive for as long as its QQuickItem / window is used."""
    changed = Signal()
    error = Signal(str)
    event = Signal(str, object)

    def __init__(self, renderer, properties):
        super().__init__(renderer)
        self._spec = validated(DEFAULTS | properties)
        self.item = None
        self.window = None
        self._errors = {}
        self.closed = False
        self.handlers = {}

    @Property("QVariantMap", notify=changed)
    def spec(self):
        return copy.deepcopy(self._spec)

    @Property(str, notify=changed)
    def shaderUrl(self):
        name = self._spec["shader"]
        if name == "none":
            return ""
        path = Path(self._spec["shaderPath"]) if name == "custom" else ROOT / "shaders" / (name + ".qsb")
        return QUrl.fromLocalFile(str(path.resolve())).toString()

    @property
    def errors(self):
        return dict(self._errors)

    def update(self, **properties):
        if self.closed or not isValid(self):
            raise RuntimeError("RenderObject is closed")
        if QThread.currentThread()!=self.thread():
            raise RuntimeError('Render updates must run in the Qt GUI thread')
        data = validated(self._spec | properties)
        if data != self._spec:
            self._spec = data
            self.changed.emit()

    def on(self, event, callback):
        """Set or replace a callback receiving (RenderObject, payload)."""
        if callback is not None and not callable(callback):
            raise TypeError('callback must be callable or None')
        self.handlers[event] = callback
        return self

    @Slot(str, "QVariant")
    def dispatch(self, name, payload):
        if self.closed:
            return
        if hasattr(payload, 'toVariant'):
            payload = payload.toVariant()
        if name == "change":
            field = {"checkbox": "checked", "slider": "value", "input": "text"}.get(self._spec["kind"])
            if field:
                try:
                    self.update(**{field: payload})
                except (ValueError,TypeError) as exc:
                    self.renderError('input',str(exc))
                    return
        self.event.emit(name, payload)
        callback = self.handlers.get(name)
        if callback:
            try:
                callback(self, payload)
            except Exception as exc:
                import logging
                logging.exception('Render callback failed')
                self.renderError("callback", str(exc))

    @Property(str, notify=changed)
    def customUrl(self):
        return QUrl.fromLocalFile(str(Path(self._spec["qmlPath"]).resolve())).toString() if self._spec["qmlPath"] else ""

    @Property(str, notify=changed)
    def imageUrl(self):
        return QUrl.fromLocalFile(str(Path(self._spec["imagePath"]).resolve())).toString() if self._spec["imagePath"] else ""

    @Slot(str, str)
    def renderError(self, stage, message):
        self._errors[stage] = message
        if message:
            self.error.emit(message)

    def close(self):
        if self.closed:
            return
        if isValid(self) and QThread.currentThread()!=self.thread():
            raise RuntimeError('RenderObject.close must run in the Qt GUI thread')
        self.closed = True
        if self.window and isValid(self.window):
            self.window.close()
            self.window.deleteLater()
        elif self.item and isValid(self.item):
            self.item.setVisible(False)
            self.item.setParentItem(None)
            self.item.deleteLater()
        # The native QObject is a child of the visual root; deleting the visual
        # root also deletes this controller, after QML has detached bindings.


class Renderer(QObject):
    """Create standalone windows or embed items in an existing Qt Quick scene."""
    def __init__(self, engine=None, parent=None):
        super().__init__(parent)
        if QGuiApplication.instance() is None:
            raise RuntimeError("Create QGuiApplication before Renderer")
        if QThread.currentThread()!=QGuiApplication.instance().thread():
            raise RuntimeError('Create Renderer in the Qt GUI thread')
        global _registered
        if not _registered:
            qmlRegisterType(PythonGraph, "Cloakd.RenderPython", 1, 0, "PythonGraph")
            _registered = True
        self.engine = engine or QQmlEngine()
        self.engine.addImportPath(str(ROOT / "qml"))
        self.objects = []
        self.components = {}
        self._frames = {}

    def _create(self, filename, properties):
        obj = RenderObject(self, properties)
        if filename not in self.components:
            self.components[filename] = QQmlComponent(self.engine, QUrl.fromLocalFile(str(ROOT / "qml" / filename)))
        component = self.components[filename]
        item = component.createWithInitialProperties({"node": obj})
        if item is None:
            obj.deleteLater()
            raise RuntimeError("\n".join(e.toString() for e in component.errors()))
        QQmlEngine.setObjectOwnership(item, QQmlEngine.ObjectOwnership.CppOwnership)
        obj.setParent(item)
        self.objects = [node for node in self.objects if not node.closed and isValid(node)]
        self.objects.append(obj)
        return obj, item

    def create_window(self, **properties):
        obj, window = self._create("RenderWindow.qml", properties)
        obj.window = window
        QObject.setParent(window, self)
        obj.item = window.contentItem().childItems()[0]
        return obj

    def create_item(self, parent_item, **properties):
        if not isinstance(parent_item, QQuickItem) or not isValid(parent_item):
            raise TypeError("parent_item must be a QQuickItem")
        obj, item = self._create("RenderItem.qml", properties)
        obj.item = item
        item.setParentItem(parent_item)
        item.setParent(parent_item)
        return obj

    def close(self):
        for obj in list(self.objects):
            obj.close()
        self.objects.clear()
        self._frames.clear()

    @property
    def available_backends(self):
        return ('python', 'native') if (ROOT/'qml/Cloakd/Native/libcloakdnativeplugin.so').exists() else ('python',)

    def frame(self, parent_item):
        from .frame import Frame
        return Frame(self, parent_item)
