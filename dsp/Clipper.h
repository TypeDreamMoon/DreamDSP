#pragma once

#include "DspTypes.h"
#include "Oversampler.h"

namespace dreamdsp::dsp {

// A soft clipper, to sit in front of the limiter.
//
// Why have both. At one decibel of peak reduction on drum-and-tone material,
// a clipper adds 0.1 dB of amplitude-modulation sideband energy to the
// sustained content while a look-ahead limiter adds 17 to 20 dB, and the
// clipper delivers more loudness for the same ceiling because the limiter's
// release never lets it recover the full headroom. The reason is duty cycle:
// at 1 dB, clipping touches 0.02% of samples in bursts averaging under three
// samples long, each hidden under the forward masking of the transient that
// caused it, whereas the limiter's gain envelope modulates *everything* for
// the whole release time. The standard chain is therefore clipper first,
// limiter second: the clipper removes the sparse peaks that would otherwise
// drive the limiter into deep envelope-driven gain reduction.
//
// The corollary, which is easy to get wrong: this must be a near-hard knee.
// tanh, arctan and x/sqrt(1+x^2) all compress everything all the time, and at
// the same 1 dB of peak reduction they add 20 to 24 dB of modulation -- as bad
// as the limiter. They are saturators. Use TubeStage for that; do not use this.
class SoftClipper
{
public:
    static constexpr int kMaxChannels = 8;

    struct Params {
        float driveDb = 0.0f;       // 0 .. 24   gain into the clipper
        // Exact at 1x. With oversampling on, the decimation filter rings past
        // it by up to about 1 dB on heavily driven material -- inherent to
        // reconstructing a band-limited waveform, and left alone on purpose.
        // See the comment in process(). The limiter is what makes a ceiling a
        // guarantee; this is what makes it cheap.
        float ceilingDb = -0.3f;    // -24 .. 0
        // How far below the ceiling the curve starts to bend.
        //
        // This is the cheapest decibel in the whole rack. A knee only about
        // 1 dB wide takes a hard clipper's alias-to-signal ratio at 3 dB of
        // clipping from 51.5 dB to 67.5 dB -- sixteen decibels -- for one
        // hundredth of a decibel of loudness and roughly one extra cycle per
        // sample. That is a better return than first-order antiderivative
        // antialiasing (58.4 dB) and better than 2x oversampling the hard
        // clipper (66.7 dB). Widen it much past 2 dB and the stage stops being
        // a clipper and starts being a saturator, with the modulation
        // behaviour that implies.
        float kneeDb = 1.0f;        // 0 .. 6
        bool oversample = true;     // 2x around the nonlinearity
        uint8_t pad[3] = {};
    };

    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;

    void process(const AudioBuffer &buf);

    // Peak reduction actually applied over the last buffer, in dB. Never
    // positive.
    float reductionDb() const noexcept { return m_reductionDb; }

private:
    Params m_p;
    int m_channels = 0;

    Oversampler2x m_os[kMaxChannels];

    float m_drive = 1.0f;
    float m_ceiling = 0.966f;
    float m_knee = 0.1f;        // fraction of the ceiling, i.e. W below
    float m_reductionDb = 0.0f;
};

// The quadratic-knee transfer curve, normalised so the ceiling is 1.
//
// Linear for |x| <= 1-W, a parabola through the knee, and exactly 1 above
// 1+W -- so the ceiling is a hard guarantee, not an asymptote. C1 continuous:
// the first derivative matches at both ends of the knee, the second does not.
// That remaining discontinuity is what the oversampler is for.
inline float clipShape(float x, float w) noexcept
{
    const float a = std::fabs(x);
    const float s = x < 0.0f ? -1.0f : 1.0f;
    if (w <= 0.0f)
        return a >= 1.0f ? s : x;
    if (a <= 1.0f - w)
        return x;
    if (a >= 1.0f + w)
        return s;
    const float d = a - (1.0f - w);
    return s * (a - d * d / (4.0f * w));
}

} // namespace dreamdsp::dsp
