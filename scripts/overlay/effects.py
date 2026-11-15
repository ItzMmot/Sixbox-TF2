"""Box-only effects rendered on the private overlay's transparent surface."""
from functools import lru_cache
import math
from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPainterPath, QPainterPathStroker, QPen


def rgba(value):
    return QColor(value & 255, value >> 8 & 255, value >> 16 & 255, value >> 24 & 255)


@lru_cache(maxsize=512)
def geometry(width, height, style):
    rect = QRectF(0, 0, width, height)
    path, interior = QPainterPath(), QPainterPath()
    if style == 4:
        path.addRoundedRect(rect, 5, 5)
        interior.addRoundedRect(rect, 5, 5)
    else:
        interior.addRect(rect)
        if style in (1, 2):
            for x,y,a,b in ((0,0,width*.25,0),(0,0,0,height*.25),
                            (width,0,width*.75,0),(width,0,width,height*.25),
                            (0,height,width*.25,height),(0,height,0,height*.75),
                            (width,height,width*.75,height),(width,height,width,height*.75)):
                path.moveTo(x,y);path.lineTo(a,b)
        else:
            path.addRect(rect)
    return path, interior


@lru_cache(maxsize=512)
def stencil_ring(width, height, style, thickness):
    path, interior = geometry(width, height, style)
    stroke = QPainterPathStroker()
    stroke.setWidth(1.2 + thickness*2)
    stroke.setCapStyle(Qt.PenCapStyle.RoundCap)
    stroke.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
    # Cut out the box interior: stencil never fills the target or bridges the
    # open middle sections of corner boxes.
    return stroke.createStroke(path).subtracted(interior)


def _paint_halo(image, path, padding, radius, strength, rgb):
    image.fill(0)
    painter=QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.translate(padding,padding)
    painter.setBrush(Qt.BrushStyle.NoBrush)
    color=rgba(rgb|0xff000000)
    layers=min(16,max(6,math.ceil(radius)))
    for layer in range(layers,0,-1):
        distance=(layer-.5)/layers
        weight=(math.exp(-3*distance*distance)-math.exp(-3))*.24*12/layers
        color.setAlphaF(min(1.,strength*weight))
        pen=QPen(color,1.2+2*radius*distance)
        pen.setCapStyle(Qt.PenCapStyle.RoundCap);pen.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
        painter.setPen(pen);painter.drawPath(path)
    painter.end()
    return image


@lru_cache(maxsize=96)
def _box_halo(width,height,style,radius,strength,rgb):
    pad=math.ceil(radius+2)
    image=QImage(math.ceil(width)+pad*2,math.ceil(height)+pad*2,QImage.Format.Format_ARGB32_Premultiplied)
    return _paint_halo(image,geometry(width,height,style)[0],pad,radius,strength,rgb)


@lru_cache(maxsize=64)
def _line_halo(radius,strength,rgb):
    pad=math.ceil(radius+2)
    image=QImage(64+pad*2,1+pad*2,QImage.Format.Format_ARGB32_Premultiplied)
    path=QPainterPath();path.moveTo(0,0);path.lineTo(64,0)
    return _paint_halo(image,path,pad,radius,strength,rgb)


def draw_halo(painter,width,height,style,color,radius,strength):
    # Images stay small regardless of target size; stretch only uniform edge
    # regions. Alpha is applied on composition, not part of the cache key.
    pad=math.ceil(radius+2)
    rgb=color.red() | (color.green()<<8) | (color.blue()<<16)
    painter.save();painter.setOpacity(painter.opacity()*color.alphaF())
    # Only uniform edge strips are stretched. Bilinear image transforms add
    # substantial CPU cost here without improving their constant-axis samples.
    painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, False)
    if style in (1,2):
        tile=_line_halo(radius,strength,rgb)
        segments=((0,0,width*.25,0),(0,0,0,height*.25),
                  (width,0,width*.75,0),(width,0,width,height*.25),
                  (0,height,width*.25,height),(0,height,0,height*.75),
                  (width,height,width*.75,height),(width,height,width,height*.75))
        for x,y,a,b in segments:
            vertical=x==a
            start_x,start_y=min(x,a),min(y,b)
            length=abs(b-y) if vertical else abs(a-x)
            painter.save();painter.translate(start_x,start_y)
            if vertical:painter.rotate(90)
            for dest,source in (
                (QRectF(-pad,-pad,pad,tile.height()),QRectF(0,0,pad,tile.height())),
                (QRectF(0,-pad,length,tile.height()),QRectF(pad+32,0,1,tile.height())),
                (QRectF(length,-pad,pad,tile.height()),QRectF(pad+64,0,pad,tile.height()))):
                painter.drawImage(dest,tile,source)
            painter.restore()
    else:
        cut=math.ceil(radius+6)
        canonical=max(64,math.ceil(radius*4+32))
        wide,tall=width>cut*2,height>cut*2
        tw,th=(canonical if wide else width),(canonical if tall else height)
        tile=_box_halo(tw,th,style,radius,strength,rgb)
        sx=[0,pad+cut,tile.width()-pad-cut,tile.width()] if wide else [0,tile.width()]
        sy=[0,pad+cut,tile.height()-pad-cut,tile.height()] if tall else [0,tile.height()]
        dx=[-pad,cut,width-cut,width+pad] if wide else [-pad,width+pad]
        dy=[-pad,cut,height-cut,height+pad] if tall else [-pad,height+pad]
        for row in range(len(sy)-1):
            for col in range(len(sx)-1):
                if wide and tall and row==1 and col==1:continue
                painter.drawImage(QRectF(dx[col],dy[row],dx[col+1]-dx[col],dy[row+1]-dy[row]),tile,
                    QRectF(sx[col],sy[row],sx[col+1]-sx[col],sy[row+1]-sy[row]))
    painter.restore()


