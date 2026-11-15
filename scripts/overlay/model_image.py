"""Read only the cropped silhouette image produced by the native stencil pass."""
from dataclasses import dataclass
from pathlib import Path
import struct
import time
from PySide6.QtGui import QImage

HEADER = struct.Struct('<8sQQQIIIIIII')
MAX_PNG = 16*1024*1024
MAX_PIXELS = 33177600
FILETIME_EPOCH = 11644473600


@dataclass
class ModelImage:
    sequence: int
    session: int
    captured: int
    width: int
    height: int
    x: int
    y: int
    image: QImage


def decode(data, session, width, height):
    if len(data)<HEADER.size or len(data)>HEADER.size+MAX_PNG:
        raise ValueError('Invalid stencil image length')
    magic,sid,seq,captured,w,h,x,y,cw,ch,size=HEADER.unpack_from(data)
    if magic!=b'SXMSK001' or not seq or not sid:
        raise ValueError('Invalid stencil image header')
    if sid!=session or (w,h)!=(width,height):
        return None
    age=time.time()-(captured/10000000-FILETIME_EPOCH)
    if not 0<=age<.25:
        return None
    if not 0<w<=8192 or not 0<h<=8192 or w*h>MAX_PIXELS or size>MAX_PNG or len(data)!=HEADER.size+size:
        raise ValueError('Invalid stencil image size')
    if not size:
        if any((x,y,cw,ch)):raise ValueError('Invalid empty stencil image')
        return ModelImage(seq,sid,captured,w,h,0,0,QImage())
    if not cw or not ch or x+cw>w or y+ch>h:
        raise ValueError('Invalid stencil image crop')
    png=data[HEADER.size:]
    if len(png)<24 or png[:8]!=b'\x89PNG\r\n\x1a\n' or png[12:16]!=b'IHDR' or struct.unpack_from('>II',png,16)!=(cw,ch):
        raise ValueError('Stencil PNG dimensions do not match its envelope')
    image=QImage.fromData(png,'PNG')
    if image.isNull() or (image.width(),image.height())!=(cw,ch):
        raise ValueError('Invalid stencil PNG')
    return ModelImage(seq,sid,captured,w,h,x,y,image)


class Reader:
    def __init__(self):self.reset()

    def reset(self):
        self.key=None;self.image=None;self.session=None;self.sequence=0

    def read(self,path,session,width,height):
        if session!=self.session:self.reset();self.session=session
        try:
            with Path(path).open('rb') as stream:
                import os
                info=os.fstat(stream.fileno())
                if not 0<=time.time()-info.st_mtime<.25:return None
                key=(info.st_ino,info.st_mtime_ns,info.st_size)
                if key==self.key:
                    if self.image and 0<=time.time()-(self.image.captured/10000000-FILETIME_EPOCH)<.25:return self.image
                    return None
                image=decode(stream.read(HEADER.size+MAX_PNG+1),session,width,height)
        except FileNotFoundError:return None
        if image is not None and image.sequence<self.sequence:return None
        self.key=key;self.image=image
        if image is not None:self.sequence=image.sequence
        return image
