#include "nativegraph.h"
#include <QSGGeometryNode>
#include <QSGFlatColorMaterial>
#include <algorithm>
#include <cmath>

NativeGraph::NativeGraph(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
}
void NativeGraph::setPhase(qreal v) { if (std::isfinite(v) && v != m_phase) { m_phase=v; emit changed(); update(); } }
void NativeGraph::setAmplitude(qreal v) { if (!std::isfinite(v)) return; v=std::clamp(v,0.,1.); if (v!=m_amplitude) {m_amplitude=v; emit changed(); update();} }
void NativeGraph::setSamples(int v) {v=std::clamp(v,16,4096); if(v!=m_samples) {m_samples=v; emit changed(); update();} }
void NativeGraph::setTint(const QColor &v) {if(v!=m_tint) {m_tint=v; emit changed(); update();} }

QSGNode *NativeGraph::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    auto *node=static_cast<QSGGeometryNode *>(old);
    if (!node) {
        node=new QSGGeometryNode;
        auto *geometry=new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGFlatColorMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
    }
    auto *g=node->geometry();
    g->allocate((m_samples-1)*6);
    auto *v=g->vertexDataAsPoint2D();
    const auto point=[this](int i) {
        const double x=double(i)/(m_samples-1);
        const double wave=std::sin(x*12+m_phase)*.65+std::sin(x*27-m_phase*.7)*.25;
        return QPointF(x*width(), height()*(.5+wave*.44*m_amplitude));
    };
    for(int i=0;i<m_samples-1;++i) {
        const auto a=point(i), b=point(i+1), d=b-a;
        const auto length=std::hypot(d.x(),d.y());
        const QPointF n=length>0 ? QPointF(-d.y()/length,d.x()/length) : QPointF(0,1);
        const QPointF vertices[6]={a+n,a-n,b+n,b+n,a-n,b-n};
        for(int j=0;j<6;++j) v[i*6+j].set(vertices[j].x(),vertices[j].y());
    }
    static_cast<QSGFlatColorMaterial *>(node->material())->setColor(m_tint);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
}
