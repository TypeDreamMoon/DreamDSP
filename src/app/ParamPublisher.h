#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "ParamBlock.h"
#include "StatusBlock.h"

// Full definitions, not forward declarations: these are read directly to build
// a parameter block.
#include "app/CompressorModel.h"
#include "app/EffectsModel.h"
#include "app/ReverbModel.h"

namespace dreamdsp {

// Collects the current settings into a dsp::ParamBlock.
//
// One function, called by both the publisher and the offline renderer, so the
// preview and the system output are built from the same reading of the same
// models. Two call sites assembling the block independently is exactly the
// divergence this design is trying to prevent.
dsp::ParamBlock blockFromModels(const CompressorModel *comp,
                                const ReverbModel *reverb,
                                const EffectsModel *effects);

// Publishes parameters to the copy of the DSP running inside audiodg.exe, and
// reads back what that copy says it is doing.
//
// The transport is an ordinary file, replaced by rename. That choice buys
// publication atomicity from the kernel rather than from a hand-written
// protocol: no torn reads across processes, nothing to poison if this process
// dies mid-write, no shared-section privilege problem between a normal user and
// LOCAL SERVICE, and a payload that can be inspected with a hex editor when
// something goes wrong.
class ParamPublisher : public QObject
{
    Q_OBJECT

public:
    explicit ParamPublisher(QObject *parent = nullptr);

    // The models to read. Not owned.
    void setSources(CompressorModel *comp, ReverbModel *reverb, EffectsModel *effects);

    // Coalesces a burst of slider movement into one write. Safe to call from
    // every valueChanged.
    void schedule();

    // Writes immediately, cancelling any pending scheduled write. Returns false
    // and sets lastError() if the file could not be replaced.
    bool publishNow();

    // Reads back the last published block, so a restarted GUI continues the
    // generation counter instead of resetting it -- an APO that has been
    // running all along must not see the sequence go backwards.
    void load();

    // The APO's own report. Empty/zeroed until one has been read.
    const dsp::StatusBlock &status() const { return m_status; }
    bool statusFresh() const { return m_statusFresh; }
    quint32 generation() const { return m_generation; }
    QString lastError() const { return m_lastError; }

    static QString controlDirectory();
    static QString paramFilePath();
    static QString statusFilePath();

signals:
    void published();
    void statusChanged();

private:
    void pollStatus();

    CompressorModel *m_comp = nullptr;
    ReverbModel *m_reverb = nullptr;
    EffectsModel *m_effects = nullptr;

    QTimer m_writeTimer;
    QTimer m_statusTimer;

    quint32 m_generation = 0;
    dsp::StatusBlock m_status{};
    bool m_statusFresh = false;
    QString m_lastError;
};

} // namespace dreamdsp
