#!/usr/bin/env python3
"""SixBox -> Cloakd Render SDK adapter for Linux, running outside Proton."""
import argparse
import fcntl
import os
from pathlib import Path
import signal
import sys
import time

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT/'vendor'))
# Managed XWayland windows receive the SAME native KWin capture rules as Cloakd.
# A layer-shell surface would bypass those window rules.
os.environ.setdefault('QT_QPA_PLATFORM', 'xcb')
from PySide6.QtCore import Property, QTimer, QRect, QRectF, Qt, Signal
from PySide6.QtGui import QGuiApplication, QColor, QFont, QFontMetricsF, QPainter, QPen
from PySide6.QtQml import qmlRegisterType
from PySide6.QtQuick import QQuickPaintedItem, QQuickWindow
from cloakd_render import Renderer
from protocol import read_frame, write_ack, STALE_SECONDS
from capture import APP_ID, TITLE, policy_configured
from effects import draw_box
from model_image import Reader as ModelReader


def rgba(value):
    return QColor(value & 255, value >> 8 & 255, value >> 16 & 255, value >> 24 & 255)


class FrameCanvas(QQuickPaintedItem):
    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._frame = {}
        self._model = None
        self.setAntialiasing(True)

    @Property('QVariantMap', notify=changed)
    def frame(self):
        return self._frame

    @frame.setter
    def frame(self, value):
        previous=self.content_bounds()
        self._frame = value.toVariant() if hasattr(value, 'toVariant') else value
        self.repaint_content(previous)
        self.changed.emit()

    def set_model(self, model):
        if model is not None and model.image.isNull(): model=None
        if model is self._model: return
        previous=self.content_bounds()
        self._model=model
        self.repaint_content(previous)

    def content_bounds(self):
        frame=self._frame
        if not frame.get('mask') or not frame.get('width') or not frame.get('height'):return None
        viewport=frame.get('viewport',dict(x=0.,y=0.,width=self.width(),height=self.height()))
        sx,sy=viewport['width']/frame['width'],viewport['height']/frame['height']
        result=None
        def include(x,y,w,h):
            nonlocal result
            rect=QRectF(viewport['x']+x*sx,viewport['y']+y*sy,w*sx,h*sy)
            result=rect if result is None else result.united(rect)
        effects=frame.get('effects',{})
        margin=max(4.,effects.get('glowRadius',0.)+3 if effects.get('flags',0)&1 else 0.,
                   effects.get('stencilThickness',0.)+3 if effects.get('flags',0)&2 else 0.)
        for command in frame.get('records',[]):
            x,y=command['x'],command['y']
            if command['kind']==1:
                include(x-margin,y-margin,command['width']+margin*2,command['height']+margin*2)
            else:
                size=command['size'];width=len(command['text'])*size*2+16
                include(x-width/2,y-size*2-8,width,size*2+16)
        model=self._model
        if frame['mask']&4 and model is not None and (model.width,model.height)==(frame['width'],frame['height']) and not model.image.isNull():
            include(model.x-2,model.y-2,model.image.width()+4,model.image.height()+4)
        return result

    def repaint_content(self,previous):
        current=self.content_bounds()
        dirty=current if previous is None else previous if current is None else previous.united(current)
        if dirty is None:return
        if self.width()<1 or self.height()<1:self.update();return
        dirty=dirty.toAlignedRect().intersected(self.boundingRect().toAlignedRect())
        if not dirty.isEmpty():self.update(dirty)

    def paint(self, painter):
        frame = self._frame
        if not frame.get('mask'):
            return
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        # The window follows the game's client rectangle in physical X11 pixels.
        # Qt draws in logical pixels; the command coordinates use the game buffer.
        viewport=frame.get('viewport',dict(x=0.,y=0.,width=self.width(),height=self.height()))
        painter.translate(viewport['x'],viewport['y'])
        painter.scale(viewport['width']/frame['width'],viewport['height']/frame['height'])
        painter.setClipRect(QRectF(0,0,frame['width'],frame['height']),Qt.ClipOperation.IntersectClip)
        model=self._model
        if frame['mask'] & 4 and model is not None and (model.width,model.height)==(frame['width'],frame['height']) and not model.image.isNull():
            painter.drawImage(model.x,model.y,model.image)
        for cmd in frame['records']:
            x, y, w, h = (cmd[k] for k in ('x', 'y', 'width', 'height'))
            color = rgba(cmd['color'])
            if cmd['kind'] == 1:
                draw_box(painter, cmd, frame.get('effects', {}))
            else:
                font = QFont('Fira Sans'); font.setPixelSize(max(1,round(cmd['size'])))
                painter.setFont(font)
                metrics = QFontMetricsF(font)
                width = metrics.horizontalAdvance(cmd['text'])
                left, baseline = x-width/2, y-metrics.descent()
                background = rgba(cmd['background'])
                if background.alpha():
                    painter.setPen(Qt.PenStyle.NoPen); painter.setBrush(background)
                    painter.drawRoundedRect(QRectF(left-5,y-metrics.height()-2,width+10,metrics.height()+4),4,4)
                painter.setPen(QColor(0,0,0,color.alpha())); painter.drawText(left+1,baseline+1,cmd['text'])
                painter.setPen(color); painter.drawText(left,baseline,cmd['text'])


