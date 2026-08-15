#pragma once

#include "DspTypes.h"
#include "Equalizer.h"

// Equal-loudness compensation: Equalizer APO's `LoudnessCorrection`.
//
// The ear loses low and (less so) high frequencies relative to the midrange as
// the playback level drops, which is why quiet music sounds thin. The
// correction is a low shelf and a high shelf whose gains track how far below a
// reference level you are playing.
//
// Where the volume comes from is the one structural difference from APO. APO
// runs a thread inside audiodg.exe that calls IAudioEndpointVolume, which means
// a COM call and a polling thread inside a system audio process. Here the
// interface reads the endpoint volume -- it already holds an MMDevice for the
// meter -- and publishes it as one more parameter. The DSP stage is then a pure
// function of its parameters like every other stage, and audiodg does no COM.

namespace dreamdsp::dsp {

class LoudnessCorrection
{
public:
    static constexpr int kMaxChannels = 8;

    struct Params {
        // The endpoint's current attenuation, from GetMasterVolumeLevel: 0 at
        // full volume, negative below it. Published by the interface.
        float volumeDb;
        // The volume at which no correction is applied.
        float referenceDb;
        float offsetDb;
        // Scales the whole correction. 1 reproduces Equalizer APO exactly;
        // lower values are the knob for "less of it".
        float amount;
    };

    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;
    void process(const AudioBuffer &buf);

    // What the stage is currently doing, so the interface can show the two
    // shelf gains rather than leaving the user to infer them from a volume
    // slider. Reading these back out of the designed state is what stops the
    // display and the audio from being two separate calculations.
    double lowShelfDb() const noexcept { return m_lowDb; }
    double highShelfDb() const noexcept { return m_highDb; }
    double preampDb() const noexcept { return m_preampDb; }
    bool neutral() const noexcept { return m_neutral; }

    // The shelf gains for a given parameter set, without touching any state.
    // Exposed so the interface can plot the correction before it is switched
    // on, from the same code the audio uses.
    static void shelvesFor(const Params &p, double *lowDb, double *highDb,
                           double *preampDb) noexcept;

private:
    Params m_p{};
    double m_sampleRate = 48000.0;
    int m_channels = 0;

    double m_lowDb = 0.0, m_highDb = 0.0, m_preampDb = 0.0;
    double m_preampLin = 1.0;
    bool m_neutral = true;

    BiquadCoeffs m_low, m_high;
    double m_state[kMaxChannels][2][4]{};   // x1, x2, y1, y2 per shelf
};

} // namespace dreamdsp::dsp
