#include "Loudness.h"

#include <cmath>
#include <cstring>

namespace dreamdsp::dsp {

namespace {

// Equalizer APO 1.4.2, LoudnessCorrectionFilter::getLShelfParamter and
// getHShelfParamter. The frequencies and Q values are fixed there too.
constexpr double kLowHz = 75.0;
constexpr double kLowQ = 0.52;
constexpr double kHighHz = 10000.0;
constexpr double kHighQ = 0.9;

// Below this the stage is a wire. APO uses the same threshold, and it matters:
// without it the correction would keep running two biquads to apply 0.05 dB.
constexpr double kNeutralDb = 0.2;

} // namespace

void LoudnessCorrection::shelvesFor(const Params &p, double *lowDb, double *highDb,
                                    double *preampDb) noexcept
{
    const double amount = clampTo(double(p.amount), 0.0, 1.0);
    const double volDiff = double(p.referenceDb) - double(p.offsetDb) - double(p.volumeDb);

    double low = 0.0, pre = 0.0;
    if (volDiff > 0.0) {
        // Playing below the reference. The boost is cancelled by an equal
        // preamp, so what the ear actually gets is the midrange coming down
        // rather than the bass going up -- which is the only version of this
        // that does not eat headroom.
        low = volDiff * 0.55 / (1.0 - 0.55) * amount;
        pre = -low;
    } else if (volDiff < 0.0) {
        low = volDiff * 0.55 * std::exp(volDiff / 90.0) * amount;
    }

    // The high shelf is evaluated at the already-preamped level, as in APO.
    const double highVolDiff = volDiff - pre;
    double high = 0.0;
    if (highVolDiff > 0.0)
        high = highVolDiff * 0.225 * std::exp(-highVolDiff / 100.0) * amount;
    else if (highVolDiff < 0.0)
        high = highVolDiff * 0.175 * std::exp(highVolDiff / 80.0) * amount;

    *lowDb = low;
    *highDb = high;
    *preampDb = pre;
}

void LoudnessCorrection::prepare(double sampleRate, int channels)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = clampTo(channels, 0, kMaxChannels);

    std::memset(&m_p, 0, sizeof m_p);
    m_low = BiquadCoeffs{};
    m_high = BiquadCoeffs{};
    m_lowDb = m_highDb = m_preampDb = 0.0;
    m_preampLin = 1.0;
    m_neutral = true;

    reset();
}

void LoudnessCorrection::reset() noexcept
{
    std::memset(m_state, 0, sizeof m_state);
}

void LoudnessCorrection::setParams(const Params &p) noexcept
{
    m_p = p;

    shelvesFor(p, &m_lowDb, &m_highDb, &m_preampDb);

    const double biggest = std::fabs(m_lowDb) > std::fabs(m_highDb)
                               ? std::fabs(m_lowDb) : std::fabs(m_highDb);
    m_neutral = biggest < kNeutralDb;
    if (m_neutral) {
        m_preampLin = 1.0;
        return;
    }

    // LSC/HSC rather than LS/HS: APO constructs its BiQuad directly with a Q,
    // bypassing the corner-frequency adjustment, and those are the two spellings
    // that mean "Fc is the centre frequency, Q as given".
    const FilterSections low = designFilter(FilterKind::LSC, kLowHz, m_lowDb, kLowQ, m_sampleRate);
    const FilterSections high = designFilter(FilterKind::HSC, kHighHz, m_highDb, kHighQ, m_sampleRate);
    m_low = low.count > 0 ? low.section[0] : BiquadCoeffs{};
    m_high = high.count > 0 ? high.section[0] : BiquadCoeffs{};

    m_preampLin = std::pow(10.0, m_preampDb / 20.0);
}

void LoudnessCorrection::process(const AudioBuffer &buf)
{
    if (m_neutral || !buf.valid())
        return;

    const int channels = buf.channelCount < m_channels ? buf.channelCount : m_channels;
    const int frames = buf.frames;

    for (int c = 0; c < channels; ++c) {
        float *x = buf.channels[c];
        if (!x)
            continue;

        double *zl = m_state[c][0];
        double *zh = m_state[c][1];
        bool bad = false;

        for (int f = 0; f < frames; ++f) {
            double v = double(x[f]) * m_preampLin;

            double y = m_low.b0 * v + m_low.b1 * zl[0] + m_low.b2 * zl[1]
                       - m_low.a1 * zl[2] - m_low.a2 * zl[3];
            zl[1] = zl[0]; zl[0] = v;
            zl[3] = zl[2]; zl[2] = y;
            v = y;

            y = m_high.b0 * v + m_high.b1 * zh[0] + m_high.b2 * zh[1]
                - m_high.a1 * zh[2] - m_high.a2 * zh[3];
            zh[1] = zh[0]; zh[0] = v;
            zh[3] = zh[2]; zh[2] = y;

            x[f] = railed(y, &bad);
        }

        if (bad) {
            std::memset(m_state[c], 0, sizeof m_state[c]);
        }
    }
}

} // namespace dreamdsp::dsp
