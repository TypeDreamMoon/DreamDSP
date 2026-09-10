#pragma once

#include "DspTypes.h"

namespace dreamdsp::dsp {

// A transient shaper: attack and sustain, independently, with no threshold.
//
// This is not a compressor with different knobs. A compressor's gain is a
// function of `level - threshold`, so a ghost note gets no processing and an
// accent gets a lot; the same drum hit 10 dB louder is treated differently.
// Here the gain is a function of the *difference between two envelopes of the
// same signal*, which is a ratio in the linear domain and therefore invariant
// under scaling: pianissimo and fortissimo get identical treatment. That is
// the whole idea, and it is why there is no threshold control.
//
// The structure follows SPL's published description of Differential Envelope
// Technology, which is four followers in two independent pairs:
//
//   attack   Env1 = fast attack, shared release      d = Env1 - Env2  (>= 0 at onsets)
//            Env2 = slow attack, shared release
//   sustain  Env3 = shared attack, fast release      d = Env4 - Env3  (>= 0 in decay)
//            Env4 = shared attack, slow release      (Env4 is a peak hold)
//
// SPL never published the time constants -- their manual says they are adaptive
// -- so the ones here come from the implementations that did publish: Calf's
// 1 ms / 200 ms reference envelope and 30 ms / 300 ms defaults, and Gruhn's
// three-follower ratios. There is no patent to work around: SPL Electronics
// holds exactly one patent family and it is the Vitalizer's filter, not this.
//
// Everything runs in dB. Working on linear envelopes and calling the difference
// a gain -- which is what the most-copied textbook implementation does -- is
// not level-independent at all: the difference scales with amplitude, so the
// threshold behaviour comes straight back in through the side door.
class TransientShaper
{
public:
    static constexpr int kMaxChannels = 8;

    struct Params {
        float attack = 0.0f;        // -1 .. +1  (-1 softens onsets, +1 sharpens)
        float sustain = 0.0f;       // -1 .. +1  (-1 shortens decay, +1 lengthens)
        float attackMs = 30.0f;     // 1 .. 500   the slow follower of the attack pair
        float releaseMs = 300.0f;   // 1 .. 5000  the slow follower of the sustain pair
        // Below this the gain is frozen at unity. Level independence has a
        // sharp edge: a ratio of envelopes is scale-invariant right down to the
        // detector's own floor, at which point it starts chasing the room tone
        // between hits just as hard as it chases the hits. Measured on white
        // noise, the gain range is identical at -75 dBFS and at -30 dBFS. So a
        // gate is not a betrayal of the "no threshold" idea, it is the price of
        // it, and every shipping implementation has one whether or not it is on
        // the panel.
        float thresholdDb = -60.0f; // -90 .. -20
    };

    void prepare(double sampleRate, int channels);
    void reset() noexcept;
    void setParams(const Params &p) noexcept;

    void process(const AudioBuffer &buf);

    // Current gain in dB, for a meter.
    float gainDb() const noexcept { return m_gainDb; }

    // The most either path may move the signal. SPL's hardware allows +/-15 dB
    // of attack and +/-24 dB of sustain; this is the sum of the two paths and
    // is deliberately the smaller number, because a system-wide effect that can
    // add 39 dB to a snare is not a feature.
    static constexpr float kMaxGainDb = 18.0f;

private:
    // Branching one-pole in the dB domain: separate rise and fall coefficients.
    struct Follower {
        float y = 0.0f;
        float aUp = 0.0f, aDown = 0.0f;
        inline float process(float x) noexcept
        {
            const float a = (x > y) ? aUp : aDown;
            y += (x - y) * a;
            return y;
        }
    };

    Params m_p;
    double m_sampleRate = 48000.0;
    int m_channels = 0;

    // One detector for the whole stream, driven by the loudest channel.
    // Independent per-channel detectors would move the stereo image on every
    // hit, which is why nothing that ships does it.
    Follower m_env1, m_env2, m_env3, m_env4;

    // Set on the first sample after a reset. Seeding the followers with a
    // fixed decibel value would make the stage's first few hundred
    // milliseconds depend on how far the signal happened to be from that
    // value -- which is level dependence, reintroduced at the one moment the
    // stage has no history to hide it.
    bool m_primed = false;
    float m_gainDb = 0.0f;
    // Slew limit on the applied gain, in dB per sample. Calf uses x4 per
    // millisecond; without it, the linear-in-dB gain law clicks on sharp
    // onsets.
    float m_maxStepDb = 1.0f;
    float m_floorDb = -60.0f;
    float m_attackAmount = 0.0f, m_sustainAmount = 0.0f;
    bool m_ready = false;
};

} // namespace dreamdsp::dsp
