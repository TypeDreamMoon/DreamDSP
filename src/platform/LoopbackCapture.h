#pragma once

#include <QAtomicInt>
#include <QString>
#include <QThread>
#include <QVector>

namespace dreamdsp {

// Real-time spectrum of what is actually being played, via WASAPI loopback.
//
// Qt Multimedia does not expose loopback at all, so this opens a capture
// stream on a *render* endpoint with AUDCLNT_STREAMFLAGS_LOOPBACK. That is
// shared-mode only: a device held in exclusive mode yields nothing, the same
// blind spot the level meter has.
//
// Capture, windowing and FFT all happen on this thread; only the finished band
// values cross to the GUI, at roughly 30 Hz.
class LoopbackCapture : public QThread
{
    Q_OBJECT

public:
    // Log-spaced display bands between 20 Hz and 20 kHz.
    static constexpr int kBands = 96;

    explicit LoopbackCapture(QObject *parent = nullptr);
    ~LoopbackCapture() override;

    // Empty id follows the default render endpoint.
    void startCapture(const QString &endpointId);
    void stopCapture();

signals:
    // Magnitudes in dBFS, one per band, already floored at -90.
    void spectrumReady(const QVector<float> &bandsDb);
    void failed(const QString &reason);

protected:
    void run() override;

private:
    QString m_endpointId;
    QAtomicInt m_stop{ 0 };
};

} // namespace dreamdsp
