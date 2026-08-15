#pragma once

#include "DspTypes.h"

namespace dreamdsp::dsp {

// Log-domain feed-forward compressor, following Giannoulis, Massberg & Reiss,
// "Digital Dynamic Range Compressor Design -- A Tutorial and Analysis" (JAES
// 2012). That topology is chosen because its gain computer is exactly
// specified, which means it can be unit-tested against closed-form answers
// rather than judged by ear.
//
// Signal flow, per sample:
//   detector (stereo-linked peak) -> dB -> gain computer (soft knee)
//   -> branching smoother (attack/release) -> makeup -> apply
//
// Channels are linked: the loudest channel drives the gain for all of them.
// Compressing channels independently shifts the stereo image whenever one side
// is louder, which is audible and almost never wanted.
class Compressor
{
public:
    struct Params {
        float thresholdDb = 0.0f;    // level above which reduction starts
        float ratio = 1.0f;          // 1 = off, 20+ approaches limiting
        float kneeDb = 0.0f;         // total width of the soft knee
        float attackMs = 5.0f;
        float releaseMs = 50.0f;
        float makeupDb = 0.0f;

        // Program-dependent modes, matching what compressors label "auto".
        bool autoKnee = false;
        bool autoAttack = false;
        bool autoRelease = false;
        bool autoMakeup = false;
    };

    void prepare(double sampleRate, int maxChannels);
    void reset();

    void setParams(const Params &p) { m_p = p; }
    const Params &params() const { return m_p; }

    void process(const AudioBuffer &buf);

    // Gain currently being applied, in dB (negative = reduction). For metering.
    float gainReductionDb() const { return -m_envelopeDb; }

    // The static input->output curve, exposed so it can be tested and drawn.
    // Both in dBFS.
    float outputForInputDb(float inputDb) const;

    // Resolved values, which differ from the raw parameters when the
    // corresponding auto flag is set. For display.
    float effectiveKneeDb() const;
    float effectiveMakeupDb() const;


private:
    float computeReductionDb(float inputDb) const;   // >= 0, amount to subtract
    void updateAdaptiveTimes(float inputDb);

    Params m_p;
    double m_sampleRate = 48000.0;

    float m_envelopeDb = 0.0f;       // smoothed reduction, dB
    float m_attackCoeff = 0.0f;
    float m_releaseCoeff = 0.0f;

    // Crest factor drives the adaptive timing: a percussive signal has a high
    // peak-to-RMS ratio and wants a fast attack, a sustained one does not.
    float m_peakSq = 0.0f;
    float m_rmsSq = 0.0f;
    float m_crestCoeff = 0.0f;
    float m_adaptiveAttackMs = 5.0f;
    float m_adaptiveReleaseMs = 50.0f;
};

} // namespace dreamdsp::dsp
