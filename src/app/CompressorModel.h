#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "Compressor.h"

namespace dreamdsp {

// QML-facing wrapper around the compressor's parameters.
//
// The DSP itself is not in the audio path yet -- it has no host. What this
// does give, today, is an honest design surface: the transfer curve shown in
// the UI is computed by the same gain computer that will process the audio,
// not by a drawing approximation of it.
class CompressorModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Owned by AppController, not by the QML engine: it is the only object
    // with a process-long lifetime and with save/restore, and the parameter
    // publisher has to see every change wherever it comes from.
    QML_UNCREATABLE("owned by AppController")

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY paramsChanged)
    Q_PROPERTY(double threshold READ threshold WRITE setThreshold NOTIFY paramsChanged)
    Q_PROPERTY(double ratio READ ratio WRITE setRatio NOTIFY paramsChanged)
    Q_PROPERTY(double knee READ knee WRITE setKnee NOTIFY paramsChanged)
    Q_PROPERTY(double attack READ attack WRITE setAttack NOTIFY paramsChanged)
    Q_PROPERTY(double release READ release WRITE setRelease NOTIFY paramsChanged)
    Q_PROPERTY(double makeup READ makeup WRITE setMakeup NOTIFY paramsChanged)

    Q_PROPERTY(bool autoKnee READ autoKnee WRITE setAutoKnee NOTIFY paramsChanged)
    Q_PROPERTY(bool autoAttack READ autoAttack WRITE setAutoAttack NOTIFY paramsChanged)
    Q_PROPERTY(bool autoRelease READ autoRelease WRITE setAutoRelease NOTIFY paramsChanged)
    Q_PROPERTY(bool autoMakeup READ autoMakeup WRITE setAutoMakeup NOTIFY paramsChanged)

    // Resolved values when the corresponding auto flag is on, for display.
    Q_PROPERTY(double effectiveKnee READ effectiveKnee NOTIFY paramsChanged)
    Q_PROPERTY(double effectiveMakeup READ effectiveMakeup NOTIFY paramsChanged)

public:
    explicit CompressorModel(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    void setEnabled(bool v);

    double threshold() const { return m_p.thresholdDb; }
    void setThreshold(double v);
    double ratio() const { return m_p.ratio; }
    void setRatio(double v);
    double knee() const { return m_p.kneeDb; }
    void setKnee(double v);
    double attack() const { return m_p.attackMs; }
    void setAttack(double v);
    double release() const { return m_p.releaseMs; }
    void setRelease(double v);
    double makeup() const { return m_p.makeupDb; }
    void setMakeup(double v);

    bool autoKnee() const { return m_p.autoKnee; }
    void setAutoKnee(bool v);
    bool autoAttack() const { return m_p.autoAttack; }
    void setAutoAttack(bool v);
    bool autoRelease() const { return m_p.autoRelease; }
    void setAutoRelease(bool v);
    bool autoMakeup() const { return m_p.autoMakeup; }
    void setAutoMakeup(bool v);

    double effectiveKnee() const;
    double effectiveMakeup() const;


    // dBFS in -> dBFS out, straight from the real gain computer.
    Q_INVOKABLE double outputFor(double inputDb) const;

    // For handing a copy to a worker thread.
    dsp::Compressor::Params dspParams() const { return m_p; }

    // Bulk restore, for reloading a saved session. One signal rather than a
    // dozen, so nothing downstream sees a half-restored state.
    void restore(const dsp::Compressor::Params &p, bool enabled);

signals:
    void paramsChanged();

private:
    void apply();

    dsp::Compressor m_comp;
    dsp::Compressor::Params m_p;
    bool m_enabled = false;
};

} // namespace dreamdsp