@lru_cache(maxsize=96)
def _closed_surface(width,height,style,flags,radius,strength,thickness,tint,rgb):
    halo=radius if flags & 1 and strength>0 else 0
    outline=thickness if flags & 2 else 0
    pad=math.ceil(max(halo,outline,2)+2)
    image=QImage(math.ceil(width)+pad*2,math.ceil(height)+pad*2,QImage.Format.Format_ARGB32_Premultiplied)
    image.fill(0);painter=QPainter(image);painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    if halo:
        halo_pad=math.ceil(radius+2)
        painter.drawImage(pad-halo_pad,pad-halo_pad,_box_halo(width,height,style,radius,strength,rgb))
    painter.translate(pad,pad)
    path,_=geometry(width,height,style)
    if flags & 2:painter.fillPath(stencil_ring(width,height,style,thickness),rgba(tint))
    painter.setBrush(Qt.BrushStyle.NoBrush)
    for pen in (QPen(QColor(0,0,0),2.2 if flags & 2 else 3.),QPen(rgba(rgb|0xff000000),1.2)):
        pen.setJoinStyle(Qt.PenJoinStyle.RoundJoin);painter.setPen(pen);painter.drawPath(path)
    painter.end()
    return image,pad


def draw_closed_surface(painter,width,height,style,color,effects):
    flags=effects['flags']
    radius,strength=effects['glowRadius'],effects['glowStrength']
    thickness=effects['stencilThickness']
    reach=max(radius if flags & 1 and strength>0 else 0,thickness if flags & 2 else 0,2)
    cut=math.ceil(reach+6);canonical=max(64,cut*2+32)
    wide,tall=width>cut*2,height>cut*2
    tw,th=(canonical if wide else width),(canonical if tall else height)
    rgb=color.red() | (color.green()<<8) | (color.blue()<<16)
    tile,pad=_closed_surface(tw,th,style,flags,radius,strength,thickness,effects['stencilColor'],rgb)
    sx=[0,pad+cut,tile.width()-pad-cut,tile.width()] if wide else [0,tile.width()]
    sy=[0,pad+cut,tile.height()-pad-cut,tile.height()] if tall else [0,tile.height()]
    dx=[-pad,cut,width-cut,width+pad] if wide else [-pad,width+pad]
    dy=[-pad,cut,height-cut,height+pad] if tall else [-pad,height+pad]
    painter.save();painter.setOpacity(painter.opacity()*color.alphaF())
    painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform,False)
    for row in range(len(sy)-1):
        for col in range(len(sx)-1):
            if wide and tall and row==1 and col==1:continue
            painter.drawImage(QRectF(dx[col],dy[row],dx[col+1]-dx[col],dy[row+1]-dy[row]),tile,
                QRectF(sx[col],sy[row],sx[col+1]-sx[col],sy[row+1]-sy[row]))
    painter.restore()


def draw_box(painter, command, effects):
    color = rgba(command['color'])
    if not color.alpha():
        return
    width, height, style = command['width'], command['height'], command['style']
    path, _ = geometry(width, height, style)
    flags = effects.get('flags', 0)
    painter.save()
    painter.translate(command['x'], command['y'])
    painter.setBrush(Qt.BrushStyle.NoBrush)
    if style not in (1,2) and flags & 3:
        draw_closed_surface(painter,width,height,style,color,effects)
        painter.restore()
        return
    if flags & 1 and effects['glowStrength'] > 0:
        draw_halo(painter,width,height,style,color,effects['glowRadius'],effects['glowStrength'])
    if flags & 2:
        stencil = rgba(effects['stencilColor'])
        stencil.setAlphaF(stencil.alphaF()*color.alphaF())
        painter.fillPath(stencil_ring(width,height,style,effects['stencilThickness']), stencil)
    # Keep the original colored core readable on top of both effects.
    for pen in (QPen(QColor(0,0,0,color.alpha()),2.2 if flags & 2 else 3.), QPen(color,1.2)):
        pen.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
        painter.setPen(pen);painter.drawPath(path)
    painter.restore()
