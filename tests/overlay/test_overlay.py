import configparser
import os
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/overlay'))
from protocol import *
from capture import policy_configured,APP_ID


def frame_data(mask=3, sequence=1, records=None):
    if records is None:
        records=[]
        if mask&1: records.append(RECORD.pack(1,1,200,120,100,230,0,0xff59b9ff,0,b''))
        if mask&2: records.append(RECORD.pack(2,0,250,110,0,0,20,0xffeeeeee,0xb0202020,'SixBox / Тест'.encode()))
    return HEADER.pack(b'SXOV0001',sequence,12345,640,480,100,100,640,480,mask,len(records))+b''.join(records)


def effects_frame(flags=3, strength=.85, radius=8., thickness=2., color=0xdcffffff, style=0, sequence=1):
    record=RECORD.pack(1,style,200,120,100,230,0,0xff59b9ff,0,b'')
    old=frame_data(1,sequence,records=[record])
    values=HEADER.unpack_from(old)
    return HEADER_V2.pack(b'SXOV0002',*values[1:],flags,strength,radius,thickness,color)+old[HEADER.size:]


class ProtocolTest(unittest.TestCase):
    def test_layout_utf8_and_routes(self):
        self.assertEqual((HEADER.size, RECORD.size, ACK.size),(56,228,28))
        for mask in range(4):
            frame=decode(frame_data(mask))
            self.assertEqual({r['kind'] for r in frame['records']},{flag for flag in (1,2) if flag&mask})
        self.assertIn('Тест',decode(frame_data())['records'][1]['text'])

    def test_rejects_bad_frames(self):
        data=frame_data()
        for bad in (data[:10],data[:-1],data+b'1',b'BADMAGIC'+data[8:],data[:48]+struct.pack('<I',8)+data[52:]):
            with self.assertRaises(ValueError):decode(bad)
        record=RECORD.pack(1,0,float('nan'),0,10,10,0,0,0,b'')
        with self.assertRaises(ValueError):decode(frame_data(records=[record]))
        with self.assertRaises(ValueError):decode(frame_data(records=[RECORD.pack(2,0,0,0,0,0,0,0,0,b'')]))

    def test_staleness_and_ack_permissions(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'frame.bin'; atomic_write(path,frame_data())
            self.assertIsNotNone(read_frame(path))
            write_ack(Path(d)/'ack.bin',decode(frame_data()))
            self.assertEqual(ACK.unpack((Path(d)/'ack.bin').read_bytes()),(b'SXACK001',12345,1,3))
            self.assertEqual((Path(d)/'ack.bin').stat().st_mode&0o777,0o600)
            os.utime(path,(time.time()-1,time.time()-1));self.assertIsNone(read_frame(path))

    def test_effects_protocol_and_legacy_defaults(self):
        self.assertEqual(HEADER_V2.size,76)
        self.assertEqual(decode(frame_data())['effects']['flags'],0)
        frame=decode(effects_frame(flags=3,strength=1.5,radius=16.,thickness=4.))
        self.assertEqual(frame['effects'],dict(flags=3,glowStrength=1.5,glowRadius=16.,stencilThickness=4.,stencilColor=0xdcffffff))
        self.assertEqual(len(frame['records']),1)
        for kwargs in (dict(flags=4),dict(strength=float('nan')),dict(radius=25.),dict(thickness=0.)):
            with self.assertRaises(ValueError):decode(effects_frame(**kwargs))
        with self.assertRaises(ValueError):decode(effects_frame()[:60])

    def test_capture_policy_requires_active_force_rule(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'kwinrulesrc'
            content=f'[General]\nrules=private\n[private]\nwmclass={APP_ID}\nwmclassmatch=1\nwmclasscomplete=false\ntitlematch=0\nexcludefromcapture=true\nexcludefromcapturerule=2\nlayer=overlay\nlayerrule=2\n'
            path.write_text(content);self.assertTrue(policy_configured(path))
            for bad in (content.replace('rules=private','rules=other'),content.replace('capture=true','capture=false'),content.replace('capturerule=2','capturerule=3'),content.replace(APP_ID,'other.app')):
                path.write_text(bad);self.assertFalse(policy_configured(path))


class RenderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        os.environ['QT_QPA_PLATFORM']='offscreen'
        os.environ['QT_QUICK_BACKEND']='software'
        from render import QGuiApplication,qmlRegisterType,FrameCanvas
        cls.app=QGuiApplication.instance() or QGuiApplication([])
        qmlRegisterType(FrameCanvas,'LocalOverlay',1,0,'FrameCanvas')

    def test_real_sdk_render_routes_and_disconnect(self):
        from render import Overlay
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);overlay=Overlay(self.app,path,require_capture=False)
            try:
                for sequence,mask in enumerate((3,1,2),1):
                    atomic_write(path/'frame.bin',frame_data(mask,sequence=sequence))
                    overlay.tick()
                    for _ in range(12): self.app.processEvents();time.sleep(.008)
                    self.assertTrue(overlay.window.isVisible())
                    self.assertFalse(any(overlay.object.errors.values()),overlay.object.errors)
                    image=overlay.window.grabWindow()
                    self.assertFalse(image.isNull())
                    image.save(str(ROOT/'tests/overlay'/f'preview-{mask}.png'))
                    # Pixel inside left edge (box); background otherwise transparent.
                    alpha=image.pixelColor(round(200*image.devicePixelRatio()),round(140*image.devicePixelRatio())).alpha()
                    self.assertEqual(alpha>0,bool(mask&1))
                    self.assertTrue((path/'ack.bin').exists())
                atomic_write(path/'frame.bin',frame_data(3,sequence=1));overlay.tick()
                self.assertFalse(overlay.window.isVisible())
                os.utime(path/'frame.bin',(time.time()-1,time.time()-1));overlay.tick()
                self.assertFalse(overlay.window.isVisible());self.assertFalse((path/'ack.bin').exists())
                atomic_write(path/'frame.bin',frame_data(0,4));overlay.tick();self.assertFalse(overlay.window.isVisible())
            finally:
                overlay.close();overlay.close();self.app.processEvents()

    def test_camera_exit_clears_canvas_without_remapping_window(self):
        from render import Overlay
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);overlay=Overlay(self.app,path,require_capture=False)
            try:
                def publish(data):
                    atomic_write(path/'frame.bin',data);overlay.tick()
                    for _ in range(6):self.app.processEvents();time.sleep(.005)
                publish(frame_data(1,1))
                self.assertTrue(overlay.window.isVisible())
                visibility=[]
                overlay.window.visibleChanged.connect(lambda value:visibility.append(value))
                # A fresh enabled frame with no draw commands means the camera
                # is facing away, rather than the module being disabled.
                publish(frame_data(1,2,records=[]))
                self.assertTrue(overlay.window.isVisible())
                self.assertEqual(overlay.timer.interval(),4)
                empty=overlay.window.grabWindow()
                self.assertEqual(empty.pixelColor(200,140).alpha(),0)
                self.assertEqual(ACK.unpack((path/'ack.bin').read_bytes())[2],2)
                publish(frame_data(1,3))
                visible=overlay.window.grabWindow()
                self.assertGreater(visible.pixelColor(200,140).alpha(),0)
                self.assertEqual(visibility,[])
                self.assertEqual(ACK.unpack((path/'ack.bin').read_bytes())[2],3)
                # Explicit disable still unmaps the window and removes ACK.
                publish(frame_data(0,4,records=[]))
                self.assertFalse(overlay.window.isVisible())
                self.assertFalse((path/'ack.bin').exists())
            finally:overlay.close();self.app.processEvents()

    def test_transport_sequence_reuses_canvas_without_losing_ack(self):
        from render import Overlay
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);overlay=Overlay(self.app,path,require_capture=False)
            try:
                atomic_write(path/'frame.bin',frame_data(1,1));overlay.tick()
                for _ in range(6):self.app.processEvents();time.sleep(.005)
                changes=[];overlay.object.changed.connect(lambda:changes.append(True))
                atomic_write(path/'frame.bin',frame_data(1,2));overlay.tick()
                for _ in range(6):self.app.processEvents();time.sleep(.005)
                self.assertEqual(changes,[])
                self.assertEqual(ACK.unpack((path/'ack.bin').read_bytes())[2],2)
            finally:overlay.close();self.app.processEvents()

    def test_fullscreen_surface_keeps_geometry_and_maps_windowed_client(self):
        from render import Overlay,Qt
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);overlay=Overlay(self.app,path,require_capture=False);overlay.screen_surface=True
            try:
                atomic_write(path/'frame.bin',frame_data(1,1));overlay.tick()
                for _ in range(8):self.app.processEvents();time.sleep(.005)
                geometry=overlay.window.geometry()
                self.assertEqual(overlay.window.windowState(),Qt.WindowState.WindowFullScreen)
                image=overlay.window.grabWindow()
                self.assertGreater(image.pixelColor(300,240).alpha(),0)
                moved=bytearray(frame_data(1,2));struct.pack_into('<ii',moved,32,240,160)
                atomic_write(path/'frame.bin',moved);overlay.tick()
                for _ in range(6):self.app.processEvents();time.sleep(.005)
                self.assertEqual(overlay.window.geometry(),geometry)
                viewport=overlay.canvas.frame['viewport']
                self.assertEqual((viewport['x'],viewport['y']),(240,160))
            finally:overlay.close();self.app.processEvents()

    def test_content_updates_never_move_native_window(self):
        from render import Overlay
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);overlay=Overlay(self.app,path,require_capture=False)
            try:
                atomic_write(path/'frame.bin',frame_data(3,1));overlay.tick();self.app.processEvents()
                changes=[]
                overlay.window.xChanged.connect(lambda value:changes.append(('x',value)))
                overlay.window.yChanged.connect(lambda value:changes.append(('y',value)))
                expected=overlay.window.geometry()
                for sequence in range(2,8):
                    frame=decode(frame_data(3,sequence))
                    overlay.object.update(customProperties={'frame':frame})
                    self.app.processEvents()
                    self.assertEqual(overlay.window.geometry(),expected)
                    atomic_write(path/'frame.bin',frame_data(3,sequence));overlay.tick();self.app.processEvents()
                self.assertEqual(changes,[])
                moved=bytearray(frame_data(3,8))
                # x/y are signed screen coordinates, independent of item x/y.
                struct.pack_into('<ii',moved,32,240,160)
                atomic_write(path/'frame.bin',moved);overlay.tick();self.app.processEvents()
                self.assertEqual((overlay.window.x(),overlay.window.y()),(240,160))
            finally:overlay.close();self.app.processEvents()

    def test_multiple_building_boxes_and_render_scaling(self):
        from render import Overlay
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);overlay=Overlay(self.app,path,require_capture=False)
            try:
                boxes=[RECORD.pack(1,0,x,100,60,60,0,0xff00ff00,0,b'') for x in (100,250,400)]
                data=bytearray(frame_data(1,records=boxes))
                # Render buffer 640x480, physical client 1280x960.
                struct.pack_into('<ii',data,40,1280,960)
                atomic_write(path/'frame.bin',data);overlay.tick()
                for _ in range(10):self.app.processEvents();time.sleep(.005)
                image=overlay.window.grabWindow();self.assertFalse(image.isNull())
                for x in (100,250,400):
                    self.assertGreater(image.pixelColor(x*2,240).alpha(),0)
                self.assertEqual(overlay.window.geometry().width(),1280)
            finally:overlay.close();self.app.processEvents()

    def test_box_glow_and_stencil_pixels(self):
        from render import Overlay
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);overlay=Overlay(self.app,path,require_capture=False)
            sequence=0
            try:
                def image(**kwargs):
                    nonlocal sequence
                    sequence+=1
                    atomic_write(path/'frame.bin',effects_frame(sequence=sequence,**kwargs));overlay.tick()
                    for _ in range(6):self.app.processEvents();time.sleep(.005)
                    result=overlay.window.grabWindow();self.assertFalse(result.isNull())
                    return result
                plain=image(flags=0)
                glow=image(flags=1,strength=1.5,radius=12.)
                self.assertEqual(plain.pixelColor(194,140).alpha(),0)
                self.assertGreater(glow.pixelColor(194,140).alpha(),0)
                self.assertGreater(glow.pixelColor(194,140).red(),glow.pixelColor(194,140).blue())
                small=image(flags=1,strength=.25,radius=12.)
                self.assertLess(small.pixelColor(194,140).alpha(),glow.pixelColor(194,140).alpha())
                no_glow=image(flags=1,strength=0.)
                self.assertEqual(no_glow.pixelColor(194,140).alpha(),0)
                narrow=image(flags=1,radius=2.)
                self.assertEqual(narrow.pixelColor(194,140).alpha(),0)
                stencil=image(flags=2,thickness=4.,color=0xff00ff00)
                edge=stencil.pixelColor(197,140)
                self.assertGreater(edge.alpha(),0);self.assertGreater(edge.green(),edge.red())
                self.assertEqual(stencil.pixelColor(250,230).alpha(),0)
                thin=image(flags=2,thickness=1.)
                self.assertEqual(thin.pixelColor(196,140).alpha(),0)
                corners=image(flags=2,thickness=4.,style=2)
                self.assertEqual(corners.pixelColor(197,235).alpha(),0)
                for style in range(5):
                    combined=image(flags=3,style=style,radius=12.,thickness=3.)
                    self.assertGreater(combined.pixelColor(194,140).alpha(),0)
                    self.assertEqual(combined.pixelColor(250,230).alpha(),0)
                    combined.save(str(ROOT/'tests/overlay'/f'preview-effects-{style}.png'))
            finally:overlay.close();self.app.processEvents()

    def test_missing_capture_policy_stays_hidden(self):
        from render import Overlay
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);atomic_write(path/'frame.bin',frame_data())
            overlay=Overlay(self.app,path,require_capture=True)
            try:
                overlay.policy_ok=False;overlay.last_policy=time.monotonic()
                overlay.tick();self.assertFalse(overlay.window.isVisible());self.assertFalse((path/'ack.bin').exists())
            finally:overlay.close();overlay.close();self.app.processEvents()

if __name__=='__main__':unittest.main(verbosity=2)
