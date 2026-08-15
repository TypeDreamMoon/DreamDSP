#pragma once

#include "DspTypes.h"

#include <cstdint>

// The parametric equalizer.
//
// This is the stage that lets DreamDSP stand on its own. Until it existed the
// equalizer was emitted as `Filter N:` lines into a text file that Equalizer APO
// executed -- which made APO a hard runtime dependency for the one feature
// nobody installs an equalizer without, and meant the curve drawn in the GUI and
// the curve applied to the audio were produced by two different programs from
// two different pieces of code.
//
// Everything here is double precision, matching Equalizer APO's own BiQuad. At
// 20 Hz on a 192 kHz endpoint a biquad's poles sit at a radius of 0.9993; in
// float the coefficient quantisation of that is audible as a shifted corner
// frequency, and the state quantisation as a raised noise floor. The cost is
// about 1 Mflop per 10 ms buffer for 32 bands across 8 channels.

namespace dreamdsp::dsp {

// Peace stores this as an integer in a .peace file's [Filters] section and
// DreamDSP puts it on the wire as a byte, so the values are fixed. Do not
// reorder or insert.
enum class FilterKind : uint8_t {
    PK = 0,   // peaking
    LPQ,      // low-pass with Q
    HPQ,      // high-pass with Q
    BP,       // band-pass (constant peak gain)
    LS,       // low shelf,  no Q  (slope S = 0.9)
    HS,       // high shelf, no Q
    NO,       // notch
    AP,       // all-pass
    LSC,      // low shelf,  Fc is the centre frequency
    HSC,      // high shelf, Fc is the centre frequency
    BWLP,     // Butterworth low-pass,      4th order
    BWHP,     // Butterworth high-pass,     4th order
    LRLP,     // Linkwitz-Riley low-pass,   4th order
    LRHP,     // Linkwitz-Riley high-pass,  4th order
    LSCQ,     // low shelf,  centre frequency, with Q
    HSCQ,     // high shelf, centre frequency, with Q
    LSQ,      // low shelf,  Fc is the corner frequency, with Q
    HSQ,      // high shelf, Fc is the corner frequency, with Q
    Count
};

// The APO config token. Note that BWLP/BWHP/LRLP/LRHP are Peace inventions:
// Equalizer APO 1.4.2's BiQuadFilterFactory maps only PK/PEQ/Modal/LP/HP/LPQ/
// HPQ/BP/LS/HS/LSC/HSC/NO/AP, and logs anything else as an invalid filter type
// before dropping it. A DreamDSP band of one of those four types therefore did
// nothing at all while APO was the one processing, and now does what its name
// says -- see designFilter() for the alignment chosen.
const char *filterToken(FilterKind kind) noexcept;

// Whether the type's gain and Q are meaningful. A gainless type ignores the
// band's gain entirely rather than clamping it, so that switching a band to
// LPQ and back does not lose the gain the user had set.
bool filterHasGain(FilterKind kind) noexcept;
bool filterHasQ(FilterKind kind) noexcept;

// Normalised biquad coefficients; a0 is folded into the rest.
struct BiquadCoeffs {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

// A designed filter. One section for the second-order types, two for the
// fourth-order Butterworth and Linkwitz-Riley ones -- which is the entire
// reason this is not just a BiquadCoeffs.
struct FilterSections {
    int count = 0;
    BiquadCoeffs section[2];
};

// Designs `kind` at the given sample rate. Never returns an unstable filter:
// any section whose poles land on or outside the unit circle is replaced by a
// pass-through, because the alternative is an exponentially growing signal
// inside audiodg.exe.
//
// Real-time safe (transcendentals only, no allocation), but not free: about a
// microsecond. Callers redesign only the bands that actually changed.
FilterSections designFilter(FilterKind kind,
                            double freqHz,
                            double gainDb,
                            double q,
                            double sampleRate) noexcept;

double magnitudeDb(const BiquadCoeffs &c, double freqHz, double sampleRate) noexcept;
double magnitudeDb(const FilterSections &f, double freqHz, double sampleRate) noexcept;

// ---------------------------------------------------------------------------

// One band on the wire. No default member initialisers, deliberately: an
// all-zero EqBand has to mean "an unused slot", so that a memset of the
// parameter block and value-initialisation of it agree byte for byte.
struct EqBand {
    float freqHz;
    float gainDb;
    float q;
    uint8_t type;      // FilterKind
    uint8_t enabled;
    uint16_t reserved;
};

static_assert(sizeof(EqBand) == 16, "wire layout");
static_assert(alignof(EqBand) == 4, "wire layout");

class Equalizer
{
public:
    // 32 is above anything a person builds by hand and above every AutoEQ
    // parametric result (which top out at 10); Peace's own maximum is 31.
    static constexpr int kMaxBands = 32;
    static constexpr int kMaxChannels = 8;
    static constexpr int kMaxSections = kMaxBands * 2;

    struct Params {
        float preampDb = 0.0f;
        uint32_t bandCount = 0;
        EqBand band[kMaxBands];
    };

    // Not real-time. Redesigns everything against the new rate.
    void prepare(double sampleRate, int channels);

    // Clears the filter state without touching the coefficients. Not free --
    // 16 kB of memset -- but allocation free.
    void reset() noexcept;

    // Real-time safe. Redesigns only the bands whose parameters differ from the
    // ones already installed, so dragging one slider costs one design rather
    // than thirty-two.
    void setParams(const Params &p) noexcept;

    void process(const AudioBuffer &buf);

    // The magnitude the running filter applies at `freqHz`, preamp included.
    // This is what the GUI plots: the curve on screen is read out of the same
    // coefficients the audio goes through, so the two cannot drift apart.
    double responseDb(double freqHz) const noexcept;

    // The largest responseDb() over a log sweep of the audible band. What
    // "auto preamp" needs: negate it and no band can push the signal above the
    // level it came in at.
    double peakResponseDb() const noexcept;

    int activeSections() const noexcept { return m_activeCount; }

private:
    void design(int band) noexcept;
    void rebuildActive() noexcept;

    double m_sampleRate = 48000.0;
    int m_channels = 0;

    Params m_applied{};

    // Coefficients are shared across channels; only the delay state is per
    // channel. Indexed by a flat section number, band * 2 + section, which
    // stays put as bands are enabled and disabled -- so a band that comes back
    // finds its own state rather than a neighbour's.
    BiquadCoeffs m_coeff[kMaxSections];
    uint8_t m_sectionCount[kMaxBands]{};

    // The sections to actually run, in order, with the disabled ones compacted
    // out. Rebuilt whenever the enable pattern changes so that the innermost
    // loop has no branch in it.
    uint8_t m_active[kMaxSections]{};
    int m_activeCount = 0;

    // x1, x2, y1, y2 per section, per channel. Direct form I: the numerator
    // runs on the input history, which is what makes a coefficient change
    // mid-stream well behaved rather than a step in the feedback path.
    double m_state[kMaxChannels][kMaxSections][4]{};

    float m_preampLin = 1.0f;
};

static_assert(sizeof(Equalizer::Params) == 520, "wire layout; bump kParamVersion");
static_assert(alignof(Equalizer::Params) == 4, "wire layout; bump kParamVersion");

} // namespace dreamdsp::dsp
