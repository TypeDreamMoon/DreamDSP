#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QVector>
#include <QtQml/qqmlregistration.h>

namespace dreamdsp {

// The magnitude response of an impulse response, on a logarithmic frequency
// axis.
//
// Several hundred files whose names are things like "Jazz Club" or
// "01.Acoustic" are not choosable without this. The curve says in one glance
// what the name does not: whether it is a gentle tilt, a telephone band-pass,
// or something that carves 12 dB out of the presence region.
//
// The values arrive already computed and already referred to their own peak,
// so this only draws.
class ImpulseCurveItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QVector<float> curve READ curve WRITE setCurve NOTIFY curveChanged)
    Q_PROPERTY(double minFreq READ minFreq WRITE setMinFreq NOTIFY rangeChanged)
    Q_PROPERTY(double maxFreq READ maxFreq WRITE setMaxFreq NOTIFY rangeChanged)
    Q_PROPERTY(double floorDb READ floorDb WRITE setFloorDb NOTIFY rangeChanged)
    Q_PROPERTY(QColor curveColor READ curveColor WRITE setCurveColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY colorsChanged)

public:
    explicit ImpulseCurveItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    QVector<float> curve() const { return m_curve; }
    void setCurve(const QVector<float> &c);
    double minFreq() const { return m_minFreq; }
    void setMinFreq(double v);
    double maxFreq() const { return m_maxFreq; }
    void setMaxFreq(double v);
    // How far below the peak the bottom of the plot sits.
    double floorDb() const { return m_floorDb; }
    void setFloorDb(double v);

    QColor curveColor() const { return m_curveColor; }
    void setCurveColor(const QColor &c);
    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor &c);
    QColor labelColor() const { return m_labelColor; }
    void setLabelColor(const QColor &c);

signals:
    void curveChanged();
    void rangeChanged();
    void colorsChanged();

private:
    QVector<float> m_curve;
    double m_minFreq = 20.0;
    double m_maxFreq = 20000.0;
    double m_floorDb = -30.0;

    QColor m_curveColor{ 0x4d, 0x9d, 0xff };
    QColor m_gridColor{ 0x40, 0x40, 0x40 };
    QColor m_labelColor{ 0x80, 0x80, 0x80 };
};

} // namespace dreamdsp