class Overlay:
    def __init__(self, app, directory, require_capture=True):
        self.app, self.directory, self.require_capture = app, directory, require_capture
        self.closed = False
        # A regular full-desktop sized window is clamped to availableGeometry
        # by KWin (panel struts). Use a managed fullscreen surface so geometry
        # cannot oscillate between e.g. 1080 and 1034 every update.
        self.screen_surface=app.platformName()=='xcb'
        self.renderer = Renderer()
        # Keep native geometry outside SDK's spec. RenderWindow.qml binds x/y
        # to spec.x/y; changing the frame otherwise resets the window to (0, 0).
        self.window = QQuickWindow()
        self.window.setColor(QColor(0, 0, 0, 0))
        self.object = self.renderer.create_item(self.window.contentItem(), kind='custom', qmlPath=str(ROOT/'Capture.qml'),
            width=640., height=480., visible=True, background='transparent', borderWidth=0., radius=0.,
            customProperties={'frame':{}})
        self.canvas=self.object.item.findChild(FrameCanvas)
        if self.canvas is None: raise RuntimeError('Stencil canvas was not created')
        self.model_reader=ModelReader()
        self.window.setTitle(TITLE)
        self.window.setFlags(Qt.WindowType.Window | Qt.WindowType.FramelessWindowHint |
            Qt.WindowType.WindowStaysOnTopHint | Qt.WindowType.WindowTransparentForInput | Qt.WindowType.WindowDoesNotAcceptFocus)
        self.last_frame = None
        self.last_id = None
        self.last_change = 0.
        self.last_policy = 0.
        self.policy_ok = not require_capture
        self.pending_ack = None
        self.window.frameSwapped.connect(self.acknowledge)
        self.timer = QTimer(); self.timer.timeout.connect(self.tick); self.timer.start(16)
        app.aboutToQuit.connect(self.close)

    def hide(self):
        self.pending_ack = None
        self.canvas.set_model(None)
        self.model_reader.reset()
        if self.window.isVisible():
            self.window.hide()
            self.object.update(customProperties={'frame':{}})
        (self.directory/'ack.bin').unlink(missing_ok=True)

    def update_model(self,frame):
        if not frame['mask'] & 4:
            self.canvas.set_model(None);self.model_reader.reset();return
        try:
            model=self.model_reader.read(self.directory/'stencil.bin',frame['session'],frame['width'],frame['height'])
            self.canvas.set_model(model)
        except (OSError,ValueError) as exc:
            self.canvas.set_model(None)
            if getattr(self,'last_model_error',None)!=str(exc):
                print('Stencil image:',str(exc),file=sys.stderr,flush=True)
                self.last_model_error=str(exc)

    def tick(self):
        now = time.monotonic()
        if now-self.last_policy > 1:
            self.last_policy = now
            self.policy_ok = not self.require_capture or policy_configured()
        try:
            frame = read_frame(self.directory/'frame.bin')
            # No visible entities is still an active frame. Keep the managed
            # window mapped and clear its canvas, otherwise turning the camera
            # back restarts KWin's window-open animation and Qt's render setup.
            # Native hide is reserved for disabled/unfocused/stale/unsafe input.
            if not frame or not frame['mask'] or not self.policy_ok:
                self.hide(); self.timer.setInterval(100); return
            self.timer.setInterval(4)
            identity = frame['session'], frame['sequence']
            if self.last_id and identity[0] == self.last_id[0] and identity[1] < self.last_id[1]:
                self.hide(); return
            if identity != self.last_id:
                self.last_id, self.last_change = identity, now
            if now-self.last_change > STALE_SECONDS:
                self.hide(); return
            self.update_model(frame)
            if frame == self.last_frame and self.window.isVisible():
                return
            self.last_frame = frame
            # Sequence is transport metadata. Removing it lets the SDK reuse
            # its texture when positions/text did not change between packets.
            display_frame={key:value for key,value in frame.items() if key!='sequence'}
            # XWayland geometry is physical, Qt screen geometry is logical.
            screen = min(self.app.screens(), key=lambda s: abs(s.geometry().center().x()*s.devicePixelRatio()-frame['x']-frame['clientWidth']/2)
                         +abs(s.geometry().center().y()*s.devicePixelRatio()-frame['y']-frame['clientHeight']/2))
            ratio = screen.devicePixelRatio()
            if self.window.screen() != screen:
                self.window.setScreen(screen)
            if self.screen_surface:
                bounds=screen.geometry()
                display_frame=dict(display_frame,viewport=dict(
                    x=frame['x']/ratio-bounds.x(),y=frame['y']/ratio-bounds.y(),
                    width=frame['clientWidth']/ratio,height=frame['clientHeight']/ratio))
                self.object.update(width=float(bounds.width()),height=float(bounds.height()),
                                   customProperties={'frame':display_frame})
                if not self.window.isVisible() or self.window.windowState()!=Qt.WindowState.WindowFullScreen:
                    self.window.showFullScreen()
            else:
                geometry = QRect(round(frame['x']/ratio), round(frame['y']/ratio),
                                 max(1,round(frame['clientWidth']/ratio)), max(1,round(frame['clientHeight']/ratio)))
                if self.window.geometry() != geometry:
                    self.window.setGeometry(geometry)
                self.object.update(width=float(geometry.width()), height=float(geometry.height()),
                                   customProperties={'frame':display_frame})
                if not self.window.isVisible():
                    self.window.show()
            self.pending_ack = frame
            self.window.update()
        except (OSError, ValueError, KeyError, TypeError) as exc:
            self.hide()
            # Log the failure without entity names / frame contents.
            if getattr(self,'last_error',None) != str(exc):
                print('Local renderer:',type(exc).__name__,str(exc),file=sys.stderr,flush=True)
                self.last_error = str(exc)

    def acknowledge(self):
        if self.pending_ack is not None and self.window.isVisible() and self.policy_ok:
            try:
                write_ack(self.directory/'ack.bin', self.pending_ack)
            except OSError as exc:
                print('ACK write failed:',exc,file=sys.stderr,flush=True)
            self.pending_ack = None

    def close(self):
        if self.closed: return
        self.closed = True
        self.timer.stop(); self.hide(); self.renderer.close()
        self.window.close(); self.window.deleteLater()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory',type=Path,default=Path.home()/'.local/share/Steam/steamapps/compatdata/440/pfx/drive_c/users/steamuser/AppData/Local/SixBox/local-render')
    args=parser.parse_args()
    os.umask(0o077)
    args.directory.mkdir(parents=True,exist_ok=True,mode=0o700); args.directory.chmod(0o700)
    lock=(args.directory/'renderer.lock').open('a')
    try: fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except BlockingIOError: raise SystemExit('Local renderer already running')
    app=QGuiApplication([sys.argv[0],'-name',APP_ID])
    app.setApplicationName(APP_ID); app.setDesktopFileName(APP_ID); app.setQuitOnLastWindowClosed(False)
    qmlRegisterType(FrameCanvas,'LocalOverlay',1,0,'FrameCanvas')
    if app.platformName() != 'xcb':
        raise SystemExit('Managed XWayland window required for native KWin capture rules')
    if not policy_configured():
        print('Cloakd native capture policy is missing; overlay stays hidden.',file=sys.stderr,flush=True)
    overlay=Overlay(app,args.directory)
    signal.signal(signal.SIGTERM,lambda *_: app.quit())
    signal.signal(signal.SIGINT,lambda *_: app.quit())
    return app.exec()

if __name__=='__main__':
    raise SystemExit(main())
