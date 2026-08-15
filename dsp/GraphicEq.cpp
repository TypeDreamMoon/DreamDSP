#include "GraphicEq.h"

#include <cmath>
#include <cstring>

namespace dreamdsp::dsp {

namespace {

// ISO 266 preferred frequencies. Not computed from 10^(n/10): the standard
// rounds them (315, 630, 1250 rather than 316.2, 631.0, 1258.9) and every
// GraphicEQ file in circulation uses the rounded values, so a computed grid
// would sit slightly beside the data it is meant to reproduce.
const double kCentres[GraphicEq::kBands] = {
    20.0,   25.0,   31.5,   40.0,   50.0,   63.0,   80.0,   100.0,
    125.0,  160.0,  200.0,  250.0,  315.0,  400.0,  500.0,  630.0,
    800.0,  1000.0, 1250.0, 1600.0, 2000.0, 2500.0, 3150.0, 4000.0,
    5000.0, 6300.0, 8000.0, 10000.0, 12500.0, 16000.0, 20000.0
};

} // namespace

const double *GraphicEq::centres() noexcept
{
    return kCentres;
}

void GraphicEq::prepare(double sampleRate, int channels)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_eq.prepare(m_sampleRate, channels);
}

void GraphicEq::reset() noexcept
{
    m_eq.reset();
}

void GraphicEq::setParams(const Params &p) noexcept
{
    Equalizer::Params e;
    std::memset(&e, 0, sizeof e);
    e.preampDb = 0.0f;
    e.bandCount = uint32_t(kBands);

    const float amount = clampTo(p.amount, 0.0f, 1.0f);
    for (int i = 0; i < kBands; ++i) {
        EqBand &b = e.band[i];
        b.freqHz = float(kCentres[i]);
        b.gainDb = p.gainDb[i] * amount;
        b.q = float(kQ);
        b.type = uint8_t(FilterKind::PK);
        // Every band stays in the cascade even at 0 dB. Dropping the flat ones
        // would freeze their delay state and then feed it back in stale the
        // moment a gain crossed zero -- a click on the commonest gesture there
        // is, for a saving of thirty-one identity biquads.
        b.enabled = 1u;
        b.reserved = 0;
    }

    m_eq.setParams(e);
}

void GraphicEq::process(const AudioBuffer &buf)
{
    m_eq.process(buf);
}

// ---------------------------------------------------------------------------

double fitGraphicEq(const double *targetDb, double sampleRate, float *gainsOut) noexcept
{
    if (!targetDb || !gainsOut || !(sampleRate > 0.0))
        return 0.0;

    const double *f = GraphicEq::centres();
    double gain[GraphicEq::kBands];

    // First guess: each band asked to produce its own target on its own. Always
    // too strong, because the neighbours help, which is what the loop fixes.
    for (int i = 0; i < GraphicEq::kBands; ++i)
        gain[i] = targetDb[i];

    // Under-relaxed. At a Q of 2.2 a band's neighbours contribute enough at its
    // centre that a full correction overshoots and the iteration rings; 0.6
    // converges monotonically across the whole range of curves these files
    // contain.
    constexpr double kRelax = 0.6;
    constexpr int kIterations = 24;

    double worst = 0.0;
    for (int iter = 0; iter < kIterations; ++iter) {
        // Design the whole cascade as it currently stands, then read its
        // response at the centres. The response is read out of designFilter(),
        // i.e. out of the same code the audio goes through, so the fit is
        // against the filter that will actually run rather than against an
        // idealised one.
        FilterSections section[GraphicEq::kBands];
        for (int i = 0; i < GraphicEq::kBands; ++i) {
            section[i] = designFilter(FilterKind::PK, f[i], gain[i], GraphicEq::kQ, sampleRate);
        }

        worst = 0.0;
        double correction[GraphicEq::kBands];
        for (int i = 0; i < GraphicEq::kBands; ++i) {
            double got = 0.0;
            for (int j = 0; j < GraphicEq::kBands; ++j)
                got += magnitudeDb(section[j], f[i], sampleRate);
            const double err = targetDb[i] - got;
            correction[i] = err;
            const double a = err < 0.0 ? -err : err;
            if (a > worst)
                worst = a;
        }

        if (worst < 0.01)
            break;

        for (int i = 0; i < GraphicEq::kBands; ++i) {
            gain[i] += kRelax * correction[i];
            // The wire format bounds these to +-40 dB; keeping the iteration
            // inside the same bound stops a pathological target from walking a
            // band off to somewhere the sanitiser would clamp it back from,
            // which would make the fit disagree with what actually runs.
            gain[i] = clampTo(gain[i], -40.0, 40.0);
        }
    }

    for (int i = 0; i < GraphicEq::kBands; ++i)
        gainsOut[i] = float(gain[i]);
    return worst;
}

} // namespace dreamdsp::dsp
