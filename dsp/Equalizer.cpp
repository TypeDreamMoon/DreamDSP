#include "Equalizer.h"

#include <cmath>
#include <cstring>

namespace dreamdsp::dsp {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInvSqrt2 = 0.70710678118654752440;

// Fourth-order Butterworth as two cascaded sections: the pole Q values are
// 1 / (2 cos(pi/8)) and 1 / (2 cos(3 pi/8)).
constexpr double kButterworth4Q[2] = { 0.54119610014619698, 1.30656296487637652 };

struct KindInfo {
    const char *token;
    bool gain;
    bool q;
};

// Index-aligned with FilterKind.
constexpr KindInfo kKinds[] = {
    { "PK",   true,  true  },
    { "LPQ",  false, true  },
    { "HPQ",  false, true  },
    { "BP",   false, true  },
    { "LS",   true,  false },
    { "HS",   true,  false },
    { "NO",   false, true  },
    { "AP",   false, true  },
    { "LSC",  true,  true  },
    { "HSC",  true,  true  },
    { "BWLP", false, false },
    { "BWHP", false, false },
    { "LRLP", false, false },
    { "LRHP", false, false },
    { "LSCQ", true,  true  },
    { "HSCQ", true,  true  },
    { "LSQ",  true,  true  },
    { "HSQ",  true,  true  },
};
static_assert(sizeof(kKinds) / sizeof(kKinds[0]) == size_t(FilterKind::Count),
              "kKinds must stay index-aligned with FilterKind");

enum class Shape { Peak, LowShelf, HighShelf, LowPass, HighPass, BandPass, Notch, AllPass };

Shape shapeOf(FilterKind k) noexcept
{
    switch (k) {
    case FilterKind::PK:                                        return Shape::Peak;
    case FilterKind::LS:  case FilterKind::LSC:
    case FilterKind::LSCQ: case FilterKind::LSQ:                return Shape::LowShelf;
    case FilterKind::HS:  case FilterKind::HSC:
    case FilterKind::HSCQ: case FilterKind::HSQ:                return Shape::HighShelf;
    case FilterKind::LPQ: case FilterKind::BWLP:
    case FilterKind::LRLP:                                      return Shape::LowPass;
    case FilterKind::HPQ: case FilterKind::BWHP:
    case FilterKind::LRHP:                                      return Shape::HighPass;
    case FilterKind::BP:                                        return Shape::BandPass;
    case FilterKind::NO:                                        return Shape::Notch;
    case FilterKind::AP:                                        return Shape::AllPass;
    case FilterKind::Count:                                     break;
    }
    return Shape::Peak;
}

bool isShelf(Shape s) noexcept
{
    return s == Shape::LowShelf || s == Shape::HighShelf;
}

// Fc names the corner (the point the slope leaves 0 dB) rather than the centre
// (the point at half the shelf's gain). Only the Q-carrying non-C spellings do:
// this reproduces Equalizer APO 1.4.2, where BiQuadFilterFactory sets
// isCornerFreq for a shelf whose type token does not end in 'C'.
bool isCornerShelf(FilterKind k) noexcept
{
    return k == FilterKind::LSQ || k == FilterKind::HSQ;
}

// Finiteness by exponent inspection. This layer is compiled with /fp:fast,
// under which std::isfinite and `v == v` may both be folded to a constant --
// see ParamBlock::sanF, which does the same thing for floats.
bool finite64(double v) noexcept
{
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    return (bits & 0x7FF0000000000000ull) != 0x7FF0000000000000ull;
}

// A direct form I biquad is stable exactly when its poles are inside the unit
// circle, which in terms of the denominator is |a2| < 1 and |a1| < 1 + a2.
// Anything else is an exponentially growing signal, so it becomes a wire.
BiquadCoeffs stabilised(double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    BiquadCoeffs c;
    if (a0 == 0.0 || !finite64(a0))
        return c;

    const double n0 = b0 / a0, n1 = b1 / a0, n2 = b2 / a0;
    const double d1 = a1 / a0, d2 = a2 / a0;

    if (!finite64(n0) || !finite64(n1) || !finite64(n2) || !finite64(d1) || !finite64(d2))
        return c;
    if (!(std::fabs(d2) < 1.0) || !(std::fabs(d1) < 1.0 + d2))
        return c;

    c.b0 = n0; c.b1 = n1; c.b2 = n2; c.a1 = d1; c.a2 = d2;
    return c;
}

// One RBJ cookbook section. `alpha` already carries whichever of Q, bandwidth
// or slope the caller resolved.
BiquadCoeffs cookbook(Shape shape, double cosw, double alpha, double A) noexcept
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (shape) {
    case Shape::Peak:
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cosw;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cosw;
        a2 = 1.0 - alpha / A;
        break;

    case Shape::LowShelf: {
        const double beta = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) - (A - 1.0) * cosw + beta);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosw);
        b2 = A * ((A + 1.0) - (A - 1.0) * cosw - beta);
        a0 = (A + 1.0) + (A - 1.0) * cosw + beta;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosw);
        a2 = (A + 1.0) + (A - 1.0) * cosw - beta;
        break;
    }

    case Shape::HighShelf: {
        const double beta = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) + (A - 1.0) * cosw + beta);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosw);
        b2 = A * ((A + 1.0) + (A - 1.0) * cosw - beta);
        a0 = (A + 1.0) - (A - 1.0) * cosw + beta;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosw);
        a2 = (A + 1.0) - (A - 1.0) * cosw - beta;
        break;
    }

    case Shape::LowPass:
        b0 = (1.0 - cosw) / 2.0;
        b1 = 1.0 - cosw;
        b2 = (1.0 - cosw) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw;
        a2 = 1.0 - alpha;
        break;

    case Shape::HighPass:
        b0 = (1.0 + cosw) / 2.0;
        b1 = -(1.0 + cosw);
        b2 = (1.0 + cosw) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw;
        a2 = 1.0 - alpha;
        break;

    case Shape::BandPass:
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw;
        a2 = 1.0 - alpha;
        break;

    case Shape::Notch:
        b0 = 1.0;
        b1 = -2.0 * cosw;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw;
        a2 = 1.0 - alpha;
        break;

    case Shape::AllPass:
        b0 = 1.0 - alpha;
        b1 = -2.0 * cosw;
        b2 = 1.0 + alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cosw;
        a2 = 1.0 - alpha;
        break;
    }

    return stabilised(b0, b1, b2, a0, a1, a2);
}

} // namespace

