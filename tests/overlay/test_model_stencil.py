import os
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'scripts/overlay'),str(ROOT/'tests/overlay')]
os.environ['QT_QPA_PLATFORM']='offscreen';os.environ['QT_QUICK_BACKEND']='software'
from model_image import HEADER,decode,Reader,FILETIME_EPOCH
from protocol import HEADER_V2,atomic_write


def native_packet(sequence=7):
    raw=(ROOT/'tests/overlay/fixtures/model-native.bin').read_bytes()
    values=list(HEADER.unpack_from(raw));values[2]=sequence;values[3]=int((time.time()+FILETIME_EPOCH)*10000000)
    return HEADER.pack(*values)+raw[HEADER.size:]


def frame(sequence=1,mask=4):
    return HEADER_V2.pack(b'SXOV0002',sequence,12345,80,120,100,100,320,480,mask,0,0,.85,8.,2.,0xdcffffff)


class ModelProtocolTest(unittest.TestCase):
    def test_native_encoder_cropped_contour_and_alpha(self):
        self.assertEqual(HEADER.size,60)
        result=decode(native_packet(),12345,80,120)
        self.assertEqual((result.x,result.y,result.image.width(),result.image.height()),(20,10,40,95))
        self.assertEqual(result.image.pixelColor(12,0).alpha(),255)
        self.assertEqual(result.image.pixelColor(0,0).alpha(),0)
        self.assertEqual(result.image.pixelColor(20,40).alpha(),0)
        shoulder=result.image.pixelColor(1,22)
        self.assertEqual(shoulder.alpha(),128)
        self.assertEqual(shoulder.red(),255)
        self.assertAlmostEqual(shoulder.green(),80,delta=1)

    def test_wrong_process_resolution_and_expiration(self):
        data=native_packet()
        self.assertIsNone(decode(data,999,80,120))
        self.assertIsNone(decode(data,12345,81,120))
        values=list(HEADER.unpack_from(data));values[3]-=10000000
        self.assertIsNone(decode(HEADER.pack(*values)+data[HEADER.size:],12345,80,120))

    def test_malformed_crop_png_and_empty(self):
        data=native_packet()
        with self.assertRaises(ValueError):decode(data[:-1],12345,80,120)
        with self.assertRaises(ValueError):decode(data[:HEADER.size]+b'X'*len(data[HEADER.size:]),12345,80,120)
        values=list(HEADER.unpack_from(data));values[8]=100
        with self.assertRaises(ValueError):decode(HEADER.pack(*values)+data[HEADER.size:],12345,80,120)
        values=list(HEADER.unpack_from(data));values[6:]=[0,0,0,0,0]
        self.assertTrue(decode(HEADER.pack(*values),12345,80,120).image.isNull())

    def test_reader_ignores_missing_and_regressing_images(self):
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'stencil.bin';reader=Reader()
            self.assertIsNone(reader.read(path,12345,80,120))
            atomic_write(path,native_packet(10));self.assertEqual(reader.read(path,12345,80,120).sequence,10)
            atomic_write(path,native_packet(9));self.assertIsNone(reader.read(path,12345,80,120))
            os.utime(path,(time.time()-1,time.time()-1));self.assertIsNone(reader.read(path,12345,80,120))


class ModelRenderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from render import QGuiApplication,qmlRegisterType,FrameCanvas
        cls.app=QGuiApplication.instance() or QGuiApplication([])
        qmlRegisterType(FrameCanvas,'LocalOverlay',1,0,'FrameCanvas')

    def test_actual_contour_without_boxes_and_stale_clear(self):
        from render import Overlay
        with tempfile.TemporaryDirectory() as d:
            path=Path(d);overlay=Overlay(self.app,path,require_capture=False)
            try:
                atomic_write(path/'frame.bin',frame())
                atomic_write(path/'stencil.bin',native_packet())
                overlay.tick()
                for _ in range(10):self.app.processEvents();time.sleep(.005)
                self.assertTrue(overlay.window.isVisible())
                image=overlay.window.grabWindow();self.assertFalse(image.isNull())
                self.assertGreater(image.pixelColor(130,42).alpha(),0)
                self.assertEqual(image.pixelColor(82,42).alpha(),0)
                self.assertEqual(image.pixelColor(162,202).alpha(),0)
                image.save(str(ROOT/'tests/overlay/preview-model-stencil.png'))
                # Image expiry clears just the silhouette, with the active
                # native window mapped (no camera-return window-open delay).
                os.utime(path/'stencil.bin',(time.time()-1,time.time()-1))
                atomic_write(path/'frame.bin',frame(2));overlay.tick()
                for _ in range(6):self.app.processEvents();time.sleep(.005)
                self.assertTrue(overlay.window.isVisible())
                self.assertEqual(overlay.window.grabWindow().pixelColor(130,42).alpha(),0)
                atomic_write(path/'stencil.bin',native_packet(11))
                atomic_write(path/'frame.bin',frame(3));overlay.tick()
                for _ in range(6):self.app.processEvents();time.sleep(.005)
                self.assertGreater(overlay.window.grabWindow().pixelColor(130,42).alpha(),0)
                overlay.policy_ok=False;overlay.last_policy=time.monotonic();overlay.tick()
                self.assertFalse(overlay.window.isVisible());self.assertIsNone(overlay.canvas._model)
            finally:overlay.close();self.app.processEvents()

if __name__=='__main__':unittest.main(verbosity=2)
