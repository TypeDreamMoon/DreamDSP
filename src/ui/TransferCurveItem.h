#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

namespace dreamdsp {

class CompressorModel;

// The compressor's static input->output curve.
//
// Values come from the real gain computer, not from a drawing approximation of
// it -- what is plotted here is exactly what the DSP will do.
class TransferCurveItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(dreamdsp::CompressorModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QColor curveColor READ curveColor WRITE setCurveColor NOTIFY curveColorChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY labelColorChanged)

public:
    explicit TransferCurveItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    CompressorModel *model() const { return m_model; }
    void setModel(CompressorModel *m);
    QColor curveColor() const { return m_curveColor; }
    void setCurveColor(const QColor &c);
    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor &c);
    QColor labelColor() const { return m_labelColor; }
    void setLabelColor(const QColor &c);

signals:
    void modelChanged();
    void curveColorChanged();
    void gridColorChanged();
    void labelColorChanged();

private:
    CompressorModel *m_model = nullptr;
    QColor m_curveColor = QColor(0x3d, 0x7e, 0xff);
    QColor m_gridColor = QColor(255, 255, 255, 26);
    QColor m_labelColor = QColor(255, 255, 255, 110);
};

} // namespace dreamdsp
