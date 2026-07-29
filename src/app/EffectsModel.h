#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "MultibandCompressor.h"
#include "Saturation.h"
#include "Stereo.h"

namespace dreamdsp {

// Parameters for the effects that do not warrant a class each.
//
// One object rather than six: these are all plain parameter bags, and six
// nearly identical QObject wrappers would be more code than the DSP they
// front. The compressor and reverb keep their own models because they have
// behaviour beyond storage -- a transfer curve to plot.
class EffectsModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Owned by AppController, not by the QML engine: it is the only object
    // with a process-long lifetime and with save/restore, and the parameter
    // publisher has to see every change wherever it comes from.
    QML_UNCREATABLE("owned by AppController")

    // --- tube saturation ---
    Q_PROPERTY(bool tubeEnabled MEMBER m_tubeEnabled NOTIFY changed)
    Q_PROPERTY(double tubeDrive READ tubeDrive WRITE setTubeDrive NOTIFY changed)
    Q_PROPERTY(double tubeBias READ tubeBias WRITE setTubeBias NOTIFY changed)
    Q_PROPERTY(double tubeMix READ tubeMix WRITE setTubeMix NOTIFY changed)

    // --- psychoacoustic bass ---
    Q_PROPERTY(bool bassEnabled MEMBER m_bassEnabled NOTIFY changed)
    Q_PROPERTY(double bassCutoff READ bassCutoff WRITE setBassCutoff NOTIFY changed)
    Q_PROPERTY(double bassAmount READ bassAmount WRITE setBassAmount NOTIFY changed)
    Q_PROPERTY(double bassDrive READ bassDrive WRITE setBassDrive NOTIFY changed)
    Q_PROPERTY(bool bassRemoveOriginal READ bassRemoveOriginal WRITE setBassRemoveOriginal NOTIFY changed)

    // --- exciter ---
    Q_PROPERTY(bool exciterEnabled MEMBER m_exciterEnabled NOTIFY changed)
    Q_PROPERTY(double exciterFreq READ exciterFreq WRITE setExciterFreq NOTIFY changed)
    Q_PROPERTY(double exciterDrive READ exciterDrive WRITE setExciterDrive NOTIFY changed)
    Q_PROPERTY(double exciterAmount READ exciterAmount WRITE setExciterAmount NOTIFY changed)

    // --- stereo width ---
    Q_PROPERTY(bool widthEnabled MEMBER m_widthEnabled NOTIFY changed)
    Q_PROPERTY(double stereoWidth READ stereoWidth WRITE setStereoWidth NOTIFY changed)
    Q_PROPERTY(double monoBelow READ monoBelow WRITE setMonoBelow NOTIFY changed)

    // --- crossfeed ---
    Q_PROPERTY(bool crossfeedEnabled MEMBER m_crossfeedEnabled NOTIFY changed)
    Q_PROPERTY(double crossfeedCutoff READ crossfeedCutoff WRITE setCrossfeedCutoff NOTIFY changed)
    Q_PROPERTY(double crossfeedLevel READ crossfeedLevel WRITE setCrossfeedLevel NOTIFY changed)

    // --- multiband ---
    Q_PROPERTY(bool multibandEnabled MEMBER m_multibandEnabled NOTIFY changed)
    Q_PROPERTY(double lowCross READ lowCross WRITE setLowCross NOTIFY changed)
    Q_PROPERTY(double highCross READ highCross WRITE setHighCross NOTIFY changed)

public:
    explicit EffectsModel(QObject *parent = nullptr);

    double tubeDrive() const { return m_tube.drive; }   void setTubeDrive(double v);
    double tubeBias() const  { return m_tube.bias; }    void setTubeBias(double v);
    double tubeMix() const   { return m_tube.mix; }     void setTubeMix(double v);

    double bassCutoff() const { return m_bass.cutoffHz; } void setBassCutoff(double v);
    double bassAmount() const { return m_bass.amount; }   void setBassAmount(double v);
    double bassDrive() const  { return m_bass.drive; }    void setBassDrive(double v);
    bool bassRemoveOriginal() const { return m_bass.removeOriginal; }
    void setBassRemoveOriginal(bool v);

    double exciterFreq() const   { return m_exciter.frequencyHz; } void setExciterFreq(double v);
    double exciterDrive() const  { return m_exciter.drive; }       void setExciterDrive(double v);
    double exciterAmount() const { return m_exciter.amount; }      void setExciterAmount(double v);

    double stereoWidth() const { return m_width.width; }       void setStereoWidth(double v);
    double monoBelow() const   { return m_width.monoBelowHz; } void setMonoBelow(double v);

    double crossfeedCutoff() const { return m_crossfeed.cutoffHz; } void setCrossfeedCutoff(double v);
    double crossfeedLevel() const  { return m_crossfeed.feedDb; }   void setCrossfeedLevel(double v);

    double lowCross() const  { return m_multiband.lowCrossHz; }  void setLowCross(double v);
    double highCross() const { return m_multiband.highCrossHz; } void setHighCross(double v);

    // Snapshots for a worker thread.
    bool tubeOn() const       { return m_tubeEnabled; }
    bool bassOn() const       { return m_bassEnabled; }
    bool exciterOn() const    { return m_exciterEnabled; }
    bool widthOn() const      { return m_widthEnabled; }
    bool crossfeedOn() const  { return m_crossfeedEnabled; }
    bool multibandOn() const  { return m_multibandEnabled; }
    bool anyEnabled() const;

    dsp::TubeStage::Params tubeParams() const           { return m_tube; }
    dsp::VirtualBass::Params bassParams() const         { return m_bass; }
    dsp::Exciter::Params exciterParams() const          { return m_exciter; }
    dsp::StereoWidener::Params widthParams() const      { return m_width; }
    dsp::Crossfeed::Params crossfeedParams() const      { return m_crossfeed; }
    dsp::MultibandCompressor::Params multibandParams() const { return m_multiband; }

signals:
    void changed();

private:
    dsp::TubeStage::Params m_tube;
    dsp::VirtualBass::Params m_bass;
    dsp::Exciter::Params m_exciter;
    dsp::StereoWidener::Params m_width;
    dsp::Crossfeed::Params m_crossfeed;
    dsp::MultibandCompressor::Params m_multiband;

    bool m_tubeEnabled = false;
    bool m_bassEnabled = false;
    bool m_exciterEnabled = false;
    bool m_widthEnabled = false;
    bool m_crossfeedEnabled = false;
    bool m_multibandEnabled = false;
};

} // namespace dreamdsp
