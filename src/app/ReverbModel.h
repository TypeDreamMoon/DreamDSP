#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "Reverb.h"

#include "app/Defaults.h"

namespace dreamdsp {

// QML-facing wrapper around the reverb's parameters.
//
// Like the compressor, the DSP is written and verified but has no host yet;
// this is the design surface for it.
class ReverbModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Owned by AppController, not by the QML engine: it is the only object
    // with a process-long lifetime and with save/restore, and the parameter
    // publisher has to see every change wherever it comes from.
    QML_UNCREATABLE("owned by AppController")

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY paramsChanged)
    Q_PROPERTY(double roomSize READ roomSize WRITE setRoomSize NOTIFY paramsChanged)
    Q_PROPERTY(double damping READ damping WRITE setDamping NOTIFY paramsChanged)
    Q_PROPERTY(double density READ density WRITE setDensity NOTIFY paramsChanged)
    Q_PROPERTY(double bandwidth READ bandwidth WRITE setBandwidth NOTIFY paramsChanged)
    Q_PROPERTY(double preDelay READ preDelay WRITE setPreDelay NOTIFY paramsChanged)
    Q_PROPERTY(double width READ width WRITE setWidth NOTIFY paramsChanged)
    Q_PROPERTY(double wet READ wet WRITE setWet NOTIFY paramsChanged)
    Q_PROPERTY(double dry READ dry WRITE setDry NOTIFY paramsChanged)

public:
    explicit ReverbModel(QObject *parent = nullptr);

    // The value this control would have had out of the box, for the reset
    // button beside it. Empty for a name that is not a property of this model.
    Q_INVOKABLE QVariant defaultOf(const QString &name) const
    {
        return defaultPropertyOf<ReverbModel>(name);
    }


    bool enabled() const { return m_enabled; }
    void setEnabled(bool v);

    double roomSize() const  { return m_p.roomSize; }   void setRoomSize(double v);
    double damping() const   { return m_p.damping; }    void setDamping(double v);
    double density() const   { return m_p.density; }    void setDensity(double v);
    double bandwidth() const { return m_p.bandwidth; }  void setBandwidth(double v);
    double preDelay() const  { return m_p.preDelayMs; } void setPreDelay(double v);
    double width() const     { return m_p.width; }      void setWidth(double v);
    double wet() const       { return m_p.wet; }        void setWet(double v);
    double dry() const       { return m_p.dry; }        void setDry(double v);

    // For handing a copy to a worker thread.
    dsp::Reverb::Params dspParams() const { return m_p; }

    // Bulk restore, for reloading a saved session.
    void restore(const dsp::Reverb::Params &p, bool enabled);

signals:
    void paramsChanged();

private:
    dsp::Reverb::Params m_p;
    bool m_enabled = false;
};

} // namespace dreamdsp
