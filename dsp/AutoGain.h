#pragma once

#include "DspTypes.h"
#include "LoudnessMeter.h"

namespace dreamdsp::dsp {

// Auto volume: one slow gain that holds programme loudness at a target.
//
// The distinction that matters, and the reason this is not the compressor:
// a leveller measures perceptual loudness over seconds and moves one gain
// slowly enough that nothing inside a phrase changes shape; a compressor
// measures a rectified envelope over milliseconds and changes the shape of the
// waveform on purpose. Anything whose gain can move appreciably within one
// syllable is a compressor no matter what it is called.
//
// The rate is the whole design. Four independent implementations converge on
// the same band for steady-state movement -- Orban's broadcast AGC calls
// 0.5 dB/s "effectively frozen" and 2 dB/s "open, natural and non-fatiguing";
// ffmpeg's loudnorm creeps at 0.5023 dB/s; its dynaudnorm peaks at 1.54 dB/s
// at defaults; AC-3 and AC-4 release at 0.4 to 1.4 dB/s. That convergence is
// the evidence, not a published threshold -- I could not find a controlled
// listening study on ramp-rate audibility, and the adjacent psychoacoustics
// (amplitude-modulation detection at roughly 0.5 to 0.8 dB peak-to-peak) only
// becomes a rate once you assume an integration window.
//
// Two details are borrowed wholesale because they are the difference between
// inaudible and breathing:
//   - Orban's target-zone window. Inside a 3 dB dead zone the gain creeps at
//     0.5 dB/s or not at all, so material that is already well controlled is
//     left alone; outside it, the full rate applies.
//   - Hard gating. Below the gate the gain freezes rather than decaying to
//     unity, so a quiet passage does not walk the gain and then slam on the
//     next entry.
class LoudnessLeveller
{
public:
    struct Params {
        float targetLufs = -20.0f;      // -40 .. -10
        float maxGainDb = 12.0f;        //   0 .. 25
        float rateDbPerSec = 3.0f;      // 0.25 .. 20
        float windowDb = 3.0f;          //   0 .. 12   Orban's target zone
        float gateLufs = -50.0f;        // -70 .. -20
    };

    void prepare(double sampleRate, int channels, int maxFrames);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;

    void process(const AudioBuffer &buf);

    float gainDb() const noexcept { return m_gainDb; }
    double lufs() const noexcept { return m_meter.lufs(); }

private:
    Params m_p;
    double m_sampleRate = 48000.0;

    // Short-term, 3 s. Momentary would track phrases; that is a compressor's
    // job, and doing it here is how a leveller starts to pump.
    LoudnessMeter m_meter;

    float m_gainDb = 0.0f;
    float m_targetDb = 0.0f;
    float m_upStep = 0.0f, m_downStep = 0.0f, m_creepStep = 0.0f;

    // A scratch view of the buffer so the meter reads the *input* to this
    // stage. Measuring the output would close a loop around a gain the meter
    // itself set, which oscillates at the meter's own window length.
    void updateTarget() noexcept;
};

// ---------------------------------------------------------------------------

// Night mode: the Dolby Digital dynamic-range-control curves.
//
// These are the five compression profiles a broadcast or Blu-ray decoder
// applies when you ask for "night mode" or "reduced dynamic range". They are
// not in the AC-3 bitstream specification -- ATSC A/52 standardises only the
// transport of the resulting gain word. The curves themselves are published in
// two places that agree: Dolby's own metadata guide, and ETSI TS 103 190-1
// Table 161, which restates them parametrically inside the AC-4 standard so a
// transcoder can reproduce them. The ETSI table is the better source: it also
// carries the time constants, which Dolby's guide omits, and it corrects two
// arithmetic slips in the guide's Film Light row.
//
// Each curve is a piecewise-linear map from level to gain, in dB, with levels
// measured relative to a reference level -- the equivalent of dialnorm. Below
// the reference the curve boosts, around it there is a null band where nothing
// happens, and above it the ratio steepens to 20:1.
class DynamicRange
{
public:
    static constexpr int kMaxChannels = 8;

    enum Profile : int32_t {
        kProfileNone = 0,
        kProfileFilmStandard,
        kProfileFilmLight,
        kProfileMusicStandard,
        kProfileMusicLight,
        kProfileSpeech,
        kProfileCount
    };

    struct Params {
        int32_t profile = kProfileFilmStandard;
        // Separate scalars for the two halves of the curve, so "compress the
        // loud bits but do not lift the quiet ones" is expressible. MPEG-D DRC
        // splits them the same way and for the same reason.
        float boost = 1.0f;         // 0 .. 1
        float cut = 1.0f;           // 0 .. 1
        float referenceLufs = -24.0f;   // -40 .. -10, our dialnorm
    };

    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;

    void process(const AudioBuffer &buf);

    float gainDb() const noexcept { return m_gainDb; }

    // The static curve, exposed so the self-test can check the published
    // ratios and so the GUI can plot it. `level` is in dB relative to the
    // reference; the result is a gain in dB, before the boost/cut scalars.
    static float curveGainDb(int32_t profile, float levelDb) noexcept;

private:
    // AC-4 updates the gain once per QMF time slot, which is 64 samples. That
    // is 1.333 ms at 48 kHz and a natural tick here too: fast enough for the
    // 10 ms attack the profiles ask for, coarse enough that the level estimate
    // is an RMS rather than a single sample.
    static constexpr int kTick = 64;

    Params m_p;
    double m_sampleRate = 48000.0;
    int m_channels = 0;

    KWeighting m_k[kMaxChannels];
    double m_acc = 0.0;
    int m_accCount = 0;

    float m_levelDb = -60.0f;     // smoothed, relative to the reference
    float m_gainDb = 0.0f;        // smoothed
    float m_gainLin = 1.0f;
    float m_gainStep = 0.0f;      // per-sample ramp toward m_gainLin
    float m_gainNow = 1.0f;
};

} // namespace dreamdsp::dsp
