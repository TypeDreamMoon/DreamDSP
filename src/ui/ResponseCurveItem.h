#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

#include "app/EqBandModel.h"

namespace dreamdsp {

// The predicted magnitude response of the current filter chain.
//
// Drawn with QPainter rather than Canvas: this repaints on every slider frame,
// and the JS round-trip of Canvas shows up as visible lag with 10+ bands.
class ResponseCurveItem : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(dreamdsp::EqBandModel *bands READ bands WRITE setBands NOTIFY bandsChanged)
    Q_PROPERTY(double preamp READ preamp WRITE setPreamp NOTIFY preampChanged)
    Q_PROPERTY(double rangeDb READ rangeDb WRITE setRangeDb NOTIFY rangeDbChanged)
    Q_PROPERTY(QColor curveColor READ curveColor WRITE setCurveColor NOTIFY curveColorChanged)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY gridColorChanged)
    Q_PROPERTY(QColor labelColor READ labelColor WRITE setLabelColor NOTIFY labelColorChanged)
    Q_PROPERTY(bool filled READ filled WRITE setFilled NOTIFY filledChanged)
    Q_PROPERTY(QVector<float> spectrum READ spectrum WRITE setSpectrum NOTIFY spectrumChanged)
    Q_PROPERTY(QColor spectrumColor READ spectrumColor WRITE setSpectrumColor NOTIFY spectrumColorChanged)

    // The rate the filters are designed at. Not cosmetic: the bilinear
    // transform warps a shelf near Nyquist by a couple of dB, so a curve drawn
    // at a fixed 48 kHz would stop being the curve the endpoint is running as
    // soon as the endpoint is not at 48 kHz. Zero means "use the default".
    Q_PROPERTY(int sampleRate READ sampleRate WRITE setSampleRate NOTIFY sampleRateChanged)

public:
    explicit ResponseCurveItem(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    EqBandModel *bands() const { return m_bands; }
    void setBands(EqBandModel *m);
    double preamp() const { return m_preamp; }
    void setPreamp(double v);
    double rangeDb() const { return m_rangeDb; }
    void setRangeDb(double v);
    QColor curveColor() const { return m_curveColor; }
    void setCurveColor(const QColor &c);
    QColor gridColor() const { return m_gridColor; }
    void setGridColor(const QColor &c);
    QColor labelColor() const { return m_labelColor; }
    void setLabelColor(const QColor &c);
    bool filled() const { return m_filled; }
    void setFilled(bool v);
    QVector<float> spectrum() const { return m_spectrum; }
    void setSpectrum(const QVector<float> &s);
    QColor spectrumColor() const { return m_spectrumColor; }
    void setSpectrumColor(const QColor &c);
    int sampleRate() const { return m_sampleRate; }
    void setSampleRate(int hz);

signals:
    void bandsChanged();
    void preampChanged();
    void rangeDbChanged();
    void curveColorChanged();
    void gridColorChanged();
    void labelColorChanged();
    void filledChanged();
    void spectrumChanged();
    void spectrumColorChanged();
    void sampleRateChanged();

private:
    EqBandModel *m_bands = nullptr;
    double m_preamp = 0.0;
    double m_rangeDb = 15.0;
    QColor m_curveColor = QColor(0x17, 0x7d, 0xdc);
    QColor m_gridColor = QColor(255, 255, 255, 28);
    QColor m_labelColor = QColor(255, 255, 255, 110);
    bool m_filled = true;
    int m_sampleRate = 0;

    QVector<float> m_spectrum;      // dBFS per log-spaced band
    QVector<float> m_decay;         // slow-falling envelope, for readability
    QColor m_spectrumColor = QColor(0x7a, 0xd1, 0xff);
};

} // namespace dreamdsp
