#pragma once
#include <QQuickItem>
#include <QColor>
#include <QtQml/qqmlregistration.h>

class NativeGraph : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal phase READ phase WRITE setPhase NOTIFY changed)
    Q_PROPERTY(qreal amplitude READ amplitude WRITE setAmplitude NOTIFY changed)
    Q_PROPERTY(int samples READ samples WRITE setSamples NOTIFY changed)
    Q_PROPERTY(QColor tint READ tint WRITE setTint NOTIFY changed)
public:
    explicit NativeGraph(QQuickItem *parent = nullptr);
    qreal phase() const { return m_phase; }
    qreal amplitude() const { return m_amplitude; }
    int samples() const { return m_samples; }
    QColor tint() const { return m_tint; }
    void setPhase(qreal v);
    void setAmplitude(qreal v);
    void setSamples(int v);
    void setTint(const QColor &v);
signals:
    void changed();
protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
private:
    qreal m_phase = 0, m_amplitude = .7;
    int m_samples = 256;
    QColor m_tint = QColor("#8fc8b5");
};