const char *filterToken(FilterKind kind) noexcept
{
    const int i = int(kind);
    return (i >= 0 && i < int(FilterKind::Count)) ? kKinds[i].token : "PK";
}

bool filterHasGain(FilterKind kind) noexcept
{
    const int i = int(kind);
    return (i >= 0 && i < int(FilterKind::Count)) ? kKinds[i].gain : true;
}

bool filterHasQ(FilterKind kind) noexcept
{
    const int i = int(kind);
    return (i >= 0 && i < int(FilterKind::Count)) ? kKinds[i].q : true;
}

FilterSections designFilter(FilterKind kind,
                            double freqHz,
                            double gainDb,
                            double q,
                            double sampleRate) noexcept
{
    FilterSections out;

    if (int(kind) < 0 || kind >= FilterKind::Count)
        return out;
    if (!(sampleRate > 0.0) || !(freqHz > 0.0) || !finite64(gainDb) || !finite64(q))
        return out;

    const Shape shape = shapeOf(kind);
    if (!filterHasGain(kind))
        gainDb = 0.0;

    // Above Nyquist the bilinear transform folds the response back on itself and
    // the design stops meaning anything; clamp rather than refuse, so that a
    // 20 kHz shelf still behaves on a 32 kHz endpoint.
    const double nyquist = sampleRate * 0.5;
    if (freqHz >= nyquist)
        freqHz = nyquist * 0.99;

    // A is the amplitude at the shelf's plateau / the peak's tip. The halved
    // exponent is the cookbook's, and is why a +6 dB peaking filter is designed
    // with A = 10^(6/40) rather than 10^(6/20).
    const double A = std::pow(10.0, gainDb / 40.0);

    // --- fourth-order types --------------------------------------------------
    //
    // Two cascaded sections at the same corner. Butterworth spreads the pole Q
    // values to keep the passband maximally flat and lands on -3 dB at Fc;
    // Linkwitz-Riley repeats one Butterworth section, landing on -6 dB, which is
    // what makes a low and a high of the same corner sum back to unity.
    //
    // Equalizer APO has no such filter types -- these four tokens are Peace's,
    // and APO dropped them -- so there is no prior behaviour to reproduce, only
    // the names to honour.
    if (kind == FilterKind::BWLP || kind == FilterKind::BWHP
        || kind == FilterKind::LRLP || kind == FilterKind::LRHP) {
        const bool butter = (kind == FilterKind::BWLP || kind == FilterKind::BWHP);
        const double w0 = 2.0 * kPi * freqHz / sampleRate;
        const double cosw = std::cos(w0), sinw = std::sin(w0);

        for (int s = 0; s < 2; ++s) {
            const double sq = butter ? kButterworth4Q[s] : kInvSqrt2;
            out.section[s] = cookbook(shape, cosw, sinw / (2.0 * sq), 1.0);
        }
        out.count = 2;
        return out;
    }

    // --- second-order types --------------------------------------------------

    // Defaults for a band whose Q was never set, taken from Equalizer APO's
    // BiQuadFilterFactory: 30 for a notch and 1/sqrt(2) for the passes are the
    // values it substitutes when a config line omits Q.
    bool useSlope = false;
    double slopeS = 0.9;   // APO's shelf default, "found out by experimentation"

    if (isShelf(shape)) {
        if (!filterHasQ(kind) || !(q > 0.0))
            useSlope = true;
    } else if (!(q > 0.0)) {
        q = (kind == FilterKind::NO) ? 30.0 : kInvSqrt2;
    }

    // The corner-frequency shelves are designed at a shifted centre frequency,
    // the DCX2496 adjustment Equalizer APO applies in BiQuadFilter::initialize.
    // Without it "Fc" would mean the half-gain point for LSQ and the corner for
    // LS, and a preset moved between the two spellings would shift audibly.
    if (isShelf(shape) && isCornerShelf(kind)) {
        double s = slopeS;
        if (!useSlope) {
            const double inv = q * q;
            s = 1.0 / ((1.0 / inv - 2.0) / (A + 1.0 / A) + 1.0);
        }
        if (finite64(s) && s > 0.0) {
            const double factor = std::pow(10.0, std::fabs(gainDb) / 80.0 / s);
            if (finite64(factor) && factor > 0.0)
                freqHz = (shape == Shape::LowShelf) ? freqHz * factor : freqHz / factor;
            if (freqHz >= nyquist)
                freqHz = nyquist * 0.99;
            if (!(freqHz > 0.0))
                return out;
        }
    }

    const double w0 = 2.0 * kPi * freqHz / sampleRate;
    const double cosw = std::cos(w0), sinw = std::sin(w0);

    const double alpha = useSlope
        ? sinw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / slopeS - 1.0) + 2.0)
        : sinw / (2.0 * q);

    out.section[0] = cookbook(shape, cosw, alpha, A);
    out.count = 1;
    return out;
}

