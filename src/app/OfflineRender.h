#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

// Full definitions, not forward declarations: moc registers the pointer types
// used in Q_PROPERTY as metatypes, and that requires complete types.
#include "app/CompressorModel.h"
#include "app/EffectsModel.h"
#include "app/EqBandModel.h"
#include "app/ReverbModel.h"

namespace dreamdsp {

// Runs the effects over a wav file.
//
// The DSP has no host yet -- nothing routes system audio through it. Rather
// than presenting controls that silently do nothing, this makes the settings
// actually audible: drop a file in, get a processed one out, play it in
// whatever you like. It is also how a setting gets auditioned honestly, on
// real material, without an audio graph in the way.
class OfflineRender : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(dreamdsp::CompressorModel *compressor MEMBER m_compressor NOTIFY sourcesChanged)
    Q_PROPERTY(dreamdsp::ReverbModel *reverb MEMBER m_reverb NOTIFY sourcesChanged)
    Q_PROPERTY(dreamdsp::EffectsModel *effects MEMBER m_effects NOTIFY sourcesChanged)
    // The equalizer too, or the file that comes out is not what the endpoint
    // would have played -- which is the one thing this class exists to avoid.
    Q_PROPERTY(dreamdsp::EqBandModel *bands MEMBER m_bands NOTIFY sourcesChanged)
    Q_PROPERTY(double preamp MEMBER m_preamp NOTIFY sourcesChanged)
    Q_PROPERTY(bool eqEnabled MEMBER m_eqEnabled NOTIFY sourcesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lastOutput READ lastOutput NOTIFY statusChanged)
    Q_PROPERTY(bool failed READ failed NOTIFY statusChanged)

public:
    explicit OfflineRender(QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString lastOutput() const { return m_lastOutput; }
    bool failed() const { return m_failed; }

    // Accepts a local file URL, as delivered by a QML DropArea.
    Q_INVOKABLE void renderUrl(const QUrl &url);
    Q_INVOKABLE void revealOutput() const;

signals:
    void sourcesChanged();
    void busyChanged();
    void statusChanged();

private:
    void setStatus(const QString &s, bool failed);
    void setBusy(bool b);

    CompressorModel *m_compressor = nullptr;
    ReverbModel *m_reverb = nullptr;
    EffectsModel *m_effects = nullptr;
    EqBandModel *m_bands = nullptr;
    double m_preamp = 0.0;
    bool m_eqEnabled = true;

    bool m_busy = false;
    bool m_failed = false;
    QString m_status;
    QString m_lastOutput;
};

} // namespace dreamdsp
