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
#include "app/EqBandModel.h"
#include "app/OutputModel.h"
#include "app/ReverbModel.h"

namespace dreamdsp {

// The band list the GUI edits, as the equalizer's wire format.
//
// Bands past the thirty-second are dropped rather than silently folded in: the
// parameter block is fixed size by design, and a preset that would not fit has
// to be visibly cut, not quietly approximated.
dsp::Equalizer::Params eqParamsFromBands(const QVector<PresetBand> &bands, double preampDb);

// Collects the current settings into a dsp::ParamBlock.
//
// One function, called by both the publisher and the offline renderer, so the
// preview and the system output are built from the same reading of the same
// models. Two call sites assembling the block independently is exactly the
// divergence this design is trying to prevent.
dsp::ParamBlock blockFromModels(const CompressorModel *comp,
                                const ReverbModel *reverb,
                                const EffectsModel *effects,
                                const dsp::Convolution::Params &convolution,
                                bool convolutionEnabled,
                                const EqBandModel *bands = nullptr,
                                double preampDb = 0.0,
                                bool eqEnabled = false,
                                const OutputModel *output = nullptr);

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
    void setSources(CompressorModel *comp, ReverbModel *reverb, EffectsModel *effects,
                    EqBandModel *bands, OutputModel *output);

    // Preamp, bypass, the graphic curve and the chain order are the
    // controller's rather than any one model's.
    void setEqualizer(double preampDb, bool enabled);

    // The one switch that turns everything off.
    //
    // Not a per-effect bit and not a separate code path: the wire format
    // already says an empty enable mask means "pass audio through untouched",
    // so a master bypass is that mask and nothing else. Every effect keeps its
    // own switch position and every parameter keeps its value, because the
    // block is also the session store -- switching back on has to restore what
    // was running, not a set of defaults.
    void setMasterEnabled(bool enabled);
    void setGraphic(const dsp::GraphicEq::Params &p, bool enabled);
    void setOrder(const uint8_t *order);

    // Decodes an impulse response file, writes it into the control directory as
    // a content-addressed blob, and returns the parameters that name it.
    //
    // The blob is committed before the parameters that reference it ever
    // reach disk, and it is named after a hash of its own samples -- so the two
    // files can never describe different impulse responses, however they
    // interleave, and a leftover from a previous run cannot masquerade as the
    // current one.
    //
    // No resampling happens here. The file goes across at its own rate and the
    // APO converts it, because only the APO knows what rate the endpoint is
    // actually running at.
    bool publishImpulse(const QString &wavPath, QString *error);

    // Forgets the current impulse response. Does not delete the blob: another
    // endpoint may still be using it, and they are small and content-addressed.
    void clearImpulse();

    // What the convolution stage should be told. Enabled is separate from
    // configured: an impulse response can be loaded and switched off.
    void setConvolutionEnabled(bool on);
    void setConvolutionMix(double mix);
    void setConvolutionTrimDb(double db);

    const dsp::Convolution::Params &convolution() const { return m_convolution; }
    bool convolutionEnabled() const { return m_convolutionEnabled; }
    QString impulseName() const { return m_impulseName; }

    // Read back after load(), so the controller resumes the curve and the order
    // that were last running rather than a second copy kept elsewhere.
    const dsp::GraphicEq::Params &graphic() const { return m_graphic; }
    bool graphicEnabled() const { return m_graphicEnabled; }
    const uint8_t *order() const { return m_order; }

    static QString impulseDirectory();

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
    EqBandModel *m_bands = nullptr;
    OutputModel *m_output = nullptr;
    double m_preampDb = 0.0;
    bool m_eqEnabled = true;
    bool m_masterEnabled = true;
    dsp::GraphicEq::Params m_graphic{};
    bool m_graphicEnabled = false;
    uint8_t m_order[dsp::kStageCount];

    QTimer m_writeTimer;
    QTimer m_statusTimer;

    quint32 m_generation = 0;
    dsp::Convolution::Params m_convolution;
    bool m_convolutionEnabled = false;
    QString m_impulseName;

    dsp::StatusBlock m_status{};
    bool m_statusFresh = false;
    QString m_lastError;
};

} // namespace dreamdsp