double magnitudeDb(const BiquadCoeffs &c, double freqHz, double sampleRate) noexcept
{
    if (!(sampleRate > 0.0))
        return 0.0;

    const double w = 2.0 * kPi * freqHz / sampleRate;
    const double cw = std::cos(w), sw = std::sin(w);
    const double c2w = std::cos(2.0 * w), s2w = std::sin(2.0 * w);

    const double numRe = c.b0 + c.b1 * cw + c.b2 * c2w;
    const double numIm = -(c.b1 * sw + c.b2 * s2w);
    const double denRe = 1.0 + c.a1 * cw + c.a2 * c2w;
    const double denIm = -(c.a1 * sw + c.a2 * s2w);

    const double num = numRe * numRe + numIm * numIm;
    const double den = denRe * denRe + denIm * denIm;
    if (!(den > 0.0) || !(num > 0.0))
        return -120.0;

    return 10.0 * std::log10(num / den);
}

double magnitudeDb(const FilterSections &f, double freqHz, double sampleRate) noexcept
{
    double db = 0.0;
    for (int i = 0; i < f.count && i < 2; ++i)
        db += magnitudeDb(f.section[i], freqHz, sampleRate);
    return db;
}

// ---------------------------------------------------------------------------

void Equalizer::prepare(double sampleRate, int channels)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = clampTo(channels, 0, kMaxChannels);

    // Forget what was installed, so the next setParams redesigns every band
    // against the new rate instead of finding them unchanged and keeping
    // coefficients built for the old one.
    std::memset(&m_applied, 0, sizeof m_applied);
    std::memset(m_sectionCount, 0, sizeof m_sectionCount);
    for (auto &c : m_coeff)
        c = BiquadCoeffs{};
    m_activeCount = 0;
    m_preampLin = 1.0f;

    reset();
}

void Equalizer::reset() noexcept
{
    std::memset(m_state, 0, sizeof m_state);
}

void Equalizer::design(int band) noexcept
{
    const EqBand &b = m_applied.band[band];
    const FilterSections f = designFilter(FilterKind(b.type), double(b.freqHz),
                                          double(b.gainDb), double(b.q), m_sampleRate);
    m_coeff[band * 2 + 0] = f.count > 0 ? f.section[0] : BiquadCoeffs{};
    m_coeff[band * 2 + 1] = f.count > 1 ? f.section[1] : BiquadCoeffs{};
    m_sectionCount[band] = uint8_t(f.count);
}

