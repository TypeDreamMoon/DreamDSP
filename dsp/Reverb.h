#pragma once

#include "DspTypes.h"

#include <vector>

namespace dreamdsp::dsp {

// Freeverb (Jezar at Dreampoint, public domain) with a pre-delay and an input
// bandwidth filter added.
//
// Freeverb rather than a Dattorro plate: the plate is the better-sounding
// topology, but its output taps are a long list of specific delay-line
// positions, and reconstructing those from memory is exactly how a reverb ends
// up subtly wrong in a way no test catches. Freeverb's tuning constants are
// short, famous, and verifiable -- eight parallel combs and four series
// allpasses per channel.
//
// The parameters are named after what a user sees, and each maps onto one
// thing in the topology:
//
//   room size   -> comb feedback
//   damping     -> the lowpass inside each comb's feedback path
//   density     -> allpass feedback
//   bandwidth   -> one-pole lowpass on the input, before anything else
//   pre-delay   -> a plain delay line in front
//   width       -> how much the two channels' wet signals are cross-mixed
//   wet / dry   -> output mix
class Reverb
{
public:
    struct Params {
        float roomSize = 0.5f;    // 0..1
        float damping = 0.5f;     // 0..1
        float density = 0.5f;     // 0..1  (allpass feedback)
        float bandwidth = 1.0f;   // 0..1  (1 = full bandwidth in)
        float preDelayMs = 0.0f;  // 0..200
        float width = 1.0f;       // 0..1
        float wet = 0.3f;         // 0..1
        float dry = 1.0f;         // 0..1
    };

    void prepare(double sampleRate);
    void reset();

    void setParams(const Params &p) { m_p = p; updateInternal(); }
    const Params &params() const { return m_p; }

    // Processes in place. Mono is handled by feeding the single channel to
    // both halves and averaging back.
    void process(const AudioBuffer &buf);

private:
    struct Comb {
        std::vector<float> buf;
        int index = 0;
        float store = 0.0f;      // the damping filter's state

        void setSize(int n) { buf.assign(size_t(n < 1 ? 1 : n), 0.0f); index = 0; store = 0.0f; }
        inline float process(float in, float feedback, float damp1, float damp2)
        {
            const float out = buf[size_t(index)];
            store = out * damp2 + store * damp1;
            buf[size_t(index)] = in + store * feedback;
            if (++index >= int(buf.size()))
                index = 0;
            return out;
        }
    };

    struct Allpass {
        std::vector<float> buf;
        int index = 0;

        void setSize(int n) { buf.assign(size_t(n < 1 ? 1 : n), 0.0f); index = 0; }
        inline float process(float in, float feedback)
        {
            const float bufout = buf[size_t(index)];
            const float out = -in + bufout;
            buf[size_t(index)] = in + bufout * feedback;
            if (++index >= int(buf.size()))
                index = 0;
            return out;
        }
    };

    void updateInternal();

    static constexpr int kCombs = 8;
    static constexpr int kAllpasses = 4;

    Params m_p;
    double m_sampleRate = 48000.0;

    Comb m_combL[kCombs], m_combR[kCombs];
    Allpass m_apL[kAllpasses], m_apR[kAllpasses];

    std::vector<float> m_preL, m_preR;
    int m_preIndex = 0;
    int m_preLen = 0;

    float m_inLpL = 0.0f, m_inLpR = 0.0f;

    // Derived from the parameters, recomputed only when they change.
    float m_feedback = 0.0f;
    float m_damp1 = 0.0f, m_damp2 = 0.0f;
    float m_apFeedback = 0.5f;
    float m_bwCoeff = 0.0f;
    float m_wet1 = 0.0f, m_wet2 = 0.0f;
};

} // namespace dreamdsp::dsp
