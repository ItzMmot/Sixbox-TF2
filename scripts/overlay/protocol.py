"""Bounded, versioned local-only transport. No network endpoints."""
import math
import os
import struct
import time
from pathlib import Path

HEADER = struct.Struct('<8sQQiiiiiiII')
HEADER_V2 = struct.Struct('<8sQQiiiiiiIIIfffI')
RECORD = struct.Struct('<IIfffffII192s')
ACK = struct.Struct('<8sQQI')
MAX_RECORDS = 512
MAX_BYTES = HEADER_V2.size + MAX_RECORDS * RECORD.size
STALE_SECONDS = .25


def decode(data):
    if len(data) < HEADER.size or len(data) > MAX_BYTES:
        raise ValueError('Invalid frame length')
    magic, sequence, session, width, height, x, y, cw, ch, mask, count = HEADER.unpack_from(data)
    if magic not in (b'SXOV0001', b'SXOV0002') or not sequence or not session or mask & ~7 or count > MAX_RECORDS:
        raise ValueError('Invalid frame header')
    header_size = HEADER.size
    effects = dict(flags=0, glowStrength=.85, glowRadius=8., stencilThickness=2., stencilColor=0xdcffffff)
    if magic == b'SXOV0002':
        header_size = HEADER_V2.size
        if len(data) < header_size:
            raise ValueError('Truncated effects header')
        flags, strength, radius, thickness, color = HEADER_V2.unpack_from(data)[-5:]
        if flags & ~3 or not all(math.isfinite(v) for v in (strength, radius, thickness)) or not (0 <= strength <= 2 and 1 <= radius <= 24 and 1 <= thickness <= 8):
            raise ValueError('Invalid box effects')
        effects = dict(flags=flags, glowStrength=strength, glowRadius=radius, stencilThickness=thickness, stencilColor=color)
    if len(data) != header_size + count * RECORD.size:
        raise ValueError('Truncated frame')
    if mask and not (1 <= width <= 32768 and 1 <= height <= 32768 and 1 <= cw <= 32768 and 1 <= ch <= 32768):
        raise ValueError('Invalid viewport')
    if abs(x) > 100000 or abs(y) > 100000:
        raise ValueError('Invalid window position')
    records = []
    for offset in range(header_size, len(data), RECORD.size):
        kind, style, rx, ry, rw, rh, size, color, bg, text = RECORD.unpack_from(data, offset)
        if kind not in (1, 2) or not mask & kind or not all(math.isfinite(v) and abs(v) < 100000 for v in (rx, ry, rw, rh, size)):
            raise ValueError('Invalid draw command')
        if kind == 1 and (rw <= 0 or rh <= 0) or kind == 2 and not 1 <= size <= 512:
            raise ValueError('Invalid command size')
        records.append(dict(kind=kind, style=style, x=rx, y=ry, width=rw, height=rh, size=size,
                            color=color, background=bg, text=text.split(b'\0', 1)[0].decode('utf-8', errors='replace')))
    return dict(sequence=sequence, session=session, width=width, height=height, x=x, y=y,
                clientWidth=cw, clientHeight=ch, mask=mask, records=records, effects=effects)


def read_frame(path):
    with Path(path).open('rb') as file:
        age = time.time() - os.fstat(file.fileno()).st_mtime
        if not 0 <= age < STALE_SECONDS:
            return None
        return decode(file.read(MAX_BYTES + 1))


def atomic_write(path, data):
    path = Path(path)
    temp = path.with_suffix('.new')
    fd = os.open(temp, os.O_CREAT | os.O_TRUNC | os.O_WRONLY, 0o600)
    with os.fdopen(fd, 'wb') as file:
        file.write(data)
    os.replace(temp, path)


def write_ack(path, frame):
    atomic_write(path, ACK.pack(b'SXACK001', frame['session'], frame['sequence'], frame['mask']))