void Equalizer::rebuildActive() noexcept
{
    int n = 0;
    const int count = int(m_applied.bandCount) < kMaxBands ? int(m_applied.bandCount) : kMaxBands;
    for (int i = 0; i < count; ++i) {
        if (!m_applied.band[i].enabled)
            continue;
        for (int s = 0; s < int(m_sectionCount[i]); ++s)
            m_active[n++] = uint8_t(i * 2 + s);
    }
    m_activeCount = n;
}

void Equalizer::setParams(const Params &p) noexcept
{
    // A band at 0 dB is left in the chain rather than compacted out. It is an
    // identity biquad and costs about a millionth of the buffer's budget, and
    // dropping it would mean its delay state froze and then re-entered the
    // recursion stale the moment the gain crossed zero -- a click on exactly the
    // gesture people make most.
    const int oldCount = int(m_applied.bandCount) < kMaxBands ? int(m_applied.bandCount) : kMaxBands;
    const int newCount = int(p.bandCount) < kMaxBands ? int(p.bandCount) : kMaxBands;
    const int scan = oldCount > newCount ? oldCount : newCount;

    bool changed = (oldCount != newCount);

    for (int i = 0; i < scan; ++i) {
        const EqBand &nb = p.band[i];
        const EqBand &ob = m_applied.band[i];
        if (std::memcmp(&nb, &ob, sizeof(EqBand)) == 0)
            continue;

        // Type changes swap the filter's whole structure, and an enable brings
        // back state that has been frozen for however long the band was off.
        // Both want a clean start; a frequency or gain change does not, and
        // clearing on those would click on every slider drag.
        const bool clearState = (nb.type != ob.type) || (nb.enabled && !ob.enabled);

        m_applied.band[i] = nb;
        changed = true;

        if (i < newCount)
            design(i);
        else
            m_sectionCount[i] = 0;

        if (clearState)
            for (int c = 0; c < kMaxChannels; ++c)
                std::memset(m_state[c][i * 2], 0, 2 * 4 * sizeof(double));
    }

    m_applied.bandCount = p.bandCount;
    m_applied.preampDb = p.preampDb;
    m_preampLin = dbToLin(p.preampDb);

    // Unconditional on any change rather than tracked precisely: the list is at
    // most sixty-four bytes long, and getting the tracking wrong would leave the
    // audio thread running a stale set of filters.
    if (changed)
        rebuildActive();
}

void Equalizer::process(const AudioBuffer &buf)
{
    if (!buf.valid())
        return;

    const int channels = buf.channelCount < kMaxChannels ? buf.channelCount : kMaxChannels;
    const int frames = buf.frames;
    const int sections = m_activeCount;
    const double preamp = double(m_preampLin);

    for (int c = 0; c < channels; ++c) {
        float *x = buf.channels[c];
        if (!x)
            continue;

        double (*state)[4] = m_state[c];
        bool bad = false;

        for (int f = 0; f < frames; ++f) {
            double v = double(x[f]) * preamp;

            // One pass over the buffer for the whole cascade, rather than one
            // pass per band: the sample stays in a register and the 2.5 kB of
            // coefficients and 2 kB of state per channel stay in L1.
            for (int i = 0; i < sections; ++i) {
                const int k = m_active[i];
                const BiquadCoeffs &s = m_coeff[k];
                double *z = state[k];

                const double y = s.b0 * v + s.b1 * z[0] + s.b2 * z[1]
                                 - s.a1 * z[2] - s.a2 * z[3];
                z[1] = z[0]; z[0] = v;
                z[3] = z[2]; z[2] = y;
                v = y;
            }

            x[f] = railed(v, &bad);
        }

        // Railing the output alone would not be enough: whatever produced the
        // NaN is sitting in a delay element, and every subsequent sample would
        // come out zeroed for the life of the stream. Clearing the channel costs
        // one buffer of audio and makes the failure self-healing.
        if (bad)
            std::memset(state, 0, sizeof m_state[0]);
    }
}

double Equalizer::responseDb(double freqHz) const noexcept
{
    double db = double(m_applied.preampDb);
    for (int i = 0; i < m_activeCount; ++i)
        db += magnitudeDb(m_coeff[m_active[i]], freqHz, m_sampleRate);
    return db;
}

double Equalizer::peakResponseDb() const noexcept
{
    const double top = m_sampleRate * 0.5 * 0.99;
    const double hi = top < 20000.0 ? top : 20000.0;
    const double lo = 20.0;
    if (!(hi > lo))
        return responseDb(lo);

    constexpr int kPoints = 512;
    const double step = std::log(hi / lo) / double(kPoints - 1);

    double peak = -1000.0;
    for (int i = 0; i < kPoints; ++i) {
        const double db = responseDb(lo * std::exp(step * double(i)));
        if (db > peak)
            peak = db;
    }
    return peak;
}

} // namespace dreamdsp::dsp
