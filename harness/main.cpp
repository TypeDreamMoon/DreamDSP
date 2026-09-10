// Offline exerciser for the DSP layer.
//
//   dspharness --test              run the assertions, exit code = failures
//   dspharness in.wav out.wav      compress a file with the default settings
//
// The point of this binary is that DSP code gets proven here, on synthetic
// signals with closed-form expected answers, long before it is loaded into a
// live audio graph where a mistake costs the machine its sound.

#include "BiquadFilter.h"
#include "Compressor.h"
#include "Convolver.h"
#include "Denormals.h"
#include "EffectChain.h"
#include "BassBoost.h"
#include "Equalizer.h"
#include "GraphicEq.h"
#include "AutoGain.h"
#include "Clipper.h"
#include "Limiter.h"
#include "LoudnessMeter.h"
#include "Fft.h"
#include "Loudness.h"
#include "ImpulseAnalysis.h"
#include "ImpulseBlob.h"
#include "MultibandCompressor.h"
#include "ParamBlock.h"
#include "ParamSlots.h"
#include "Resampler.h"
#include "Reverb.h"
#include "Routing.h"
#include "Saturation.h"
#include "StatusBlock.h"
#include "Stereo.h"
#include "Transient.h"
#include "WavFile.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <limits>
#include <thread>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Counting allocator hooks, so "the real-time path does not allocate" can be
// asserted rather than asserted-in-a-comment. Replacing the global operators is
// the only way to see allocations made deep inside std::vector or std::function
// where no test can reach.
namespace {
std::atomic<long> g_allocations{ 0 };
bool g_countAllocations = false;
}

void *operator new(std::size_t n)
{
    if (g_countAllocations)
        g_allocations.fetch_add(1, std::memory_order_relaxed);
    void *p = std::malloc(n ? n : 1);
    if (!p)
        throw std::bad_alloc();
    return p;
}

void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

using namespace dreamdsp::dsp;

namespace {

int g_failures = 0;

void check(bool ok, const std::string &what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok)
        ++g_failures;
}

std::string fmt(const char *f, double a, double b = 0, double c = 0)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), f, a, b, c);
    return buf;
}

// A cheap deterministic generator, so a failure can be reproduced exactly.
uint32_t xorshift(uint32_t &s)
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

// Runs a mono signal through a compressor and returns it.
std::vector<float> run(Compressor &comp, std::vector<float> signal, double sr)
{
    float *ch[1] = { signal.data() };
    AudioBuffer buf{ ch, 1, int(signal.size()) };
    (void)sr;
    comp.process(buf);
    return signal;
}

// ---------------------------------------------------------------- static curve

void testStaticCurve()
{
    std::printf("\n[compressor: static curve]\n");

    Compressor c;
    c.prepare(48000.0, 2);

    Compressor::Params p;
    p.thresholdDb = -20.0f;
    p.ratio = 4.0f;
    p.kneeDb = 0.0f;          // hard knee: the curve is exactly two lines
    c.setParams(p);

    // Below the threshold nothing happens at all.
    check(std::fabs(c.outputForInputDb(-40.0f) - (-40.0f)) < 1e-4,
          fmt("-40 dB in -> %.4f dB out (want -40)", c.outputForInputDb(-40.0f)));
    check(std::fabs(c.outputForInputDb(-20.0f) - (-20.0f)) < 1e-4,
          fmt("at threshold -> %.4f dB (want -20)", c.outputForInputDb(-20.0f)));

    // 20 dB over threshold at 4:1 must come out 5 dB over.
    check(std::fabs(c.outputForInputDb(0.0f) - (-15.0f)) < 1e-4,
          fmt("0 dB in at 4:1 -> %.4f dB (want -15)", c.outputForInputDb(0.0f)));
    check(std::fabs(c.outputForInputDb(-10.0f) - (-17.5f)) < 1e-4,
          fmt("-10 dB in -> %.4f dB (want -17.5)", c.outputForInputDb(-10.0f)));

    // Ratio 1 must be a straight wire.
    p.ratio = 1.0f;
    c.setParams(p);
    bool unity = true;
    for (float x = -60.0f; x <= 0.0f; x += 1.0f)
        if (std::fabs(c.outputForInputDb(x) - x) > 1e-4) unity = false;
    check(unity, "ratio 1:1 is a straight wire across -60..0 dB");

    // A high ratio approaches limiting: output barely rises above threshold.
    p.ratio = 100.0f;
    c.setParams(p);
    check(c.outputForInputDb(0.0f) < -19.5f,
          fmt("100:1 holds 0 dB input down to %.2f dB", c.outputForInputDb(0.0f)));
}

void testSoftKnee()
{
    std::printf("\n[compressor: soft knee]\n");

    Compressor c;
    c.prepare(48000.0, 2);

    Compressor::Params p;
    p.thresholdDb = -20.0f;
    p.ratio = 4.0f;
    p.kneeDb = 10.0f;
    c.setParams(p);

    // Outside the knee the soft curve must coincide with the hard one.
    check(std::fabs(c.outputForInputDb(-30.0f) - (-30.0f)) < 1e-4,
          "below the knee the curve is untouched");
    check(std::fabs(c.outputForInputDb(0.0f) - (-15.0f)) < 1e-4,
          "above the knee the curve is the plain ratio line");

    // At the centre of the knee, reduction is a quarter of the hard-knee case:
    // (1-1/R)*(W/2)^2/(2W) = (1-1/R)*W/8.
    const float expected = -20.0f - (1.0f - 1.0f / 4.0f) * 10.0f / 8.0f;
    check(std::fabs(c.outputForInputDb(-20.0f) - expected) < 1e-3,
          fmt("at threshold with a 10 dB knee -> %.4f (want %.4f)",
              c.outputForInputDb(-20.0f), expected));

    // Continuity: no step anywhere across the knee.
    float worst = 0.0f, prev = c.outputForInputDb(-40.0f);
    for (float x = -40.0f; x <= 5.0f; x += 0.05f) {
        const float y = c.outputForInputDb(x);
        worst = std::max(worst, std::fabs(y - prev));
        prev = y;
    }
    check(worst < 0.06f, fmt("curve is continuous, largest step %.4f dB", worst));

    // Monotonic: more in must never mean less out.
    bool mono = true;
    prev = -1e9f;
    for (float x = -60.0f; x <= 6.0f; x += 0.1f) {
        const float y = c.outputForInputDb(x);
        if (y < prev - 1e-4f) mono = false;
        prev = y;
    }
    check(mono, "curve is monotonic");
}

// ------------------------------------------------------------------- timing

void testTiming()
{
    std::printf("\n[compressor: timing]\n");

    constexpr double sr = 48000.0;
    Compressor c;
    c.prepare(sr, 1);

    Compressor::Params p;
    p.thresholdDb = -20.0f;
    p.ratio = 4.0f;
    p.kneeDb = 0.0f;
    p.attackMs = 10.0f;
    p.releaseMs = 100.0f;
    c.setParams(p);
    c.reset();

    // A step from silence to 0 dBFS. Steady-state reduction is 15 dB.
    const int n = int(sr * 0.5);
    std::vector<float> sig(size_t(n), 1.0f);
    float *ch[1] = { sig.data() };

    // Feed it one sample at a time so the envelope can be sampled.
    // Two arguments on purpose: `vector<float> v(size_t(n));` parses as a
    // function declaration (the most vexing parse), not a variable.
    std::vector<float> reduction(size_t(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        float s = 1.0f;
        float *one[1] = { &s };
        AudioBuffer b{ one, 1, 1 };
        c.process(b);
        reduction[size_t(i)] = -c.gainReductionDb();
    }
    (void)ch;

    const float finalDb = reduction[size_t(n - 1)];
    check(std::fabs(finalDb - 15.0f) < 0.1f,
          fmt("steady-state reduction %.3f dB (want 15)", finalDb));

    // One time constant should land at 1 - 1/e = 63.2% of the target.
    const int atAttack = int(sr * 0.010);
    const float frac = reduction[size_t(atAttack)] / finalDb;
    check(std::fabs(frac - 0.632f) < 0.03f,
          fmt("after one 10 ms attack, %.1f%% of target (want 63.2%%)", frac * 100.0));

    // Now drop to silence and watch the release.
    std::vector<float> rel(size_t(n), 0.0f);
    for (int i = 0; i < n; ++i) {
        float s = 0.0f;
        float *one[1] = { &s };
        AudioBuffer b{ one, 1, 1 };
        c.process(b);
        rel[size_t(i)] = -c.gainReductionDb();
    }
    const int atRelease = int(sr * 0.100);
    const float remaining = rel[size_t(atRelease)] / finalDb;
    check(std::fabs(remaining - 0.368f) < 0.03f,
          fmt("after one 100 ms release, %.1f%% remains (want 36.8%%)", remaining * 100.0));
}

void testAutoMakeup()
{
    std::printf("\n[compressor: auto makeup]\n");

    Compressor c;
    c.prepare(48000.0, 2);

    Compressor::Params p;
    p.thresholdDb = -30.0f;
    p.ratio = 3.0f;
    p.autoMakeup = true;
    c.setParams(p);

    check(std::fabs(c.outputForInputDb(0.0f)) < 1e-3,
          fmt("full-scale input stays at %.4f dBFS (want 0)", c.outputForInputDb(0.0f)));
    check(c.outputForInputDb(-60.0f) > -60.0f,
          "quiet material is lifted, which is the point of makeup");
}

void testStereoLink()
{
    std::printf("\n[compressor: stereo link]\n");

    Compressor c;
    c.prepare(48000.0, 2);
    Compressor::Params p;
    p.thresholdDb = -20.0f;
    p.ratio = 8.0f;
    p.attackMs = 0.01f;
    p.releaseMs = 1.0f;
    c.setParams(p);
    c.reset();

    // Loud left, quiet right. Both channels must be scaled by the same amount
    // or the stereo image shifts whenever one side gets loud.
    const int n = 4096;
    std::vector<float> L(size_t(n), 1.0f), R(size_t(n), 0.01f);
    const float ratioBefore = R[0] / L[0];

    float *ch[2] = { L.data(), R.data() };
    AudioBuffer b{ ch, 2, n };
    c.process(b);

    const float ratioAfter = R[size_t(n - 1)] / L[size_t(n - 1)];
    check(std::fabs(ratioAfter - ratioBefore) < 1e-4,
          fmt("L/R ratio preserved: %.6f -> %.6f", ratioBefore, ratioAfter));
    check(L[size_t(n - 1)] < 0.9f, "the loud channel was actually reduced");
}

void testSilenceAndSanity()
{
    std::printf("\n[compressor: sanity]\n");

    Compressor c;
    c.prepare(48000.0, 2);
    Compressor::Params p;
    p.thresholdDb = -20.0f;
    p.ratio = 4.0f;
    p.autoAttack = true;
    p.autoRelease = true;
    c.setParams(p);
    c.reset();

    // Digital silence must not produce NaN through the log detector.
    std::vector<float> sig(8192, 0.0f);
    auto out = run(c, sig, 48000.0);
    bool finite = true;
    for (float v : out)
        if (!std::isfinite(v)) finite = false;
    check(finite, "silence stays finite (log detector is floored)");

    // Full-scale square wave: output must not exceed sane bounds.
    c.reset();
    std::vector<float> sq(48000);
    for (size_t i = 0; i < sq.size(); ++i)
        sq[i] = (i / 128) % 2 ? 1.0f : -1.0f;
    auto sqOut = run(c, sq, 48000.0);
    float peak = 0.0f;
    bool ok = true;
    for (float v : sqOut) {
        if (!std::isfinite(v)) ok = false;
        peak = std::max(peak, std::fabs(v));
    }
    check(ok && peak <= 1.0f + 1e-4f,
          fmt("full-scale square stays finite and <= 1.0 (peak %.4f)", peak));
}

// ------------------------------------------------------------------- reverb

// Energy of an impulse response in a window, in dB.
double windowDb(const std::vector<float> &x, size_t from, size_t len)
{
    double sum = 0.0;
    const size_t end = std::min(from + len, x.size());
    for (size_t i = from; i < end; ++i)
        sum += double(x[i]) * double(x[i]);
    const size_t n = (end > from) ? (end - from) : 1;
    return 10.0 * std::log10(sum / double(n) + 1e-20);
}

std::vector<float> reverbImpulse(Reverb::Params p, double sr, int seconds = 4)
{
    Reverb r;
    r.prepare(sr);
    r.setParams(p);
    r.reset();

    const int n = int(sr) * seconds;
    std::vector<float> L(size_t(n), 0.0f), R(size_t(n), 0.0f);
    L[0] = 1.0f;
    R[0] = 1.0f;

    float *ch[2] = { L.data(), R.data() };
    AudioBuffer buf{ ch, 2, n };
    r.process(buf);
    return L;
}

void testReverb()
{
    std::printf("\n[reverb]\n");
    constexpr double sr = 48000.0;

    // Fully dry must be bit-exact: a "reverb off" that still colours the signal
    // is a bug you only notice after chasing it for an hour.
    {
        Reverb r;
        r.prepare(sr);
        Reverb::Params p;
        p.wet = 0.0f; p.dry = 1.0f;
        r.setParams(p);
        r.reset();

        std::vector<float> L(4096), R(4096), refL(4096);
        for (size_t i = 0; i < L.size(); ++i) {
            L[i] = refL[i] = std::sin(float(i) * 0.05f);
            R[i] = L[i];
        }
        float *ch[2] = { L.data(), R.data() };
        AudioBuffer b{ ch, 2, int(L.size()) };
        r.process(b);

        float worst = 0.0f;
        for (size_t i = 0; i < L.size(); ++i)
            worst = std::max(worst, std::fabs(L[i] - refL[i]));
        check(worst == 0.0f, fmt("wet=0 is bit-exact (largest deviation %.2e)", worst));
    }

    // The tail has to decay, and keep decaying.
    {
        Reverb::Params p;
        p.roomSize = 0.7f; p.damping = 0.4f; p.wet = 1.0f; p.dry = 0.0f;
        const auto ir = reverbImpulse(p, sr);

        const double e0 = windowDb(ir, size_t(sr * 0.05), size_t(sr * 0.1));
        const double e1 = windowDb(ir, size_t(sr * 0.50), size_t(sr * 0.1));
        const double e2 = windowDb(ir, size_t(sr * 1.50), size_t(sr * 0.1));
        std::printf("    energy at 50 ms / 500 ms / 1.5 s: %.1f / %.1f / %.1f dB\n", e0, e1, e2);
        check(e1 < e0 && e2 < e1, "the tail decays monotonically");

        double peak = 0.0;
        bool finite = true;
        for (float v : ir) {
            if (!std::isfinite(v)) finite = false;
            peak = std::max(peak, double(std::fabs(v)));
        }
        check(finite, "impulse response stays finite over 4 s");
        check(peak < 4.0, fmt("no runaway feedback (peak %.3f)", peak));
    }

    // A bigger room must ring longer. This is the single check that would catch
    // the feedback coefficient being wired backwards.
    {
        Reverb::Params small, big;
        small.wet = big.wet = 1.0f;
        small.dry = big.dry = 0.0f;
        small.damping = big.damping = 0.2f;
        small.roomSize = 0.1f;
        big.roomSize = 0.95f;

        const auto irS = reverbImpulse(small, sr);
        const auto irB = reverbImpulse(big, sr);
        const double sDb = windowDb(irS, size_t(sr * 1.0), size_t(sr * 0.2));
        const double bDb = windowDb(irB, size_t(sr * 1.0), size_t(sr * 0.2));
        std::printf("    energy at 1 s: small room %.1f dB, big room %.1f dB\n", sDb, bDb);
        check(bDb > sDb + 6.0, "a bigger room rings substantially longer");
    }

    // Damping must remove high frequencies from the tail, not just level.
    {
        Reverb::Params dry_, damped;
        dry_.wet = damped.wet = 1.0f;
        dry_.dry = damped.dry = 0.0f;
        dry_.roomSize = damped.roomSize = 0.8f;
        dry_.damping = 0.0f;
        damped.damping = 1.0f;

        const auto irA = reverbImpulse(dry_, sr);
        const auto irB = reverbImpulse(damped, sr);

        // Crude HF measure: mean |x[n] - x[n-1]| over the tail.
        const auto hf = [](const std::vector<float> &x, size_t from, size_t len) {
            double s = 0.0;
            const size_t end = std::min(from + len, x.size());
            for (size_t i = from + 1; i < end; ++i)
                s += std::fabs(double(x[i]) - double(x[i - 1]));
            return s / double(end - from);
        };
        const double a = hf(irA, size_t(sr * 0.5), size_t(sr * 0.3));
        const double b = hf(irB, size_t(sr * 0.5), size_t(sr * 0.3));
        std::printf("    tail high-frequency content: undamped %.2e, damped %.2e\n", a, b);
        check(b < a, "damping removes high frequencies from the tail");
    }

    // Pre-delay must actually delay the onset of the wet signal.
    {
        Reverb::Params p;
        p.wet = 1.0f; p.dry = 0.0f; p.roomSize = 0.5f;
        p.preDelayMs = 50.0f;
        const auto ir = reverbImpulse(p, sr, 2);

        const double before = windowDb(ir, 0, size_t(sr * 0.04));
        const double after = windowDb(ir, size_t(sr * 0.06), size_t(sr * 0.04));
        std::printf("    before/after a 50 ms pre-delay: %.1f / %.1f dB\n", before, after);
        check(after > before + 20.0, "pre-delay holds the wet signal back");
    }

    // The two channels must not be identical, or the reverb is mono.
    {
        Reverb r;
        r.prepare(sr);
        Reverb::Params p;
        p.wet = 1.0f; p.dry = 0.0f; p.width = 1.0f; p.roomSize = 0.6f;
        r.setParams(p);
        r.reset();

        const int n = int(sr);
        std::vector<float> L(size_t(n), 0.0f), R(size_t(n), 0.0f);
        L[0] = R[0] = 1.0f;
        float *ch[2] = { L.data(), R.data() };
        AudioBuffer b{ ch, 2, n };
        r.process(b);

        double diff = 0.0;
        for (int i = 0; i < n; ++i)
            diff += std::fabs(double(L[size_t(i)]) - double(R[size_t(i)]));
        check(diff > 1.0, fmt("left and right decorrelate (total difference %.2f)", diff));
    }
}

// ------------------------------------------------------------ spectrum tools

// Magnitude at one frequency by direct Goertzel-style correlation. Enough to
// answer "is there a second harmonic", which is what these tests need.
double magnitudeAt(const std::vector<float> &x, double freq, double sr,
                   size_t from = 0, size_t len = 0)
{
    const size_t end = len ? std::min(from + len, x.size()) : x.size();
    double re = 0.0, im = 0.0;
    const double w = 2.0 * 3.14159265358979323846 * freq / sr;
    for (size_t i = from; i < end; ++i) {
        re += double(x[i]) * std::cos(w * double(i));
        im += double(x[i]) * std::sin(w * double(i));
    }
    const double n = double(end > from ? end - from : 1);
    return 2.0 * std::sqrt(re * re + im * im) / n;
}

std::vector<float> sine(double freq, double sr, int n, float amp = 0.5f)
{
    std::vector<float> v(size_t(n), 0.0f);
    for (int i = 0; i < n; ++i)
        v[size_t(i)] = amp * std::sin(2.0 * 3.14159265358979323846 * freq * i / sr);
    return v;
}

// ------------------------------------------------------------------ crossover

void testCrossover()
{
    std::printf("\n[crossover]\n");
    constexpr double sr = 48000.0;

    LinkwitzRiley4 lr;
    lr.design(1000.0, sr);

    // The property that makes a multiband processor usable: low + high must
    // reconstruct the input with flat magnitude.
    const int n = 32768;
    double worstDb = 0.0;
    double worstFreq = 0.0;
    for (double f : { 50.0, 200.0, 500.0, 900.0, 1000.0, 1100.0, 2000.0, 5000.0, 12000.0 }) {
        lr.reset();
        const auto in = sine(f, sr, n);
        std::vector<float> summed(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i) {
            float lo = 0.0f, hi = 0.0f;
            lr.process(in[size_t(i)], &lo, &hi);
            summed[size_t(i)] = lo + hi;
        }
        // Skip the settling transient.
        const double a = magnitudeAt(in, f, sr, size_t(n / 2));
        const double b = magnitudeAt(summed, f, sr, size_t(n / 2));
        const double db = 20.0 * std::log10((b + 1e-12) / (a + 1e-12));
        if (std::fabs(db) > std::fabs(worstDb)) { worstDb = db; worstFreq = f; }
    }
    check(std::fabs(worstDb) < 0.35,
          fmt("LR4 bands sum flat: worst %.3f dB at %.0f Hz", worstDb, worstFreq));
}

// ----------------------------------------------------------------- saturation

// --------------------------------------------------- dry/wet time alignment

// A parallel-mixed nonlinear stage must not comb-filter itself.
//
// The oversampler is an 8th-order Butterworth run on both the interpolation and
// the decimation pass, so the wet path comes back with a few samples of group
// delay. The dry path has none. Summing them is then a comb filter, and at the
// default mix of 0.5 the null is nearly total -- a cancellation in the presence
// region that no amount of tuning the drive would explain.
//
// Measured in the linear regime (tiny signal, low drive) where the shaper is
// effectively a wire, so anything that is not flat is the alignment and not the
// distortion.
void testDryWetAlignment()
{
    std::printf("\n[dry/wet alignment]\n");

    constexpr double sr = 48000.0;
    Fft fft(8192);
    const int len = fft.realSize();

    // Impulse response magnitude, in dB, bin by bin. The impulse is tiny so
    // every stage stays in its linear regime -- this measures the *structure*,
    // not the distortion.
    const auto response = [&](auto &&configure) {
        std::vector<float> imp(size_t(len), 0.0f);
        imp[0] = 1e-4f;
        float *ch[1] = { imp.data() };
        AudioBuffer b{ ch, 1, len };
        configure(b);

        std::vector<std::complex<float>> spec(size_t(fft.realBins()));
        fft.realForward(imp.data(), spec.data());

        std::vector<double> db(spec.size(), 0.0);
        for (size_t k = 0; k < spec.size(); ++k)
            db[k] = 20.0 * std::log10(double(std::abs(spec[k])) / 1e-4 + 1e-20);
        return db;
    };

    const auto binHz = [&](size_t k) { return double(k) * sr / double(len); };

    // -- TubeStage. `mix` is a genuine dry/wet control, so the guarantee is
    // exact: a parallel blend of two paths must never be quieter than the
    // quieter of the two. Anything below that is cancellation.
    {
        const auto tube = [&](float mix) {
            return response([&](const AudioBuffer &b) {
                TubeStage t;
                t.prepare(sr, 1);
                TubeStage::Params p;
                p.drive = 1.0f; p.bias = 0.0f; p.mix = mix; p.outputDb = 0.0f;
                t.setParams(p);
                t.process(b);
            });
        };

        const auto dry = tube(0.0f);
        const auto wet = tube(1.0f);

        double worst = 0.0, worstAt = 0.0;
        float worstMix = 0.0f;
        for (float mix : { 0.25f, 0.5f, 0.75f }) {
            const auto mid = tube(mix);
            for (size_t k = 1; k < mid.size(); ++k) {
                const double hz = binHz(k);
                if (hz < 50.0 || hz > 20000.0)
                    continue;
                const double floorDb = std::min(dry[k], wet[k]);
                const double margin = mid[k] - floorDb;
                if (margin < worst) { worst = margin; worstAt = hz; worstMix = mix; }
            }
        }
        std::printf("    tube: worst blend loss %.2f dB at %.0f Hz (mix %.2f)\n",
                    worst, worstAt, worstMix);
        check(worst > -0.25,
              fmt("a partly-wet tube stage never cancels against itself (%.2f dB at %.0f Hz)",
                  worst, worstAt));
    }

    // -- Exciter. `amount` is additive, not a blend, and the stage genuinely
    // does dip near its corner: adding a 4th-order high-pass copy of the
    // signal subtracts where that filter's phase passes 180 degrees. That dip
    // is the design, and it lives at the corner. The oversampler comb, if it
    // came back, would sit an octave or two below Nyquist -- so measure well
    // clear of the corner and require the band to be lifted, not notched.
    {
        const auto exc = [&](float amount) {
            return response([&](const AudioBuffer &b) {
                Exciter e;
                e.prepare(sr, 1);
                Exciter::Params p;
                p.frequencyHz = 3500.0f; p.drive = 3.0f; p.amount = amount;
                // Below any signal, so the shaper is always engaged. This test
                // is about the *structure* -- whether the added band lines up
                // with the dry one -- and the transient threshold would
                // otherwise gate a -80 dBFS impulse away entirely.
                p.thresholdDb = -120.0f;
                e.setParams(p);
                e.process(b);
            });
        };

        const auto off = exc(0.0f);
        const auto on = exc(0.3f);

        double worst = 1e9, worstAt = 0.0;
        for (size_t k = 1; k < on.size(); ++k) {
            const double hz = binHz(k);
            if (hz < 8000.0 || hz > 20000.0)
                continue;
            const double lift = on[k] - off[k];
            if (lift < worst) { worst = lift; worstAt = hz; }
        }
        std::printf("    exciter: least lift above 8 kHz %+.2f dB at %.0f Hz\n", worst, worstAt);
        check(worst > 0.0,
              fmt("the exciter lifts its whole band rather than notching it (%+.2f dB at %.0f Hz)",
                  worst, worstAt));
    }
}


// ---------------------------------------------------------------- new stages

void testLoudnessMeter()
{
    std::printf("\n[loudness meter]\n");
    constexpr double sr = 48000.0;

    // The published coefficient tables are the external check on the design
    // code. BS.1770-4 tabulates 48 kHz only; the design constants cover every
    // rate, and this asserts that they reproduce the tabulated case exactly.
    {
        KWeighting k;
        k.prepare(sr);
        double b[3], a[3], hp[3];
        k.coefficients(b, a, hp);

        const double refB[3] = { 1.53512485958697, -2.69169618940638, 1.19839281085285 };
        const double refA[3] = { 1.0, -1.69065929318241, 0.73248077421585 };
        const double refH[3] = { 1.0, -1.99004745483398, 0.99007225036621 };

        double worst = 0.0;
        for (int i = 0; i < 3; ++i) {
            worst = std::max(worst, std::fabs(b[i] - refB[i]));
            worst = std::max(worst, std::fabs(a[i] - refA[i]));
            worst = std::max(worst, std::fabs(hp[i] - refH[i]));
        }
        std::printf("    K-weighting vs BS.1770-4 tables at 48 kHz: worst %.2e\n", worst);
        check(worst < 5e-12,
              fmt("the designed coefficients are the published ones (%.2e)", worst));
    }

    // Level scaling and channel summing, which is all a meter has to promise
    // beyond its filter.
    {
        const int n = 48000 * 4;
        const auto measure = [&](float amp, int channels) {
            LoudnessMeter m;
            m.prepare(sr, channels, 3000.0);
            auto a = sine(1000.0, sr, n, amp);
            auto b = a;
            float *ch[2] = { a.data(), b.data() };
            AudioBuffer buf{ ch, channels, n };
            m.process(buf);
            return m.lufs();
        };
        const double mono = measure(0.5f, 1);
        const double louder = measure(1.0f, 1);
        const double stereo = measure(0.5f, 2);
        std::printf("    1 kHz mono %.2f LUFS, +6 dB -> %.2f, same in stereo -> %.2f\n",
                    mono, louder, stereo);
        check(std::fabs((louder - mono) - 6.0206) < 0.01,
              "six decibels in is six decibels out");
        check(std::fabs((stereo - mono) - 3.0103) < 0.01,
              "two identical channels read three decibels louder");
    }
}

void testTransient()
{
    std::printf("\n[transient shaper]\n");
    constexpr double sr = 48000.0;
    const int n = 24000;   // 500 ms

    // A decaying burst, which is what the stage is for.
    const auto burst = [&](float amp) {
        std::vector<float> x(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i) {
            const double t = double(i) / sr;
            const double env = std::exp(-t * 12.0);
            x[size_t(i)] = float(amp * env * std::sin(2.0 * 3.14159265358979 * 180.0 * t));
        }
        return x;
    };

    const auto run = [&](float amp, float attack, float sustain) {
        TransientShaper t;
        t.prepare(sr, 1);
        TransientShaper::Params p;
        p.attack = attack;
        p.sustain = sustain;
        p.thresholdDb = -90.0f;      // out of the way; the gate is tested below
        t.setParams(p);
        t.reset();
        auto x = burst(amp);
        auto dry = x;
        float *ch[1] = { x.data() };
        AudioBuffer b{ ch, 1, n };
        t.process(b);
        // Gain trajectory, in dB, relative to the untouched burst.
        std::vector<double> g(size_t(n), 0.0);
        for (int i = 0; i < n; ++i)
            g[size_t(i)] = (std::fabs(dry[size_t(i)]) > 1e-7)
                               ? 20.0 * std::log10(std::fabs(x[size_t(i)] / dry[size_t(i)]))
                               : 0.0;
        return g;
    };

    // The whole point: identical treatment at any level.
    {
        const auto loud = run(0.5f, 1.0f, 0.0f);
        const auto quiet = run(0.05f, 1.0f, 0.0f);     // 20 dB down
        double worst = 0.0;
        for (size_t i = 0; i < loud.size(); ++i)
            worst = std::max(worst, std::fabs(loud[i] - quiet[i]));
        std::printf("    same burst 20 dB apart: worst gain difference %.3f dB\n", worst);
        check(worst < 0.1,
              fmt("a transient shaper treats pianissimo and fortissimo alike (%.3f dB)", worst));
    }

    // The attack path lifts the onset and must leave the tail alone -- SPL's
    // "the two do not influence each other". A two-sided gain law fails this.
    {
        const auto g = run(0.5f, 1.0f, 0.0f);
        double onset = 0.0, tail = 0.0;
        for (int i = 0; i < int(sr * 0.01); ++i)          // first 10 ms
            onset = std::max(onset, g[size_t(i)]);
        for (int i = int(sr * 0.2); i < n; ++i)           // past 200 ms
            tail = std::min(tail, g[size_t(i)]);
        std::printf("    attack +1: onset %+.2f dB, worst on the tail %+.2f dB\n", onset, tail);
        check(onset > 3.0, "the attack path lifts the onset");
        check(tail > -0.05, "and does not duck the decay it is not addressing");
    }

    // Below the gate, nothing happens at all.
    {
        TransientShaper t;
        t.prepare(sr, 1);
        TransientShaper::Params p;
        p.attack = 1.0f;
        p.thresholdDb = -40.0f;
        t.setParams(p);
        t.reset();
        auto x = burst(0.002f);          // -54 dBFS peak, below the gate
        auto dry = x;
        float *ch[1] = { x.data() };
        AudioBuffer b{ ch, 1, n };
        t.process(b);
        double worst = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
            worst = std::max(worst, double(std::fabs(x[i] - dry[i])));
        std::printf("    below the gate: worst change %.2e\n", worst);
        check(worst < 1e-9, "the gate stops it chasing the noise floor");
    }
}

void testClipper()
{
    std::printf("\n[soft clipper]\n");
    constexpr double sr = 48000.0;

    // The ceiling is a guarantee, not an asymptote -- including through the
    // oversampler's reconstruction ripple.
    {
        for (int mode = 0; mode < 2; ++mode) {
            const bool os = mode != 0;
            SoftClipper c;
            c.prepare(sr, 1);
            SoftClipper::Params p;
            p.driveDb = 18.0f;
            p.ceilingDb = -1.0f;
            p.kneeDb = 1.0f;
            p.oversample = os;
            c.setParams(p);
            c.reset();

            auto x = sine(997.0, sr, 8192, 0.9f);
            float *ch[1] = { x.data() };
            AudioBuffer b{ ch, 1, 8192 };
            c.process(b);
            float peak = 0.0f;
            for (float v : x)
                peak = std::max(peak, std::fabs(v));
            const double db = 20.0 * std::log10(double(peak));
            std::printf("    18 dB into a -1.0 dB ceiling (%s): peak %.4f dB\n",
                        os ? "2x" : "1x", db);
            // At 1x the shaper's output is the output, so the ceiling is exact.
            // With oversampling the decimation filter rings past it, and that
            // overshoot is left in rather than clipped off -- clipping it costs
            // 17 dB of alias suppression to buy back one decibel of peak. What
            // is asserted here is that the ring stays bounded and small; the
            // limiter is the stage that guarantees a ceiling.
            const double allowed = os ? 1.5 : 1e-4;
            check(db <= -1.0 + allowed,
                  fmt("the ceiling holds to within the reconstruction ring (%.4f dB)", db));
        }
    }

    // What the knee is worth, measured.
    //
    // The tone is deliberately not a neat fraction of the sample rate. With a
    // bin-aligned fundamental every alias folds back onto a harmonic bin and
    // the measurement reads clean no matter how bad the aliasing is -- the
    // classic way to accidentally prove a clipper is transparent.
    //
    // Everything within a few bins of a genuine harmonic counts as signal;
    // everything else is aliasing. Sampling only one narrow band instead --
    // say, below the fundamental -- catches whichever handful of folded
    // partials happen to land there and misses the rest, which is how a real
    // 16 dB difference reads as 2.
    {
        const int n = 16384;
        const double f0 = 1013.7;
        const auto aliasSnrDrive = [&](float kneeDb, float driveDb, bool os) {
            SoftClipper c;
            c.prepare(sr, 1);
            SoftClipper::Params p;
            p.driveDb = driveDb;
            p.ceilingDb = 0.0f;
            p.kneeDb = kneeDb;
            p.oversample = os;
            c.setParams(p);
            c.reset();
            auto x = sine(f0, sr, n, 0.5f);
            float *ch[1] = { x.data() };
            AudioBuffer b{ ch, 1, n };
            c.process(b);

            // Hann, because an off-bin tone smears across the whole spectrum
            // without one and the leakage would swamp what is being measured.
            for (int i = 0; i < n; ++i) {
                const double w = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979
                                                      * double(i) / double(n));
                x[size_t(i)] = float(double(x[size_t(i)]) * w);
            }
            Fft fft(n / 2);                       // realSize() == n
            std::vector<std::complex<float>> spec(size_t(fft.realBins()));
            fft.realForward(x.data(), spec.data());

            const double binHz = sr / double(n);
            double sig = 0.0, junk = 0.0;
            for (size_t k = 4; k < spec.size(); ++k) {
                const double hz = double(k) * binHz;
                const double m = std::floor(hz / f0 + 0.5);
                const bool harmonic = m >= 1.0 && m * f0 < sr * 0.5
                                      && std::fabs(hz - m * f0) < 6.0 * binHz;
                (harmonic ? sig : junk) += double(std::norm(spec[k]));
            }
            return 10.0 * std::log10(sig / (junk + 1e-30));
        };
        const auto aliasSnr = [&](float kneeDb, bool os) {
            return aliasSnrDrive(kneeDb, 9.0f, os);
        };

        // What the oversampler itself puts on the floor, with nothing to
        // alias: 0 dB of drive into a 0 dB ceiling never reaches the knee, so
        // any junk here is the filters own.
        const double clean1x = aliasSnrDrive(0.0f, 1.0f, false);
        const double clean2x = aliasSnrDrive(0.0f, 1.0f, true);
        std::printf("    with nothing to clip: 1x %.1f dB, 2x %.1f dB\n", clean1x, clean2x);

        const double hard = aliasSnr(0.0f, false);
        const double knee = aliasSnr(1.0f, false);
        const double kneeOs = aliasSnr(1.0f, true);
        std::printf("    alias-to-signal: hard %.1f dB, 1 dB knee %.1f dB, knee + 2x %.1f dB\n",
                    hard, knee, kneeOs);
        check(knee > hard + 6.0,
              fmt("a one-decibel knee is worth six decibels of aliasing or more (%.1f -> %.1f)",
                  hard, knee));
        check(kneeOs > knee + 6.0, "and oversampling is worth more again");
        check(clean2x >= clean1x - 0.5,
              "oversampling puts no floor of its own under the signal");
    }

    // A clipper is not a saturator: below the knee it must be exactly linear,
    // or the sustained content gets modulated and the whole reason to prefer it
    // over a limiter evaporates.
    {
        SoftClipper c;
        c.prepare(sr, 1);
        SoftClipper::Params p;
        p.driveDb = 0.0f;
        p.ceilingDb = 0.0f;
        p.kneeDb = 1.0f;
        p.oversample = false;
        c.setParams(p);
        c.reset();
        auto x = sine(997.0, sr, 4096, 0.5f);   // -6 dBFS, well under the knee
        const auto dry = x;
        float *ch[1] = { x.data() };
        AudioBuffer b{ ch, 1, 4096 };
        c.process(b);
        double worst = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
            worst = std::max(worst, double(std::fabs(x[i] - dry[i])));
        std::printf("    below the knee: worst deviation %.2e\n", worst);
        check(worst < 1e-7, "material under the knee passes through untouched");
    }
}

void testNightMode()
{
    std::printf("\n[night mode]\n");

    // The five published curves, checked against the ratios Dolby documents.
    // Levels are dB relative to the reference; a ratio of R means R dB in makes
    // one dB out, so the gain slope is 1 - 1/R.
    // `probe` is a level inside that profile's boost slope -- the profiles put
    // their boost regions in different places, which is most of what tells them
    // apart, so one probe level cannot serve all five.
    struct Case { int profile; const char *name; float boostRatio; float maxBoost;
                  float cutRatio; float probe; };
    const Case cases[] = {
        { DynamicRange::kProfileFilmStandard,  "Film Standard",  2.0f,  6.0f, 20.0f,  -6.0f },
        { DynamicRange::kProfileFilmLight,     "Film Light",     2.0f,  6.0f, 20.0f, -16.0f },
        { DynamicRange::kProfileMusicStandard, "Music Standard", 2.0f, 12.0f, 20.0f, -12.0f },
        { DynamicRange::kProfileMusicLight,    "Music Light",    2.0f, 12.0f,  2.0f, -22.0f },
        { DynamicRange::kProfileSpeech,        "Speech",         4.75f, 15.0f, 20.0f, -10.0f },
    };

    for (const Case &c : cases) {
        // Maximum boost: the gain a long way below the curve's lowest knot.
        const float maxBoost = DynamicRange::curveGainDb(c.profile, -80.0f);
        // Boost ratio, from the slope just above the lowest knot.
        const float g1 = DynamicRange::curveGainDb(c.profile, c.probe);
        const float g2 = DynamicRange::curveGainDb(c.profile, c.probe + 1.0f);
        const float boostRatio = 1.0f / (1.0f + (g2 - g1));
        // Cut ratio, from the slope at the very top.
        const float g3 = DynamicRange::curveGainDb(c.profile, 33.0f);
        const float g4 = DynamicRange::curveGainDb(c.profile, 34.0f);
        const float cutRatio = 1.0f / (1.0f + (g4 - g3));
        // The null band: nothing at all happens at the reference level.
        const float atRef = DynamicRange::curveGainDb(c.profile, 0.0f);

        std::printf("    %-15s max boost %+5.2f dB, boost %.2f:1, cut %.1f:1, at reference %+.2f dB\n",
                    c.name, maxBoost, boostRatio, cutRatio, atRef);
        check(std::fabs(maxBoost - c.maxBoost) < 0.3f,
              std::string(c.name) + " reaches its published maximum boost");
        check(std::fabs(boostRatio - c.boostRatio) < 0.05f,
              std::string(c.name) + " boosts at its published ratio");
        check(std::fabs(cutRatio - c.cutRatio) < 0.5f,
              std::string(c.name) + " cuts at its published ratio");
        check(atRef == 0.0f, std::string(c.name) + " leaves the reference level alone");
    }

    // And it actually compresses: a quiet passage comes up and a loud one comes
    // down, relative to the same material with the profile switched off.
    {
        constexpr double sr = 48000.0;
        const int n = 48000 * 3;
        const auto level = [&](float amp, int profile) {
            DynamicRange d;
            d.prepare(sr, 1);
            DynamicRange::Params p;
            p.profile = profile;
            p.referenceLufs = -24.0f;
            d.setParams(p);
            d.reset();
            auto x = sine(1000.0, sr, n, amp);
            float *ch[1] = { x.data() };
            AudioBuffer b{ ch, 1, n };
            d.process(b);
            double sum = 0.0;
            for (int i = n / 2; i < n; ++i)
                sum += double(x[size_t(i)]) * double(x[size_t(i)]);
            return 10.0 * std::log10(sum / double(n / 2) + 1e-30);
        };
        const double quietOff = level(0.005f, DynamicRange::kProfileNone);
        const double quietOn  = level(0.005f, DynamicRange::kProfileSpeech);
        const double loudOff  = level(0.9f, DynamicRange::kProfileNone);
        const double loudOn   = level(0.9f, DynamicRange::kProfileSpeech);
        std::printf("    Speech profile: quiet %+.2f dB, loud %+.2f dB\n",
                    quietOn - quietOff, loudOn - loudOff);
        check(quietOn - quietOff > 3.0, "night mode lifts what is too quiet to hear");
        check(loudOn - loudOff < -3.0, "and holds down what would wake the house");
    }
}

void testAutoGain()
{
    std::printf("\n[auto volume]\n");
    constexpr double sr = 48000.0;
    const int n = int(sr * 30.0);

    {
        // A signal well below target should be brought up to it.
        LoudnessLeveller l;
        l.prepare(sr, 1, n);
        LoudnessLeveller::Params p;
        p.targetLufs = -20.0f;
        p.rateDbPerSec = 5.0f;
        p.maxGainDb = 25.0f;
        l.setParams(p);
        l.reset();
        auto x = sine(1000.0, sr, n, 0.02f);
        float *ch[1] = { x.data() };
        AudioBuffer b{ ch, 1, n };
        l.process(b);
        const double lufs = l.lufs();
        const double gain = double(l.gainDb());
        std::printf("    input %.1f LUFS -> gain %+.2f dB (target -20)\n", lufs, gain);
        check(std::fabs(lufs + gain - (-20.0)) < 0.5,
              fmt("the leveller converges on its target (%.2f LUFS)", lufs + gain));
    }

    {
        // And it must not get there faster than it was told to. The rate limit
        // is the entire difference between a leveller and a compressor.
        LoudnessLeveller l;
        l.prepare(sr, 1, n);
        LoudnessLeveller::Params p;
        p.targetLufs = -20.0f;
        p.rateDbPerSec = 2.0f;
        p.windowDb = 0.0f;          // no dead zone, so the rate is the only limit
        p.maxGainDb = 25.0f;
        l.setParams(p);
        l.reset();

        auto x = sine(1000.0, sr, n, 0.02f);
        double worstRate = 0.0;
        float prev = l.gainDb();
        const int chunk = int(sr * 0.1);
        for (int off = 0; off + chunk <= n; off += chunk) {
            float *c2[1] = { x.data() + off };
            AudioBuffer b{ c2, 1, chunk };
            l.process(b);
            const float now = l.gainDb();
            worstRate = std::max(worstRate, std::fabs(double(now - prev)) / 0.1);
            prev = now;
        }
        // Turning *up* is deliberately a quarter of the configured rate: a
        // leveller that lifts quickly rides the noise floor up with the music,
        // which is what people hear as pumping. So the bound here is 0.5 dB/s,
        // not the 2 dB/s the control says.
        std::printf("    fastest gain movement while lifting: %.4f dB/s (limit 0.5)\n",
                    worstRate);
        check(worstRate <= 0.5 * 1.02,
              fmt("the gain never moves faster than the rate limit (%.2f dB/s)", worstRate));
    }
}

// Peak of the reconstructed waveform, by windowed-sinc interpolation.
//
// Deliberately not the engine's own oversampler: a true-peak test that
// measures with the same filter the limiter detects with would only prove the
// filter agrees with itself.
double truePeakOf(const std::vector<float> &x, int factor = 8, int taps = 64)
{
    const int n = int(x.size());
    double peak = 0.0;
    for (int i = 0; i < n; ++i)
        peak = std::max(peak, std::fabs(double(x[size_t(i)])));

    for (int i = taps; i < n - taps; ++i) {
        for (int s = 1; s < factor; ++s) {
            const double frac = double(s) / double(factor);
            double acc = 0.0;
            for (int k = -taps; k < taps; ++k) {
                const double t = double(k) - frac;
                double sinc;
                if (std::fabs(t) < 1e-9) {
                    sinc = 1.0;
                } else {
                    const double a = 3.14159265358979 * t;
                    sinc = std::sin(a) / a;
                }
                // Blackman, over the whole 2*taps span.
                const double u = (double(k + taps)) / double(2 * taps - 1);
                const double w = 0.42 - 0.5 * std::cos(2.0 * 3.14159265358979 * u)
                                 + 0.08 * std::cos(4.0 * 3.14159265358979 * u);
                acc += double(x[size_t(i + k)]) * sinc * w;
            }
            peak = std::max(peak, std::fabs(acc));
        }
    }
    return peak;
}

void testTruePeak()
{
    std::printf("\n[true peak]\n");
    constexpr double sr = 48000.0;
    const int n = 4096;

    // A quarter-rate sine, offset an eighth of a period. Every sample lands at
    // +/-0.707 of the amplitude while the waveform between them reaches the
    // full amplitude -- 3 dB of peak that a sample-domain meter cannot see.
    std::vector<float> src(size_t(n), 0.0f);
    for (int i = 0; i < n; ++i)
        src[size_t(i)] = float(0.99 * std::sin(3.14159265358979 * 0.5 * double(i)
                                               + 3.14159265358979 * 0.25));

    const auto run = [&](bool truePeak) {
        Limiter l;
        l.prepare(sr, 2, n);
        Limiter::Params p;
        p.thresholdDb = -1.0f;
        p.releaseMs = 50.0f;
        p.lookaheadMs = 1.5f;
        p.truePeak = truePeak;
        l.setParams(p);
        l.reset();
        auto a = src;
        auto b = src;
        float *ch[2] = { a.data(), b.data() };
        AudioBuffer buf{ ch, 2, n };
        l.process(buf);
        // Skip the look-ahead priming region.
        std::vector<float> tail(a.begin() + 512, a.end());
        return 20.0 * std::log10(truePeakOf(tail) + 1e-12);
    };

    const double off = run(false);
    const double on = run(true);
    std::printf("    -1.0 dBFS ceiling, inter-sample peaks: sample mode %+.2f dBTP, "
                "true-peak mode %+.2f dBTP\n", off, on);
    // The exact overshoot is signal-dependent -- this construction is a
    // near-worst case and still only reaches 0.9 dB. What is asserted is that
    // the sample-domain limiter measurably misses its own ceiling, and that
    // switching the mode on recovers it.
    check(off > -1.0 + 0.5,
          fmt("a sample-domain ceiling really is exceeded between the samples (%.2f dBTP)", off));
    check(on <= -1.0 + 0.25,
          fmt("true-peak mode holds the ceiling where it counts (%.2f dBTP)", on));
    check(off - on > 0.5,
          fmt("and the mode is what makes the difference (%.2f dB)", off - on));
}

void testMonoCompatibility()
{
    std::printf("\n[mono compatibility]\n");
    constexpr double sr = 48000.0;
    Fft fft(4096);
    const int len = fft.realSize();

    // Response of each output channel, and of their sum, for a mono input.
    const auto measure = [&](float width, float phaseAmount) {
        StereoWidener w;
        w.prepare(sr);
        StereoWidener::Params p;
        p.width = width;
        p.monoBelowHz = 0.0f;
        p.phaseAmount = phaseAmount;
        w.setParams(p);
        w.reset();

        std::vector<float> l(size_t(len), 0.0f), r(size_t(len), 0.0f);
        l[8] = 1.0f;
        r[8] = 1.0f;                   // mono impulse
        float *ch[2] = { l.data(), r.data() };
        AudioBuffer b{ ch, 2, len };
        w.process(b);

        // Two arguments, again: `vector<float> sum(size_t(len));` declares a
        // function. Eighth time in this tree.
        std::vector<float> sum(size_t(len), 0.0f);
        for (int i = 0; i < len; ++i)
            sum[size_t(i)] = l[size_t(i)] + r[size_t(i)];

        const auto spectrum = [&](const std::vector<float> &x) {
            std::vector<std::complex<float>> spec(size_t(fft.realBins()));
            std::vector<float> copy = x;
            fft.realForward(copy.data(), spec.data());
            return spec;
        };
        return std::make_pair(spectrum(l), spectrum(sum));
    };

    const auto band = [&](const std::vector<std::complex<float>> &spec, double ref) {
        double lo = 1e9, hi = -1e9, worstHz = 0.0;
        for (size_t k = 1; k < spec.size(); ++k) {
            const double hz = double(k) * sr / double(len);
            if (hz < 50.0 || hz > 18000.0)
                continue;
            const double db = 20.0 * std::log10(double(std::abs(spec[k])) / ref + 1e-20);
            if (db < lo) { lo = db; worstHz = hz; }
            hi = std::max(hi, db);
        }
        return std::make_tuple(lo, hi, worstHz);
    };

    // Side gain alone is exactly mono-safe: it only ever scales a signal that
    // vanishes from the sum.
    {
        const auto [chan, sum] = measure(1.8f, 0.0f);
        const auto [slo, shi, shz] = band(sum, 2.0);
        std::printf("    side gain 1.8: mono sum %+.3f .. %+.3f dB\n", slo, shi);
        check(std::fabs(slo) < 0.01 && std::fabs(shi) < 0.01,
              fmt("a side-gain widener cannot damage a mono fold (%.3f dB at %.0f Hz)",
                  slo, shz));
        (void)chan;
    }

    // The phase widener is the one that can widen a mono source at all -- and
    // the price is a bounded ripple in the sum. Bounded is the point: an
    // independent filter pair, which is what "decorrelation" usually means,
    // nulls completely wherever the two phases differ by 180 degrees.
    {
        const auto [chan, sum] = measure(1.0f, 1.0f);
        const auto [clo, chi, chz] = band(chan, 1.0);
        const auto [slo, shi, shz] = band(sum, 2.0);
        std::printf("    phase widener: per channel %+.3f .. %+.3f dB, "
                    "mono sum %+.3f .. %+.3f dB (worst at %.0f Hz)\n",
                    clo, chi, slo, shi, shz);
        check(std::fabs(clo) < 0.1 && std::fabs(chi) < 0.1,
              fmt("each channel stays flat (%.3f .. %.3f dB at %.0f Hz)", clo, chi, chz));
        check(slo > -3.0 && shi < 0.05,
              fmt("and the mono fold loses a bounded amount (%.3f dB at %.0f Hz)",
                  slo, shz));
    }

    // At zero it is the identity, delay aside -- an effect that colours its own
    // neutral setting is a trap.
    {
        const auto [chan, sum] = measure(1.0f, 0.0f);
        const auto [clo, chi, chz] = band(chan, 1.0);
        std::printf("    neutral: per channel %+.4f .. %+.4f dB\n", clo, chi);
        check(std::fabs(clo) < 1e-3 && std::fabs(chi) < 1e-3,
              fmt("neutral is exactly neutral (%.4f dB at %.0f Hz)", clo, chz));
        (void)sum;
    }
}

void testTube()
{
    std::printf("\n[tube saturation]\n");
    constexpr double sr = 48000.0;
    constexpr double f = 1000.0;
    const int n = 32768;

    // Symmetric drive makes odd harmonics only; that is a fuzz box, not a
    // valve. Asymmetry is what produces the second harmonic.
    const auto run = [&](float bias) {
        TubeStage t;
        t.prepare(sr, 1);
        TubeStage::Params p;
        p.drive = 6.0f; p.bias = bias; p.mix = 1.0f;
        t.setParams(p);
        t.reset();
        auto sig = sine(f, sr, n, 0.5f);
        float *ch[1] = { sig.data() };
        AudioBuffer b{ ch, 1, n };
        t.process(b);
        return sig;
    };

    const auto sym = run(0.0f);
    const auto asym = run(0.4f);

    const size_t skip = size_t(n / 2);
    const double sym2 = magnitudeAt(sym, 2 * f, sr, skip) / magnitudeAt(sym, f, sr, skip);
    const double asym2 = magnitudeAt(asym, 2 * f, sr, skip) / magnitudeAt(asym, f, sr, skip);
    const double sym3 = magnitudeAt(sym, 3 * f, sr, skip) / magnitudeAt(sym, f, sr, skip);

    std::printf("    2nd harmonic: symmetric %.4f, asymmetric %.4f\n", sym2, asym2);
    std::printf("    3rd harmonic (symmetric): %.4f\n", sym3);
    check(sym2 < 0.01, "a symmetric curve makes almost no second harmonic");
    check(sym3 > 0.02, "a symmetric curve does make odd harmonics");
    check(asym2 > sym2 * 10.0, "bias introduces the even harmonics tubes are wanted for");

    // Aliasing check: a tone high enough that its harmonics exceed Nyquist.
    // Without oversampling they fold back to inharmonic frequencies.
    {
        TubeStage t;
        t.prepare(sr, 1);
        TubeStage::Params p;
        p.drive = 8.0f; p.bias = 0.3f; p.mix = 1.0f;
        t.setParams(p);
        t.reset();
        auto sig = sine(9000.0, sr, n, 0.5f);
        float *ch[1] = { sig.data() };
        AudioBuffer b{ ch, 1, n };
        t.process(b);

        const double fund = magnitudeAt(sig, 9000.0, sr, skip);
        // 2*9k = 18k is real; 3*9k = 27k would fold to 48k-27k = 21k, and
        // 4*9k = 36k folds to 12k -- squarely in the audible range.
        const double fold = magnitudeAt(sig, 12000.0, sr, skip) / fund;
        std::printf("    fold-back at 12 kHz from a 9 kHz tone: %.5f of fundamental\n", fold);
        check(fold < 0.02, "oversampling keeps aliasing out of the audible range");
    }
}

void testVirtualBass()
{
    std::printf("\n[virtual bass]\n");
    constexpr double sr = 48000.0;
    const int n = 65536;
    const double f = 45.0;          // below what a small speaker manages

    VirtualBass vb;
    vb.prepare(sr, 1);
    VirtualBass::Params p;
    p.cutoffHz = 100.0f;
    p.amount = 0.8f;
    p.drive = 6.0f;
    p.removeOriginal = false;
    vb.setParams(p);
    vb.reset();

    auto sig = sine(f, sr, n, 0.5f);
    const auto original = sig;
    float *ch[1] = { sig.data() };
    AudioBuffer b{ ch, 1, n };
    vb.process(b);

    const size_t skip = size_t(n / 2);
    const double before2 = magnitudeAt(original, 2 * f, sr, skip);
    const double after2 = magnitudeAt(sig, 2 * f, sr, skip);
    const double after3 = magnitudeAt(sig, 3 * f, sr, skip);
    const double fund = magnitudeAt(sig, f, sr, skip);

    std::printf("    45 Hz in: 2nd harmonic %.5f -> %.5f, 3rd %.5f\n",
                before2, after2, after3);
    // The assertion is on the third harmonic, not the second. Two reasons,
    // and they agree: the second harmonic of a 45 Hz tone is at 90 Hz, which
    // is below this stage's own 100 Hz harmonic high-pass and is therefore
    // filtered out on purpose; and the dominance region for residue pitch is
    // harmonics three to five, so the third is the one carrying the illusion.
    check(after3 > before2 * 20.0 + 1e-4,
          "harmonics of the missing fundamental are synthesised");
    check(after2 > 1e-4, "the series includes the second harmonic");
    check(fund > 1e-3, "the original low tone is still there when not removed");

    // Harmonic balance must not depend on how loud the track was mastered.
    //
    // A memoryless nonlinearity's harmonic *ratios* are a function of how hard
    // it is driven, so without a level normaliser in front of the shaper this
    // stage's timbre -- and its apparent strength -- slide with programme
    // level. Published measurements of a comparable shaper put the drift at
    // 26.6 dB on the second harmonic over 21 dB of input level. This runs the
    // same tone 20 dB apart and compares the ratios between harmonics.
    {
        const auto ratios = [&](float amp) {
            VirtualBass v;
            v.prepare(sr, 1);
            v.setParams(p);
            v.reset();
            auto sg = sine(f, sr, n, amp);
            float *cc[1] = { sg.data() };
            AudioBuffer bb{ cc, 1, n };
            v.process(bb);
            const double h3 = magnitudeAt(sg, 3 * f, sr, skip);
            const double h4 = magnitudeAt(sg, 4 * f, sr, skip);
            const double h5 = magnitudeAt(sg, 5 * f, sr, skip);
            return std::make_pair(h4 / (h3 + 1e-12), h5 / (h3 + 1e-12));
        };
        const auto loud = ratios(0.5f);
        const auto quiet = ratios(0.05f);        // 20 dB down
        const double d4 = 20.0 * std::log10(loud.first / (quiet.first + 1e-12));
        const double d5 = 20.0 * std::log10(loud.second / (quiet.second + 1e-12));
        std::printf("    harmonic balance over 20 dB of level: 4th %+.2f dB, 5th %+.2f dB\n",
                    d4, d5);
        check(std::fabs(d4) < 3.0 && std::fabs(d5) < 3.0,
              fmt("the shaper's harmonic balance holds over 20 dB of level "
                  "(%+.2f dB, %+.2f dB)", d4, d5));
    }

    // With removeOriginal the fundamental should be strongly attenuated -- that
    // is the mode for a speaker that cannot reproduce it at all.
    {
        VirtualBass vb2;
        vb2.prepare(sr, 1);
        p.removeOriginal = true;
        vb2.setParams(p);
        vb2.reset();
        auto s2 = sine(f, sr, n, 0.5f);
        float *c2[1] = { s2.data() };
        AudioBuffer b2{ c2, 1, n };
        vb2.process(b2);
        const double f2 = magnitudeAt(s2, f, sr, skip);
        std::printf("    fundamental with removeOriginal: %.5f (was %.5f)\n", f2, fund);
        check(f2 < fund * 0.3, "removeOriginal takes out what the speaker cannot play");
    }
}

void testExciter()
{
    std::printf("\n[exciter]\n");
    constexpr double sr = 48000.0;
    const int n = 32768;

    Exciter ex;
    ex.prepare(sr, 1);
    Exciter::Params p;
    p.frequencyHz = 3000.0f;
    p.drive = 5.0f;
    p.amount = 0.6f;
    ex.setParams(p);
    ex.reset();

    // A tone above the split point must gain harmonics.
    auto high = sine(4000.0, sr, n, 0.4f);
    float *ch[1] = { high.data() };
    AudioBuffer b{ ch, 1, n };
    ex.process(b);

    const size_t skip = size_t(n / 2);
    const double h3 = magnitudeAt(high, 12000.0, sr, skip) / magnitudeAt(high, 4000.0, sr, skip);
    std::printf("    4 kHz tone gains a 12 kHz harmonic at %.4f of fundamental\n", h3);
    check(h3 > 0.005, "the high band is excited");

    // A tone well below the split point must come through nearly untouched --
    // otherwise this is just a distortion box.
    ex.reset();
    auto low = sine(300.0, sr, n, 0.4f);
    const auto lowRef = low;
    float *ch2[1] = { low.data() };
    AudioBuffer b2{ ch2, 1, n };
    ex.process(b2);

    const double before = magnitudeAt(lowRef, 300.0, sr, skip);
    const double after = magnitudeAt(low, 300.0, sr, skip);
    const double db = 20.0 * std::log10((after + 1e-12) / (before + 1e-12));
    std::printf("    300 Hz tone changed by %.3f dB\n", db);
    check(std::fabs(db) < 0.5, "content below the split point is left alone");
}

// --------------------------------------------------------------------- stereo

void testStereo()
{
    std::printf("\n[stereo]\n");
    constexpr double sr = 48000.0;
    const int n = 8192;

    // Width 1 must be the exact identity. A "neutral" setting that colours the
    // signal is the kind of thing that gets blamed on everything else.
    {
        StereoWidener w;
        w.prepare(sr);
        StereoWidener::Params p;
        p.width = 1.0f; p.monoBelowHz = 0.0f;
        w.setParams(p);
        w.reset();

        std::vector<float> L(size_t(n), 0.0f), R(size_t(n), 0.0f), refL(size_t(n), 0.0f), refR(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i) {
            L[size_t(i)] = refL[size_t(i)] = std::sin(float(i) * 0.03f);
            R[size_t(i)] = refR[size_t(i)] = std::cos(float(i) * 0.017f) * 0.7f;
        }
        float *ch[2] = { L.data(), R.data() };
        AudioBuffer b{ ch, 2, n };
        w.process(b);

        float worst = 0.0f;
        for (int i = 0; i < n; ++i) {
            worst = std::max(worst, std::fabs(L[size_t(i)] - refL[size_t(i)]));
            worst = std::max(worst, std::fabs(R[size_t(i)] - refR[size_t(i)]));
        }
        check(worst < 1e-6f, fmt("width 1.0 is the identity (worst deviation %.2e)", worst));
    }

    // Width 0 must collapse to mono.
    {
        StereoWidener w;
        w.prepare(sr);
        StereoWidener::Params p;
        p.width = 0.0f;
        w.setParams(p);
        w.reset();

        std::vector<float> L(size_t(n), 1.0f), R(size_t(n), -1.0f);
        float *ch[2] = { L.data(), R.data() };
        AudioBuffer b{ ch, 2, n };
        w.process(b);

        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
            worst = std::max(worst, std::fabs(L[size_t(i)] - R[size_t(i)]));
        check(worst < 1e-6f, "width 0 collapses to mono");
    }

    // Crossfeed must put some of each channel into the other, and must not
    // change a mono signal's balance.
    {
        Crossfeed cf;
        cf.prepare(sr);
        Crossfeed::Params p;
        cf.setParams(p);
        cf.reset();

        std::vector<float> L(size_t(n), 0.0f), R(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i)
            L[size_t(i)] = std::sin(float(i) * 0.02f) * 0.5f;   // hard left

        float *ch[2] = { L.data(), R.data() };
        AudioBuffer b{ ch, 2, n };
        cf.process(b);

        double energyR = 0.0;
        for (int i = n / 2; i < n; ++i)
            energyR += double(R[size_t(i)]) * double(R[size_t(i)]);
        check(energyR > 1e-4, fmt("a hard-left signal reaches the right ear (energy %.4f)", energyR));

        cf.reset();
        std::vector<float> mL(size_t(n), 0.0f), mR(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i)
            mL[size_t(i)] = mR[size_t(i)] = std::sin(float(i) * 0.02f) * 0.5f;
        float *ch2[2] = { mL.data(), mR.data() };
        AudioBuffer b2{ ch2, 2, n };
        cf.process(b2);

        float worst = 0.0f;
        for (int i = 0; i < n; ++i)
            worst = std::max(worst, std::fabs(mL[size_t(i)] - mR[size_t(i)]));
        check(worst < 1e-6f, "a centred signal stays centred");
    }
}

// ----------------------------------------------------------------- multiband

void testMultiband()
{
    std::printf("\n[multiband compressor]\n");
    constexpr double sr = 48000.0;
    const int n = 32768;

    // Every band at 1:1 must be transparent. This is the check that a
    // multiband processor is safe to leave switched on.
    MultibandCompressor mb;
    mb.prepare(sr, 2, n);
    MultibandCompressor::Params p;
    for (int b = 0; b < MultibandCompressor::kBands; ++b) {
        p.band[b].ratio = 1.0f;
        p.band[b].thresholdDb = 0.0f;
    }
    mb.setParams(p);
    mb.reset();

    double worstDb = 0.0, worstFreq = 0.0;
    for (double f : { 60.0, 150.0, 250.0, 700.0, 3000.0, 6000.0, 12000.0 }) {
        mb.reset();
        auto L = sine(f, sr, n, 0.4f);
        auto R = L;
        const auto ref = L;
        float *ch[2] = { L.data(), R.data() };
        AudioBuffer b{ ch, 2, n };
        mb.process(b);

        const double a = magnitudeAt(ref, f, sr, size_t(n / 2));
        const double c = magnitudeAt(L, f, sr, size_t(n / 2));
        const double db = 20.0 * std::log10((c + 1e-12) / (a + 1e-12));
        if (std::fabs(db) > std::fabs(worstDb)) { worstDb = db; worstFreq = f; }
    }
    std::printf("    bypassed deviation: worst %.3f dB at %.0f Hz\n", worstDb, worstFreq);
    check(std::fabs(worstDb) < 0.6, "all bands at 1:1 is transparent");

    // The three-band tree reconstructs flat -- measured across the spectrum,
    // not sampled at seven tones.
    //
    // A two-crossover tree is not symmetric: the high band leaves through two
    // cascaded Linkwitz-Riley sections and the low band through none, so their
    // phases do not line up and the sum dips at the lower crossover. process()
    // compensates by running the low band through the second crossover's
    // allpass pair, and that compensation had no test of its own -- which is
    // how a correct implementation quietly becomes an incorrect one.
    {
        mb.reset();
        Fft fft(8192);
        const int len = fft.realSize();
        std::vector<float> imp(size_t(len), 0.0f), imp2(size_t(len), 0.0f);
        imp[0] = 1.0f;
        float *ich[2] = { imp.data(), imp2.data() };
        AudioBuffer ib{ ich, 2, len };
        mb.process(ib);

        std::vector<std::complex<float>> spec(size_t(fft.realBins()));
        fft.realForward(imp.data(), spec.data());

        double worst = 0.0, worstAt = 0.0;
        for (size_t k = 1; k < spec.size(); ++k) {
            const double hz = double(k) * sr / double(len);
            if (hz < 30.0 || hz > 18000.0)
                continue;
            const double db = 20.0 * std::log10(double(std::abs(spec[k])) + 1e-20);
            if (std::fabs(db) > std::fabs(worst)) { worst = db; worstAt = hz; }
        }
        check(std::fabs(worst) < 0.10,
              fmt("the three-band tree sums flat: worst %.4f dB at %.0f Hz", worst, worstAt));
    }

    // A loud bass tone must not duck the treble -- the entire point.
    {
        mb.reset();
        MultibandCompressor::Params q;
        for (int b = 0; b < MultibandCompressor::kBands; ++b) {
            q.band[b].thresholdDb = -30.0f;
            q.band[b].ratio = 10.0f;
            q.band[b].attackMs = 1.0f;
            q.band[b].releaseMs = 50.0f;
        }
        mb.setParams(q);
        mb.reset();

        // 60 Hz at full scale plus a quiet 8 kHz.
        std::vector<float> L(size_t(n), 0.0f), R(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i) {
            const double t = double(i) / sr;
            const double v = 0.9 * std::sin(2 * 3.14159265358979 * 60 * t)
                             + 0.05 * std::sin(2 * 3.14159265358979 * 8000 * t);
            L[size_t(i)] = R[size_t(i)] = float(v);
        }
        float *ch[2] = { L.data(), R.data() };
        AudioBuffer b{ ch, 2, n };
        mb.process(b);

        const double hi = magnitudeAt(L, 8000.0, sr, size_t(n / 2));
        const double lo = magnitudeAt(L, 60.0, sr, size_t(n / 2));
        std::printf("    loud 60 Hz + quiet 8 kHz -> bass %.4f, treble %.4f\n", lo, hi);
        check(hi > 0.02, "a loud bass note does not duck the treble");
        check(lo < 0.5, "the bass band itself is compressed");
    }
}

// ------------------------------------------------------------------------ fft

void testFft()
{
    std::printf("\n[fft]\n");

    // Checked against a naive O(n^2) DFT rather than against itself. A fast
    // transform that agrees with its own inverse can still be wrong in a way
    // that cancels; agreeing with the definition cannot.
    auto naiveDft = [](const std::vector<float> &x) {
        const int n = int(x.size());
        std::vector<std::complex<double>> out(size_t(n), std::complex<double>(0.0, 0.0));
        for (int k = 0; k < n; ++k) {
            std::complex<double> sum(0.0, 0.0);
            for (int t = 0; t < n; ++t) {
                const double a = -2.0 * 3.14159265358979323846 * double(k) * double(t) / double(n);
                sum += double(x[size_t(t)]) * std::complex<double>(std::cos(a), std::sin(a));
            }
            out[size_t(k)] = sum;
        }
        return out;
    };

    uint32_t seed = 0xBEEF01u;
    for (int half : { 4, 8, 64, 512 }) {
        Fft fft(half);
        const int n = fft.realSize();

        std::vector<float> x(size_t(n), 0.0f);
        for (auto &v : x)
            v = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;

        const auto reference = naiveDft(x);
        std::vector<std::complex<float>> bins(size_t(fft.realBins()), std::complex<float>(0.0f, 0.0f));
        fft.realForward(x.data(), bins.data());

        double worst = 0.0;
        for (int k = 0; k < fft.realBins(); ++k)
            worst = std::max(worst, std::abs(std::complex<double>(bins[size_t(k)]) - reference[size_t(k)]));
        check(worst < 1e-2,
              fmt("real forward matches a naive DFT at n=%.0f (worst error %.2e)",
                  double(n), worst));

        // Round trip. The forward is unnormalised and the inverse divides by N,
        // which is what makes multiply-in-the-frequency-domain a plain
        // convolution with no stray scale factor to remember.
        std::vector<float> back(size_t(n), 0.0f);
        fft.realInverse(bins.data(), back.data());
        double worstRt = 0.0;
        for (int i = 0; i < n; ++i)
            worstRt = std::max(worstRt, double(std::fabs(back[size_t(i)] - x[size_t(i)])));
        check(worstRt < 1e-5,
              fmt("real round trip is the identity at n=%.0f (worst error %.2e)",
                  double(n), worstRt));
    }

    // The property the convolver actually depends on: multiplying spectra is
    // circular convolution in time.
    {
        Fft fft(64);
        const int n = fft.realSize();
        std::vector<float> a(size_t(n), 0.0f), b(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i) {
            a[size_t(i)] = float(int(xorshift(seed) & 0xFFFu) - 2048) / 2048.0f;
            b[size_t(i)] = (i < 8) ? float(int(xorshift(seed) & 0xFFFu) - 2048) / 2048.0f : 0.0f;
        }

        std::vector<float> expected(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                expected[size_t(i)] += a[size_t(j)] * b[size_t((i - j + n) % n)];

        const std::complex<float> zeroC(0.0f, 0.0f);
        std::vector<std::complex<float>> A(size_t(fft.realBins()), zeroC);
        std::vector<std::complex<float>> B(size_t(fft.realBins()), zeroC);
        fft.realForward(a.data(), A.data());
        fft.realForward(b.data(), B.data());
        for (int k = 0; k < fft.realBins(); ++k)
            A[size_t(k)] *= B[size_t(k)];

        std::vector<float> got(size_t(n), 0.0f);
        fft.realInverse(A.data(), got.data());

        double worst = 0.0;
        for (int i = 0; i < n; ++i)
            worst = std::max(worst, double(std::fabs(got[size_t(i)] - expected[size_t(i)])));
        check(worst < 1e-4,
              fmt("spectral multiply is circular convolution (worst error %.2e)", worst));
    }
}

// ------------------------------------------------------------------ resampler

void testResampler()
{
    std::printf("\n[resampler]\n");

    // Unity ratio has to be exactly the identity, not merely close: it is the
    // common case, and a resampler that quietly filters an already-correct
    // impulse response would degrade it for nothing.
    {
        std::vector<float> x(size_t(1000), 0.0f);
        uint32_t seed = 0x51EDu;
        for (auto &v : x)
            v = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;
        const auto y = Resampler::resample(x.data(), int(x.size()), 48000.0, 48000.0);
        check(y.size() == x.size() && std::memcmp(y.data(), x.data(), x.size() * sizeof(float)) == 0,
              "a matching rate is passed through untouched");
    }

    // Constant in, constant out. This is the DC gain, and getting it wrong
    // would change the level of every impulse response that needs converting.
    {
        std::vector<float> x(size_t(4000), 0.5f);
        const auto y = Resampler::resample(x.data(), int(x.size()), 44100.0, 96000.0);
        double worst = 0.0;
        // Skip the ends, where the kernel legitimately runs off the signal.
        for (size_t i = 400; i + 400 < y.size(); ++i)
            worst = std::max(worst, double(std::fabs(y[i] - 0.5f)));
        check(worst < 1e-4, fmt("DC is preserved 44.1k -> 96k (worst error %.2e)", worst));
    }

    // A sine has to come out as the same sine, at the same frequency and
    // amplitude. Anything else means the conversion shifted pitch, which is
    // exactly the failure that makes a mismatched impulse response wrong.
    {
        constexpr double inRate = 44100.0, outRate = 96000.0, freq = 1000.0;
        const int n = 44100;
        std::vector<float> x(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i)
            x[size_t(i)] = float(std::sin(2.0 * 3.14159265358979323846 * freq * double(i) / inRate));

        const auto y = Resampler::resample(x.data(), n, inRate, outRate);

        double worst = 0.0;
        const size_t guard = 2000;
        for (size_t i = guard; i + guard < y.size(); ++i) {
            const double want = std::sin(2.0 * 3.14159265358979323846 * freq * double(i) / outRate);
            worst = std::max(worst, std::fabs(double(y[i]) - want));
        }
        check(worst < 2e-3,
              fmt("a 1 kHz sine survives 44.1k -> 96k (worst error %.2e)", worst));
    }

    // Downsampling has to REMOVE content above the new Nyquist, not fold it
    // back. A resampler without this sounds broken rather than merely soft,
    // because the aliases are inharmonic.
    {
        constexpr double inRate = 96000.0, outRate = 44100.0;
        constexpr double freq = 30000.0;          // well above 22.05 kHz
        const int n = 96000;
        std::vector<float> x(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i)
            x[size_t(i)] = float(std::sin(2.0 * 3.14159265358979323846 * freq * double(i) / inRate));

        const auto y = Resampler::resample(x.data(), n, inRate, outRate);

        double peak = 0.0;
        const size_t guard = 2000;
        for (size_t i = guard; i + guard < y.size(); ++i)
            peak = std::max(peak, double(std::fabs(y[i])));
        check(peak < 0.01,
              fmt("a 30 kHz tone is removed, not aliased, at 96k -> 44.1k (peak %.4f)", peak));
    }

    // An impulse must land where it belongs in time. A resampler that is right
    // in the frequency domain but off by a sample would smear every impulse
    // response it touched.
    {
        constexpr double inRate = 48000.0, outRate = 96000.0;
        const int n = 2000, at = 1000;
        std::vector<float> x(size_t(n), 0.0f);
        x[size_t(at)] = 1.0f;

        const auto y = Resampler::resample(x.data(), n, inRate, outRate);

        size_t peakAt = 0;
        double peak = 0.0;
        for (size_t i = 0; i < y.size(); ++i) {
            if (std::fabs(double(y[i])) > peak) {
                peak = std::fabs(double(y[i]));
                peakAt = i;
            }
        }
        const size_t want = size_t(double(at) * outRate / inRate);
        check(peakAt == want,
              fmt("an impulse stays put: peak at %.0f, want %.0f", double(peakAt), double(want)));
    }

    check(Resampler::outputFrames(44100, 44100.0, 96000.0) == 96000,
          "output length follows the ratio");
}

// ----------------------------------------------------------------- convolver

// Straight time-domain convolution. Slow and obviously correct, which is the
// point: the partitioned FFT engine is checked against this, not against
// itself.
std::vector<float> naiveConvolve(const std::vector<float> &x, const std::vector<float> &h)
{
    std::vector<float> y(x.size(), 0.0f);
    for (size_t n = 0; n < x.size(); ++n) {
        double acc = 0.0;
        for (size_t k = 0; k < h.size() && k <= n; ++k)
            acc += double(x[n - k]) * double(h[k]);
        y[n] = float(acc);
    }
    return y;
}

// Runs `x` through a stage in blocks of `blockSize`, returning the output.
std::vector<float> runStage(ConvolutionStage &stage, const std::vector<float> &x,
                            int blockSize)
{
    std::vector<float> y = x;
    size_t at = 0;
    while (at < y.size()) {
        const int n = int(std::min(size_t(blockSize), y.size() - at));
        float *ch[1] = { y.data() + at };
        AudioBuffer buf{ ch, 1, n };
        stage.process(buf);
        at += size_t(n);
    }
    return y;
}

void testConvolver()
{
    std::printf("\n[convolution]\n");

    constexpr double sr = 48000.0;
    const uint32_t block = convBlockForRate(sr);
    check(block == 256u, fmt("block size at 48 kHz is %.0f", double(block)));

    // A short decaying impulse response, deliberately not a multiple of the
    // block so the final partition is partly empty.
    //
    // Normalised so that the sum of its magnitudes is 1. That is the only bound
    // that actually guarantees |y| <= |x|, and without it a random 700-tap
    // response drives a full-scale input to about +24 dBFS -- where safeWet's
    // +12 dB ceiling correctly clamps it, and the test would be measuring the
    // clamp rather than the convolution.
    uint32_t seed = 0xC0FFEEu;
    std::vector<float> ir(size_t(700), 0.0f);
    for (size_t i = 0; i < ir.size(); ++i) {
        const float decay = std::exp(-float(i) / 200.0f);
        ir[i] = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f * decay;
    }
    {
        double l1 = 0.0;
        for (float v : ir)
            l1 += std::fabs(double(v));
        for (auto &v : ir)
            v = float(double(v) / l1);
    }

    std::vector<float> x(size_t(4096), 0.0f);
    for (auto &v : x)
        v = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;

    const std::vector<float> reference = naiveConvolve(x, ir);

    auto buildStage = [&](ConvolutionStage &stage) {
        stage.prepare(sr, 1, 64);
        stage.setArmed(true);
        Convolution::Params p;
        p.mix = 1.0f;
        p.irGeneration = 1;
        stage.setParams(p);

        const float *irChans[1] = { ir.data() };
        ConvBuildRequest req;
        req.irChannels = irChans;
        req.irChannelCount = 1;
        req.tapCount = int(ir.size());
        req.streamChannels = 1;
        req.block = block;
        req.wetGain = 1.0f;
        ConvKernel *k = buildConvKernel(req);
        check(k != nullptr, "kernel builds");
        stage.offerKernel(k);
        stage.setEnabled(true);
        // Two ticks: one to take the kernel and duck, one to install it. Then
        // let the crossfade finish.
        std::vector<float> warm(size_t(block) * 4, 0.0f);
        runStage(stage, warm, int(block));
        stage.setEnabled(true);
        std::vector<float> settle(size_t(sr * 0.05), 0.0f);
        runStage(stage, settle, int(block));
    };

    // The core claim: the partitioned engine reproduces a direct convolution,
    // delayed by exactly one block.
    {
        ConvolutionStage stage;
        buildStage(stage);
        const std::vector<float> got = runStage(stage, x, int(block));

        double worst = 0.0;
        // Compare from one block in, since the output is delayed by `block`.
        for (size_t n = block; n < got.size(); ++n)
            worst = std::max(worst, double(std::fabs(got[n] - reference[n - block])));
        check(worst < 1e-4,
              fmt("matches a direct convolution, delayed one block (worst %.2e)", worst));
    }

    // Independence from the host's block size. The audio engine hands over
    // whatever it likes, including sizes that are not multiples of the internal
    // block, and the result must not change.
    {
        double worst = 0.0;
        for (int hostBlock : { 1, 37, 100, 256, 480, 1056 }) {
            ConvolutionStage stage;
            buildStage(stage);
            const std::vector<float> got = runStage(stage, x, hostBlock);
            for (size_t n = block; n < got.size(); ++n)
                worst = std::max(worst, double(std::fabs(got[n] - reference[n - block])));
        }
        check(worst < 1e-4,
              fmt("same output for host blocks of 1..1056 samples (worst %.2e)", worst));
    }

    // An unarmed stage must be exactly transparent -- not approximately, and
    // with no delay either, since an unarmed stage reports no latency.
    {
        ConvolutionStage stage;
        stage.prepare(sr, 1, 64);
        const std::vector<float> got = runStage(stage, x, 256);
        check(std::memcmp(got.data(), x.data(), x.size() * sizeof(float)) == 0,
              "an unarmed stage is bit-exact transparent");
        check(stage.latencySamples() == 0u, "an unarmed stage reports no latency");
    }

    // Armed but with no kernel: dry, delayed by one block. This is the state
    // between arming and the impulse response finishing its build, and it must
    // already carry the delay the engine has been told about.
    {
        ConvolutionStage stage;
        stage.prepare(sr, 1, 64);
        stage.setArmed(true);
        const std::vector<float> got = runStage(stage, x, 256);
        double worst = 0.0;
        for (size_t n = block; n < got.size(); ++n)
            worst = std::max(worst, double(std::fabs(got[n] - x[n - block])));
        check(worst == 0.0, "armed without a kernel is the dry signal, delayed exactly");
        check(stage.latencySamples() == block, "an armed stage reports one block of latency");
    }

    // Nothing may allocate once prepared. An FFT convolver that allocates per
    // callback would be the single worst offender in the whole chain.
    {
        ConvolutionStage stage;
        buildStage(stage);

        std::vector<float> audio(size_t(1056), 0.2f);
        float *ch[1] = { audio.data() };

        g_allocations.store(0, std::memory_order_relaxed);
        g_countAllocations = true;
        for (int round = 0; round < 8; ++round) {
            for (int n : { 128, 256, 512, 1056, 480, 64 }) {
                AudioBuffer buf{ ch, 1, n };
                stage.process(buf);
            }
        }
        g_countAllocations = false;
        const long allocations = g_allocations.load(std::memory_order_relaxed);
        check(allocations == 0,
              fmt("no allocations on the convolution path (%.0f seen)", double(allocations)));

        ConvKernel *retired = stage.reclaimKernel();
        destroyConvKernel(retired);
    }

    // A NaN arriving from upstream must not poison the delay line. Every other
    // effect washes one out as its state decays; this one would keep it
    // forever, silencing the endpoint permanently.
    {
        ConvolutionStage stage;
        buildStage(stage);

        std::vector<float> poisoned = x;
        poisoned[100] = std::nanf("");
        poisoned[1000] = INFINITY;
        const std::vector<float> got = runStage(stage, poisoned, 256);

        bool finite = true;
        for (size_t n = 2000; n < got.size(); ++n) {
            uint32_t bits;
            std::memcpy(&bits, &got[n], 4);
            if ((bits & 0x7F800000u) == 0x7F800000u)
                finite = false;
        }
        check(finite, "a NaN from upstream does not poison the delay line");
    }

    // Channel routing: a mono impulse response must feed every channel, and an
    // LFE channel must be left alone by default.
    {
        const float *irChans[1] = { ir.data() };
        ConvBuildRequest req;
        req.irChannels = irChans;
        req.irChannelCount = 1;
        req.tapCount = int(ir.size());
        req.streamChannels = 6;
        req.block = block;
        // 5.1: FL FR FC LFE BL BR -- LFE is stream channel 3.
        req.channelMask = 0x3F;
        ConvKernel *k = buildConvKernel(req);
        check(k && k->route[0].spec == 0 && k->route[1].spec == 0,
              "a mono impulse response feeds the main channels");
        check(k && k->route[3].spec == kConvBypass,
              "the LFE channel is bypassed by default");
        destroyConvKernel(k);
    }
}

// ------------------------------------------------------------------ equalizer

EqBand eqBand(FilterKind kind, double f, double gainDb, double q, bool on = true)
{
    EqBand b;
    std::memset(&b, 0, sizeof b);
    b.freqHz = float(f);
    b.gainDb = float(gainDb);
    b.q = float(q);
    b.type = uint8_t(kind);
    b.enabled = on ? 1u : 0u;
    return b;
}

Equalizer::Params eqParams(const std::vector<EqBand> &bands, float preampDb = 0.0f)
{
    Equalizer::Params p;
    std::memset(&p, 0, sizeof p);
    p.preampDb = preampDb;
    int n = int(bands.size());
    if (n > Equalizer::kMaxBands)
        n = Equalizer::kMaxBands;
    for (int i = 0; i < n; ++i)
        p.band[i] = bands[size_t(i)];
    p.bandCount = uint32_t(n);
    return p;
}

// The equalizer's impulse response, as a spectrum.
//
// This is the measurement that matters: it goes through process(), i.e. through
// the difference equation the audio actually takes, rather than re-evaluating
// the transfer function the design produced. A coefficient that is right and a
// state update that is wrong look identical to any test that only asks the
// design what it thinks it does.
std::vector<float> eqSpectrumDb(Equalizer &eq, int fftSize)
{
    Fft fft(fftSize);
    const int n = fft.realSize();

    std::vector<float> impulse(size_t(n), 0.0f);
    impulse[0] = 1.0f;

    float *ch[1] = { impulse.data() };
    AudioBuffer buf{ ch, 1, n };
    eq.process(buf);

    std::vector<std::complex<float>> spec(size_t(fft.realBins()));
    fft.realForward(impulse.data(), spec.data());

    std::vector<float> db(spec.size());
    for (size_t i = 0; i < spec.size(); ++i)
        db[i] = 20.0f * std::log10(std::abs(spec[i]) + 1e-20f);
    return db;
}

void testEqualizer()
{
    std::printf("\n[equalizer]\n");

    constexpr double sr = 48000.0;
    constexpr int fftSize = 8192;             // 16384 real samples, 341 ms

    // --- the curve on screen is the curve in the audio ---------------------
    //
    // A realistic AutoEQ-shaped set, plus one fourth-order type so the two-
    // section path is exercised as well.
    {
        Equalizer eq;
        eq.prepare(sr, 1);
        eq.setParams(eqParams({
            eqBand(FilterKind::LSC, 105.0, 8.7, 0.71),
            eqBand(FilterKind::PK, 35.0, 1.6, 1.40),
            eqBand(FilterKind::PK, 1200.0, -4.5, 2.00),
            eqBand(FilterKind::PK, 6300.0, 3.2, 3.50),
            eqBand(FilterKind::HSC, 10000.0, -2.5, 0.71),
            eqBand(FilterKind::BWHP, 22.0, 0.0, 0.0),
        }, -6.3f));

        const std::vector<float> measured = eqSpectrumDb(eq, fftSize);
        const int realSize = fftSize * 2;

        double worst = 0.0;
        double worstHz = 0.0;
        for (size_t i = 1; i < measured.size(); ++i) {
            const double hz = double(i) * sr / double(realSize);
            if (hz < 30.0 || hz > 18000.0)
                continue;
            const double err = std::fabs(double(measured[i]) - eq.responseDb(hz));
            if (err > worst) { worst = err; worstHz = hz; }
        }
        check(worst < 0.02,
              fmt("measured response matches responseDb() to %.4f dB (worst at %.0f Hz)",
                  worst, worstHz));
    }

    // --- fourth-order alignments -------------------------------------------
    //
    // A 4th-order Butterworth is -3.01 dB at Fc and, being 24 dB/octave,
    // -24.1 dB one octave above it: |H|^2 = 1 / (1 + (f/fc)^8).
    {
        const FilterSections bw = designFilter(FilterKind::BWLP, 500.0, 0.0, 0.0, sr);
        check(bw.count == 2, "BWLP is two cascaded sections");
        const double atFc = magnitudeDb(bw, 500.0, sr);
        const double atOct = magnitudeDb(bw, 1000.0, sr);
        check(std::fabs(atFc + 3.0103) < 0.02, fmt("BWLP is %.3f dB at Fc", atFc));
        check(std::fabs(atOct + 24.1) < 0.15, fmt("BWLP is %.2f dB one octave up", atOct));

        // Linkwitz-Riley is two identical Butterworth sections, so -6 dB at Fc.
        const FilterSections lr = designFilter(FilterKind::LRLP, 500.0, 0.0, 0.0, sr);
        check(std::fabs(magnitudeDb(lr, 500.0, sr) + 6.0206) < 0.02,
              fmt("LRLP is %.3f dB at Fc", magnitudeDb(lr, 500.0, sr)));
    }

    // The defining property of a Linkwitz-Riley crossover: low plus high sums
    // to flat. Checked through process(), on the summed impulse responses.
    {
        Equalizer low, high;
        low.prepare(sr, 1);
        high.prepare(sr, 1);
        low.setParams(eqParams({ eqBand(FilterKind::LRLP, 1000.0, 0.0, 0.0) }));
        high.setParams(eqParams({ eqBand(FilterKind::LRHP, 1000.0, 0.0, 0.0) }));

        Fft fft(fftSize);
        const int n = fft.realSize();
        std::vector<float> a(size_t(n), 0.0f), b(size_t(n), 0.0f);
        a[0] = 1.0f;
        b[0] = 1.0f;
        {
            float *ca[1] = { a.data() };
            float *cb[1] = { b.data() };
            AudioBuffer ba{ ca, 1, n };
            AudioBuffer bb{ cb, 1, n };
            low.process(ba);
            high.process(bb);
        }
        for (size_t i = 0; i < a.size(); ++i)
            a[i] += b[i];

        std::vector<std::complex<float>> spec(size_t(fft.realBins()));
        fft.realForward(a.data(), spec.data());

        double worst = 0.0;
        for (size_t i = 1; i < spec.size(); ++i) {
            const double hz = double(i) * sr / double(n);
            if (hz < 20.0 || hz > 20000.0)
                continue;
            const double db = 20.0 * std::log10(double(std::abs(spec[i])) + 1e-20);
            worst = std::max(worst, std::fabs(db));
        }
        check(worst < 0.02, fmt("LRLP + LRHP sum flat to %.4f dB", worst));
    }

    // The crossover the multiband compressor uses is the same filter under a
    // different name. Two definitions of Linkwitz-Riley in one codebase that
    // merely intend to agree is exactly the kind of drift this catches.
    {
        Equalizer low;
        low.prepare(sr, 1);
        low.setParams(eqParams({ eqBand(FilterKind::LRLP, 800.0, 0.0, 0.0) }));

        LinkwitzRiley4 ref;
        ref.design(800.0, sr);

        const int n = 4096;
        std::vector<float> mine(size_t(n), 0.0f);
        mine[0] = 1.0f;
        {
            float *ch[1] = { mine.data() };
            AudioBuffer buf{ ch, 1, n };
            low.process(buf);
        }

        double worst = 0.0;
        for (int i = 0; i < n; ++i) {
            float lo = 0.0f, hi = 0.0f;
            ref.process(i == 0 ? 1.0f : 0.0f, &lo, &hi);
            worst = std::max(worst, double(std::fabs(mine[size_t(i)] - lo)));
        }
        check(worst < 1e-6, fmt("LRLP matches LinkwitzRiley4 to %.3g", worst));
    }

    // --- transparency and gain ---------------------------------------------
    {
        Equalizer eq;
        eq.prepare(sr, 2);

        Equalizer::Params none;
        std::memset(&none, 0, sizeof none);
        eq.setParams(none);

        const int n = 1024;
        // The second argument is not decoration: `l(size_t(n))` declares a
        // function returning std::vector<float>, which is the seventh time that
        // parse has been hit in this codebase.
        std::vector<float> l(size_t(n), 0.0f), r(size_t(n), 0.0f);
        uint32_t seed = 0x1234567u;
        for (int i = 0; i < n; ++i) {
            l[size_t(i)] = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;
            r[size_t(i)] = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;
        }
        const std::vector<float> origL = l;

        float *ch[2] = { l.data(), r.data() };
        AudioBuffer buf{ ch, 2, n };
        eq.process(buf);
        check(std::memcmp(l.data(), origL.data(), size_t(n) * sizeof(float)) == 0,
              "no bands is bit-exact passthrough");

        // -6.0206 dB is exactly one half.
        Equalizer::Params half = none;
        half.preampDb = -6.020599913f;
        eq.setParams(half);
        // Copied into the existing storage rather than assigned, so the
        // pointers already handed to `buf` stay valid.
        std::copy(origL.begin(), origL.end(), l.begin());
        eq.process(buf);
        double worst = 0.0;
        for (int i = 0; i < n; ++i)
            worst = std::max(worst, double(std::fabs(l[size_t(i)] - origL[size_t(i)] * 0.5f)));
        check(worst < 1e-6, fmt("preamp -6.02 dB halves the signal (err %.3g)", worst));
    }

    // --- the rail ----------------------------------------------------------
    //
    // The stage's own bound on what it can emit, which is the only thing
    // standing between a hostile parameter file and a full-scale blast inside
    // audiodg.
    {
        Equalizer eq;
        eq.prepare(sr, 1);
        Equalizer::Params loud;
        std::memset(&loud, 0, sizeof loud);
        loud.preampDb = 60.0f;
        eq.setParams(loud);

        std::vector<float> x(64, 1.0f);
        float *ch[1] = { x.data() };
        AudioBuffer buf{ ch, 1, 64 };
        eq.process(buf);

        bool railed = true;
        for (float v : x)
            if (!(v == 4.0f))
                railed = false;
        check(railed, "+60 dB of preamp is railed at +12 dBFS");
    }

    // A NaN arriving from an upstream processing object must not become a
    // permanent silence: the sample is dropped and the filter state cleared, so
    // the next buffer is correct again.
    {
        Equalizer eq, clean;
        eq.prepare(sr, 1);
        clean.prepare(sr, 1);
        const Equalizer::Params p = eqParams({ eqBand(FilterKind::PK, 1000.0, 9.0, 1.4) });
        eq.setParams(p);
        clean.setParams(p);

        std::vector<float> poison(512, 0.0f);
        poison[10] = std::numeric_limits<float>::quiet_NaN();
        {
            float *ch[1] = { poison.data() };
            AudioBuffer buf{ ch, 1, 512 };
            eq.process(buf);
        }
        bool anyNan = false;
        for (float v : poison)
            if (v != v)
                anyNan = true;
        check(!anyNan, "a NaN input does not leave the equalizer");

        std::vector<float> after(1024), reference(1024);
        for (int i = 0; i < 1024; ++i) {
            const float s = 0.25f * float(std::sin(2.0 * 3.14159265358979 * 1000.0 * i / sr));
            after[size_t(i)] = s;
            reference[size_t(i)] = s;
        }
        {
            float *ca[1] = { after.data() };
            float *cb[1] = { reference.data() };
            AudioBuffer ba{ ca, 1, 1024 };
            AudioBuffer bb{ cb, 1, 1024 };
            eq.process(ba);
            clean.process(bb);
        }
        double worst = 0.0;
        for (int i = 0; i < 1024; ++i)
            worst = std::max(worst, double(std::fabs(after[size_t(i)] - reference[size_t(i)])));
        check(worst < 1e-6, fmt("the equalizer recovers from a NaN (err %.3g)", worst));
    }

    // --- nothing in the whole parameter space can run away ------------------
    //
    // Every type, at frequencies from below the audible band to above Nyquist,
    // at extreme gains and Qs, on four sample rates. The claim being tested is
    // the one the design makes: an unstable section becomes a wire rather than
    // an exponentially growing signal inside a system process.
    {
        const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
        const double freqs[] = { 10.0, 20.0, 1000.0, 19000.0, 21000.0, 100000.0, 380000.0 };
        const double gains[] = { -40.0, -12.0, 0.0, 12.0, 40.0 };
        const double qs[] = { 0.05, 0.5, 1.41, 12.0, 100.0 };

        long unstable = 0, misbehaved = 0, cases = 0;
        std::vector<float> x(256);

        for (double rate : rates) {
            for (int k = 0; k < int(FilterKind::Count); ++k) {
                for (double f : freqs) {
                    for (double g : gains) {
                        for (double q : qs) {
                            ++cases;
                            const FilterSections fs =
                                designFilter(FilterKind(k), f, g, q, rate);
                            for (int s = 0; s < fs.count; ++s) {
                                const BiquadCoeffs &c = fs.section[size_t(s)];
                                if (!(std::fabs(c.a2) < 1.0)
                                    || !(std::fabs(c.a1) < 1.0 + c.a2))
                                    ++unstable;
                            }

                            Equalizer eq;
                            eq.prepare(rate, 1);
                            eq.setParams(eqParams({ eqBand(FilterKind(k), f, g, q) },
                                                  30.0f));
                            uint32_t seed = 0xBEEF0000u + uint32_t(cases);
                            for (auto &v : x)
                                v = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;
                            float *ch[1] = { x.data() };
                            AudioBuffer buf{ ch, 1, int(x.size()) };
                            for (int pass = 0; pass < 8; ++pass)
                                eq.process(buf);
                            for (float v : x)
                                if (!(v >= -4.0f && v <= 4.0f))
                                    ++misbehaved;
                        }
                    }
                }
            }
        }
        check(unstable == 0, fmt("no unstable section over %.0f designs", double(cases)));
        check(misbehaved == 0,
              fmt("every output stays inside the rail over %.0f cases", double(cases)));
    }

    // --- wire format --------------------------------------------------------
    {
        ParamBlock b = transparentBlock();
        b.eq.bandCount = 999u;
        b.eq.band[0] = eqBand(FilterKind::PK, 1000.0, 6.0, 1.4);
        b.eq.band[Equalizer::kMaxBands - 1] = eqBand(FilterKind::PK, 8000.0, -3.0, 2.0);
        b.eq.preampDb = 1e30f;
        const ParamBlock s = sanitise(b);
        check(s.eq.bandCount == uint32_t(Equalizer::kMaxBands), "band count is clamped");
        check(s.eq.preampDb <= 30.0f && s.eq.preampDb >= -60.0f, "preamp is clamped");
        check(isSane(s), "a sanitised block with bands is sane");

        // Unused slots must hold a fixed pattern, or the same settings would
        // produce different bytes on disk depending on history.
        ParamBlock c = transparentBlock();
        c.eq.bandCount = 2u;
        c.eq.band[0] = eqBand(FilterKind::PK, 100.0, 1.0, 1.0);
        c.eq.band[1] = eqBand(FilterKind::PK, 200.0, 2.0, 1.0);
        c.eq.band[7] = eqBand(FilterKind::HSQ, 9000.0, 5.0, 0.7);
        const ParamBlock cs = sanitise(c);
        EqBand zero;
        std::memset(&zero, 0, sizeof zero);
        check(std::memcmp(&cs.eq.band[7], &zero, sizeof zero) == 0,
              "slots past bandCount are zeroed");
    }
}

// -------------------------------------------------------- routing and delay

void testRouting()
{
    std::printf("\n[routing]\n");

    constexpr double sr = 48000.0;
    constexpr int frames = 512;

    // --- the matrix ---------------------------------------------------------
    {
        ChannelMatrix m;
        m.prepare(sr, 2, frames);
        check(ChannelMatrix::isIdentity(ChannelMatrix::identity()), "identity() is the identity");

        std::vector<float> l(size_t(frames), 0.0f), r(size_t(frames), 0.0f);
        for (int i = 0; i < frames; ++i) {
            l[size_t(i)] = 0.25f;
            r[size_t(i)] = -0.5f;
        }
        const std::vector<float> l0 = l, r0 = r;

        float *ch[2] = { l.data(), r.data() };
        AudioBuffer buf{ ch, 2, frames };

        m.setParams(ChannelMatrix::identity());
        m.process(buf);
        check(std::memcmp(l.data(), l0.data(), size_t(frames) * sizeof(float)) == 0
                  && std::memcmp(r.data(), r0.data(), size_t(frames) * sizeof(float)) == 0,
              "the identity matrix is bit-exact passthrough");

        // A swap is the case that catches an in-place implementation: doing it
        // channel by channel over the live buffer gives R in both outputs.
        ChannelMatrix::Params swap;
        std::memset(&swap, 0, sizeof swap);
        swap.gain[0][1] = 1.0f;
        swap.gain[1][0] = 1.0f;
        for (int c = 2; c < ChannelMatrix::kMaxChannels; ++c)
            swap.gain[c][c] = 1.0f;

        std::copy(l0.begin(), l0.end(), l.begin());
        std::copy(r0.begin(), r0.end(), r.begin());
        m.setParams(swap);
        m.process(buf);
        bool swapped = true;
        for (int i = 0; i < frames; ++i)
            if (l[size_t(i)] != r0[size_t(i)] || r[size_t(i)] != l0[size_t(i)])
                swapped = false;
        check(swapped, "L and R swap without either being overwritten first");

        // Downmix to mono at -6 dB, the other common use.
        ChannelMatrix::Params mono;
        std::memset(&mono, 0, sizeof mono);
        for (int o = 0; o < 2; ++o)
            mono.gain[o][0] = mono.gain[o][1] = 0.5f;
        std::copy(l0.begin(), l0.end(), l.begin());
        std::copy(r0.begin(), r0.end(), r.begin());
        m.setParams(mono);
        m.process(buf);
        const float want = 0.5f * (0.25f + -0.5f);
        bool monoOk = true;
        for (int i = 0; i < frames; ++i)
            if (std::fabs(l[size_t(i)] - want) > 1e-7f || std::fabs(r[size_t(i)] - want) > 1e-7f)
                monoOk = false;
        check(monoOk, fmt("mono downmix gives %.4f in both channels", double(want)));
    }

    // --- the delay ----------------------------------------------------------
    //
    // A whole-sample delay has to be bit-exact: the interpolator's coefficients
    // collapse to (0, 1, 0, 0) at a zero fractional part, and if they do not the
    // most common setting is the one that is quietly wrong.
    {
        ChannelDelay d;
        d.prepare(sr, 1, 1024);

        // 1 ms at 48 kHz is 48 samples, and both 1.0f and 48000/1000 are exact
        // in floating point -- so the fractional part really is zero rather than
        // 4e-6 away from it, and "bit-exact" is a fair thing to demand.
        ChannelDelay::Params p;
        std::memset(&p, 0, sizeof p);
        p.ms[0] = 1.0f;
        const int delaySamples = 48;
        d.setParams(p);

        const int n = 1024;
        std::vector<float> x(size_t(n), 0.0f);
        uint32_t seed = 0x51DE1u;
        for (auto &v : x)
            v = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;
        const std::vector<float> x0 = x;

        float *ch[1] = { x.data() };
        AudioBuffer buf{ ch, 1, n };
        d.process(buf);

        int bad = 0;
        for (int i = delaySamples; i < n; ++i)
            if (x[size_t(i)] != x0[size_t(i - delaySamples)])
                ++bad;
        check(bad == 0, fmt("a 48-sample delay is bit-exact (%.0f wrong)", double(bad)));

        bool leadingSilence = true;
        for (int i = 0; i < delaySamples; ++i)
            if (x[size_t(i)] != 0.0f)
                leadingSilence = false;
        check(leadingSilence, "the delay starts from silence, not from stale samples");
    }

    // A fractional delay is checked against the thing it is for: a sine shifted
    // by a non-integer number of samples. Third-order Lagrange is not exact, so
    // the tolerance is a level rather than zero -- but it has to be far below
    // what rounding to the nearest sample would cost, or there is no point.
    {
        ChannelDelay d;
        d.prepare(sr, 1, 4096);

        const double delaySamples = 40.5;
        ChannelDelay::Params p;
        std::memset(&p, 0, sizeof p);
        p.ms[0] = float(delaySamples * 1000.0 / sr);
        d.setParams(p);

        const int n = 4096;
        const double f0 = 1000.0;
        std::vector<float> x(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i)
            x[size_t(i)] = float(0.5 * std::sin(2.0 * 3.14159265358979 * f0 * i / sr));

        float *ch[1] = { x.data() };
        AudioBuffer buf{ ch, 1, n };
        d.process(buf);

        double worst = 0.0;
        for (int i = 200; i < n; ++i) {
            const double want = 0.5 * std::sin(2.0 * 3.14159265358979 * f0
                                               * (double(i) - delaySamples) / sr);
            worst = std::max(worst, std::fabs(double(x[size_t(i)]) - want));
        }
        // Half a sample of rounding error at 1 kHz would be 0.033 of amplitude.
        check(worst < 1e-3, fmt("a 40.5-sample delay of a 1 kHz sine is within %.2e", worst));
    }

    // Zero delay must be a wire, and must still fill the line so that switching
    // a channel on later does not read history that was never recorded.
    {
        ChannelDelay d;
        d.prepare(sr, 2, 256);
        ChannelDelay::Params p;
        std::memset(&p, 0, sizeof p);
        d.setParams(p);

        std::vector<float> a(256, 0.25f), b(256, -0.125f);
        const std::vector<float> a0 = a;
        float *ch[2] = { a.data(), b.data() };
        AudioBuffer buf{ ch, 2, 256 };
        d.process(buf);
        check(std::memcmp(a.data(), a0.data(), a0.size() * sizeof(float)) == 0,
              "zero delay is bit-exact passthrough");
        check(d.latencySamples() == 0, "zero delay reports no latency");
    }
}

// ------------------------------------------------------------------ loudness

void testLoudness()
{
    std::printf("\n[loudness correction]\n");

    constexpr double sr = 48000.0;

    // At the reference level the stage is a wire. This is the setting the
    // control sits at most of the time, so it is the one that must cost nothing
    // and change nothing.
    {
        LoudnessCorrection::Params p;
        std::memset(&p, 0, sizeof p);
        p.volumeDb = -20.0f;
        p.referenceDb = -20.0f;
        p.amount = 1.0f;

        double lo = 1.0, hi = 1.0, pre = 1.0;
        LoudnessCorrection::shelvesFor(p, &lo, &hi, &pre);
        check(lo == 0.0 && hi == 0.0 && pre == 0.0, "at the reference level nothing is applied");

        LoudnessCorrection l;
        l.prepare(sr, 2);
        l.setParams(p);
        check(l.neutral(), "and the stage reports itself neutral");

        std::vector<float> a(512, 0.3f), b(512, -0.2f);
        const std::vector<float> a0 = a;
        float *ch[2] = { a.data(), b.data() };
        AudioBuffer buf{ ch, 2, 512 };
        l.process(buf);
        check(std::memcmp(a.data(), a0.data(), a0.size() * sizeof(float)) == 0,
              "neutral is bit-exact passthrough");
    }

    // Below the reference: bass up, and an equal preamp down, so the net effect
    // is the midrange coming down rather than the low end eating the headroom.
    // The numbers are Equalizer APO's formula, reproduced exactly.
    {
        LoudnessCorrection::Params p;
        std::memset(&p, 0, sizeof p);
        p.volumeDb = -20.0f;
        p.referenceDb = 0.0f;
        p.amount = 1.0f;

        double lo = 0.0, hi = 0.0, pre = 0.0;
        LoudnessCorrection::shelvesFor(p, &lo, &hi, &pre);
        const double wantLow = 20.0 * 0.55 / (1.0 - 0.55);
        check(std::fabs(lo - wantLow) < 1e-9, fmt("20 dB down -> %.3f dB low shelf", lo));
        check(std::fabs(pre + lo) < 1e-9, "the preamp cancels the low shelf exactly");
        // The high shelf is evaluated at the already-preamped level, so the
        // preamp's 24 dB is added to the distance from the reference before it
        // is worked out -- it comes out positive, restoring some of what the
        // preamp took off the top end rather than cutting further.
        check(hi > 0.0, fmt("the high shelf lifts the top back up by %.3f dB", hi));

        // Halving the amount halves both shelves: the control has to be a scale
        // on the correction, not a second curve.
        LoudnessCorrection::Params half = p;
        half.amount = 0.5f;
        double lo2 = 0.0, hi2 = 0.0, pre2 = 0.0;
        LoudnessCorrection::shelvesFor(half, &lo2, &hi2, &pre2);
        check(std::fabs(lo2 - lo * 0.5) < 1e-9, "amount 0.5 halves the low shelf");
    }

    // What the stage actually does to audio must be the shelves it says it is
    // applying -- measured, not asserted from the design.
    {
        LoudnessCorrection::Params p;
        std::memset(&p, 0, sizeof p);
        p.volumeDb = -10.0f;
        p.referenceDb = 0.0f;
        p.amount = 0.5f;

        LoudnessCorrection l;
        l.prepare(sr, 1);
        l.setParams(p);
        check(!l.neutral(), "10 dB below reference is not neutral");

        Fft fft(8192);
        const int n = fft.realSize();
        std::vector<float> imp(size_t(n), 0.0f);
        imp[0] = 1.0f;
        {
            float *ch[1] = { imp.data() };
            AudioBuffer buf{ ch, 1, n };
            l.process(buf);
        }
        std::vector<std::complex<float>> spec(size_t(fft.realBins()));
        fft.realForward(imp.data(), spec.data());

        const auto binAt = [&](double hz) {
            return size_t(std::lround(hz * double(n) / sr));
        };
        const double atLow = 20.0 * std::log10(double(std::abs(spec[binAt(20.0)])) + 1e-20);
        const double atMid = 20.0 * std::log10(double(std::abs(spec[binAt(1000.0)])) + 1e-20);
        const double atHigh = 20.0 * std::log10(double(std::abs(spec[binAt(18000.0)])) + 1e-20);

        // Measured against what the stage says it is applying, rather than
        // against a guess. The tolerance is half a dB because 20 Hz is only
        // 1.9 octaves below a 75 Hz shelf with Q 0.52 -- it has not quite
        // reached its plateau there, and neither has the 10 kHz shelf at 18 kHz.
        check(std::fabs(atMid - l.preampDb()) < 0.1,
              fmt("the midrange sits at the preamp: %.3f dB vs %.3f", atMid, l.preampDb()));
        check(std::fabs((atLow - atMid) - l.lowShelfDb()) < 0.5,
              fmt("the low shelf measures %.3f dB against a designed %.3f",
                  atLow - atMid, l.lowShelfDb()));
        check(std::fabs((atHigh - atMid) - l.highShelfDb()) < 0.5,
              fmt("the high shelf measures %.3f dB against a designed %.3f",
                  atHigh - atMid, l.highShelfDb()));
    }
}

// ------------------------------------------------------------------- limiter

void testLimiter()
{
    std::printf("\n[limiter]\n");

    constexpr double sr = 48000.0;
    constexpr int n = 8192;

    // The threshold is documented as a guarantee rather than an aim, so it is
    // tested as one: a full-scale sine against a -6 dB ceiling, and not one
    // sample allowed through above it.
    {
        Limiter lim;
        lim.prepare(sr, 2, 1024);
        Limiter::Params p;
        p.gainDb = 0.0f;
        p.thresholdDb = -6.0f;
        p.releaseMs = 50.0f;
        p.lookaheadMs = 1.5f;
        lim.setParams(p);

        std::vector<float> l(size_t(n), 0.0f), r(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i) {
            const float v = float(std::sin(2.0 * 3.14159265358979 * 220.0 * i / sr));
            l[size_t(i)] = v;
            r[size_t(i)] = v;
        }
        float *ch[2] = { l.data(), r.data() };
        AudioBuffer buf{ ch, 2, n };
        lim.process(buf);

        const float thr = std::pow(10.0f, -6.0f / 20.0f);
        float worst = 0.0f;
        for (float v : l)
            worst = std::max(worst, std::fabs(v));
        check(worst <= thr * 1.0001f,
              fmt("peak %.5f never exceeds the %.5f threshold", double(worst), double(thr)));
        // ...and it did not simply mute everything to get there.
        check(worst > thr * 0.9f, fmt("and it reaches it (%.5f)", double(worst)));
    }

    // Material below the threshold has to come out untouched, delayed by the
    // look-ahead and nothing more. A limiter that colours quiet passages is
    // worse than no limiter.
    {
        Limiter lim;
        lim.prepare(sr, 1, 1024);
        Limiter::Params p;
        p.thresholdDb = -6.0f;
        p.lookaheadMs = 1.0f;      // exactly 48 samples
        lim.setParams(p);
        const int look = lim.latencySamples();
        check(look == 48, fmt("1 ms of look-ahead is %.0f samples", double(look)));

        std::vector<float> x(size_t(n), 0.0f);
        uint32_t seed = 0xA11CEu;
        for (auto &v : x)
            v = 0.1f * float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;
        const std::vector<float> x0 = x;

        float *ch[1] = { x.data() };
        AudioBuffer buf{ ch, 1, n };
        lim.process(buf);

        int bad = 0;
        for (int i = look; i < n; ++i)
            if (x[size_t(i)] != x0[size_t(i - look)])
                ++bad;
        check(bad == 0, fmt("quiet material passes bit-exact (%.0f wrong)", double(bad)));
    }

    // The pre-gain is before the limiter, which is the whole point of putting it
    // there: turning it up makes the limiter work, not the output louder.
    {
        Limiter lim;
        lim.prepare(sr, 1, 1024);
        Limiter::Params p;
        p.gainDb = 20.0f;
        p.thresholdDb = -3.0f;
        p.releaseMs = 20.0f;
        lim.setParams(p);

        std::vector<float> x(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i)
            x[size_t(i)] = 0.2f * float(std::sin(2.0 * 3.14159265358979 * 440.0 * i / sr));
        float *ch[1] = { x.data() };
        AudioBuffer buf{ ch, 1, n };
        lim.process(buf);

        const float thr = std::pow(10.0f, -3.0f / 20.0f);
        float worst = 0.0f;
        for (float v : x)
            worst = std::max(worst, std::fabs(v));
        check(worst <= thr * 1.0001f,
              fmt("+20 dB of pre-gain still lands under the ceiling (%.5f)", double(worst)));
        // 0.2 lifted 20 dB is 2.0 against a 0.708 ceiling: 20*log10(0.708/2)
        // is -9.02 dB, and the meter has to say exactly that rather than
        // something plausible.
        check(std::fabs(lim.reductionDb() + 9.02) < 0.1,
              fmt("and the meter reads the arithmetic: %.2f dB", lim.reductionDb()));
    }
}

// --------------------------------------------------------- dynamic bass boost

void testDynamicBass()
{
    std::printf("\n[dynamic bass]\n");

    constexpr double sr = 48000.0;
    constexpr int n = 24000;   // half a second, long enough for the envelope

    const auto runTone = [&](float amplitude, float maxGainDb) {
        DynamicBass b;
        b.prepare(sr, 1);
        DynamicBass::Params p;
        p.maxGainDb = maxGainDb;
        p.cutoffHz = 100.0f;
        p.releaseMs = 100.0f;
        b.setParams(p);

        std::vector<float> x(size_t(n), 0.0f);
        for (int i = 0; i < n; ++i)
            x[size_t(i)] = amplitude * float(std::sin(2.0 * 3.14159265358979 * 50.0 * i / sr));
        float *ch[1] = { x.data() };
        AudioBuffer buf{ ch, 1, n };
        b.process(buf);

        float peak = 0.0f;
        for (int i = n / 2; i < n; ++i)   // steady state only
            peak = std::max(peak, std::fabs(x[size_t(i)]));
        return std::make_pair(peak, b.appliedDb());
    };

    // A quiet low end gets the whole boost, because there is room for it.
    {
        const auto [peak, db] = runTone(0.05f, 12.0f);
        check(db > 11.5, fmt("a -26 dBFS tone gets the full +%.2f dB", db));
        check(peak > 0.05f * 3.5f, fmt("and comes out at %.3f", double(peak)));
    }

    // A loud one gets whatever is left and no more, which is the property the
    // design is built around: the envelope is measured before the boost, so the
    // gain is exactly the headroom and the loop cannot chase itself.
    //
    // The output lands a little over full scale even so, and the tolerance says
    // so rather than hiding it: the boosted band is bounded, but the stage
    // emits that band summed with the one above the crossover, and at 50 Hz
    // against a 100 Hz split the high band still contributes. Half a decibel,
    // and the limiter is what turns a bound like this into a ceiling.
    {
        const auto [peak, db] = runTone(0.9f, 12.0f);
        check(db < 1.5, fmt("a -0.9 dBFS tone gets only +%.2f dB", db));
        check(peak <= 1.10f, fmt("and lands within half a dB of full scale (%.3f)", double(peak)));
    }

    // The behaviour that actually matters, as a ratio: nearly nothing when the
    // material is already loud, the whole boost when it is not.
    {
        const auto [loudPeak, loudDb] = runTone(0.9f, 12.0f);
        const auto [quietPeak, quietDb] = runTone(0.05f, 12.0f);
        (void)loudPeak; (void)quietPeak;
        check(quietDb - loudDb > 9.0,
              fmt("%.2f dB of boost on quiet material against %.2f on loud", quietDb, loudDb));
    }

    // At a maximum of 0 dB the stage is a crossover that puts its two bands
    // straight back together. That is an all-pass, not a wire -- the magnitude
    // is untouched but the phase is not, which is worth stating rather than
    // discovering.
    {
        DynamicBass b;
        b.prepare(sr, 1);
        DynamicBass::Params p;
        p.maxGainDb = 0.0f;
        b.setParams(p);

        Fft fft(4096);
        const int len = fft.realSize();
        std::vector<float> imp(size_t(len), 0.0f);
        imp[0] = 1.0f;
        float *ch[1] = { imp.data() };
        AudioBuffer buf{ ch, 1, len };
        b.process(buf);

        std::vector<std::complex<float>> spec(size_t(fft.realBins()));
        fft.realForward(imp.data(), spec.data());
        double worst = 0.0;
        for (size_t i = 1; i < spec.size(); ++i) {
            const double hz = double(i) * sr / double(len);
            if (hz < 20.0 || hz > 20000.0)
                continue;
            worst = std::max(worst,
                             std::fabs(20.0 * std::log10(double(std::abs(spec[i])) + 1e-20)));
        }
        check(worst < 0.05, fmt("at +0 dB the magnitude is flat to %.4f dB", worst));
    }
}

// -------------------------------------------------------------- graphic eq

void testGraphicEq()
{
    std::printf("\n[graphic eq]\n");

    constexpr double sr = 48000.0;
    const double *f = GraphicEq::centres();

    // A curve with a bass lift, a presence dip and a treble tilt -- the shape
    // an AutoEQ correction actually has.
    double target[GraphicEq::kBands];
    for (int i = 0; i < GraphicEq::kBands; ++i) {
        const double hz = f[i];
        target[i] = 6.0 * std::exp(-std::pow(std::log(hz / 45.0), 2.0))
                    - 4.5 * std::exp(-std::pow(std::log(hz / 3000.0) * 1.6, 2.0))
                    - 2.0 * std::log10(hz / 1000.0);
    }

    float gains[GraphicEq::kBands];
    const double residual = fitGraphicEq(target, sr, gains);
    check(residual < 0.25,
          fmt("the fit lands within %.4f dB of the requested curve at the centres", residual));

    // Setting each band to its own target without solving for the overlap is
    // the naive thing to do, and it is wrong by several dB. Worth measuring, so
    // the iteration is justified by a number rather than by an assertion.
    {
        double naiveWorst = 0.0;
        FilterSections s[GraphicEq::kBands];
        for (int i = 0; i < GraphicEq::kBands; ++i)
            s[i] = designFilter(FilterKind::PK, f[i], target[i], GraphicEq::kQ, sr);
        for (int i = 0; i < GraphicEq::kBands; ++i) {
            double got = 0.0;
            for (int j = 0; j < GraphicEq::kBands; ++j)
                got += magnitudeDb(s[j], f[i], sr);
            naiveWorst = std::max(naiveWorst, std::fabs(target[i] - got));
        }
        check(naiveWorst > 1.0,
              fmt("...where not solving for the overlap would be off by %.2f dB", naiveWorst));
    }

    // And what actually comes out of process() is that curve, measured.
    {
        GraphicEq g;
        g.prepare(sr, 1);
        GraphicEq::Params p;
        std::memcpy(p.gainDb, gains, sizeof p.gainDb);
        p.amount = 1.0f;
        g.setParams(p);

        Fft fft(8192);
        const int len = fft.realSize();
        std::vector<float> imp(size_t(len), 0.0f);
        imp[0] = 1.0f;
        {
            float *ch[1] = { imp.data() };
            AudioBuffer buf{ ch, 1, len };
            g.process(buf);
        }
        std::vector<std::complex<float>> spec(size_t(fft.realBins()));
        fft.realForward(imp.data(), spec.data());

        double worst = 0.0;
        for (int i = 0; i < GraphicEq::kBands; ++i) {
            if (f[i] < 30.0 || f[i] > 18000.0)
                continue;
            const size_t bin = size_t(std::lround(f[i] * double(len) / sr));
            const double got = 20.0 * std::log10(double(std::abs(spec[bin])) + 1e-20);
            worst = std::max(worst, std::fabs(got - target[i]));
        }
        check(worst < 0.4,
              fmt("the measured response follows the requested curve to %.4f dB", worst));
    }

    // Amount scales the whole thing; at zero the stage is thirty-one identity
    // filters and has to be inaudible.
    {
        GraphicEq g;
        g.prepare(sr, 1);
        GraphicEq::Params p;
        std::memcpy(p.gainDb, gains, sizeof p.gainDb);
        p.amount = 0.0f;
        g.setParams(p);

        const int len = 512;
        std::vector<float> x(size_t(len), 0.0f);
        uint32_t seed = 0x6E401u;
        for (auto &v : x)
            v = 0.25f * float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;
        const std::vector<float> x0 = x;
        float *ch[1] = { x.data() };
        AudioBuffer buf{ ch, 1, len };
        g.process(buf);

        double worst = 0.0;
        for (int i = 0; i < len; ++i)
            worst = std::max(worst, double(std::fabs(x[size_t(i)] - x0[size_t(i)])));
        check(worst < 1e-6, fmt("amount 0 is inaudible (%.3g)", worst));
    }
}

// ---------------------------------------------------------------- chain order

void testMasterBypass()
{
    std::printf("\n[master bypass]\n");
    constexpr double sr = 48000.0;
    const int n = 2048;

    // Something audible to switch out: an equalizer with real gain in it.
    ParamBlock p = transparentBlock();
    p.enableMask = kEnEqualizer | kEnTube;
    p.eq.bandCount = 2;
    p.eq.band[0] = eqBand(FilterKind::PK, 1000.0, 9.0, 1.0);
    p.eq.band[1] = eqBand(FilterKind::HS, 6000.0, -6.0, 0.7);
    p.tube.drive = 6.0f;
    p.tube.mix = 0.8f;
    p = sanitise(p);

    const auto run = [&](const ParamBlock &block) {
        EffectChain chain;
        chain.prepare(sr, 2, n);
        chain.apply(block);
        auto l = sine(997.0, sr, n, 0.4f);
        auto r = l;
        float *ch[2] = { l.data(), r.data() };
        AudioBuffer b{ ch, 2, n };
        chain.process(b);
        return std::make_pair(l, chain.activeMask());
    };

    const auto dry = sine(997.0, sr, n, 0.4f);
    const auto [wet, wetMask] = run(p);

    double worstWet = 0.0;
    for (size_t i = 0; i < wet.size(); ++i)
        worstWet = std::max(worstWet, double(std::fabs(wet[i] - dry[i])));
    std::printf("    with the rack running: mask 0x%05X, worst change %.4f\n",
                wetMask, worstWet);
    check(worstWet > 1e-3, "the test block actually does something");

    // Bypassed: bit-exact passthrough, and the switch positions untouched.
    ParamBlock bp = p;
    bp.flags |= kPfBypass;
    bp = sanitise(bp);
    const auto [byp, bypMask] = run(bp);

    int differing = 0;
    for (size_t i = 0; i < byp.size(); ++i)
        if (byp[i] != dry[i])
            ++differing;
    std::printf("    bypassed: mask 0x%05X, enableMask still 0x%05X, %d samples differ\n",
                bypMask, bp.enableMask, differing);
    check(bypMask == 0u, "the bypass switches every stage out");
    check(differing == 0, fmt("and does it bit-exactly (%.0f samples differ)",
                              double(differing)));

    // The point of a flag rather than an empty mask: this file is also the
    // session store, so the switch positions have to survive being bypassed.
    check(bp.enableMask == p.enableMask,
          "a bypassed block still remembers which effects are switched on");
    check((sanitise(bp).flags & kPfBypass) != 0u, "the flag survives sanitising");

    // And an unknown flag bit is not a configuration.
    ParamBlock junk = p;
    junk.flags = 0xFFFFFFFFu;
    check((sanitise(junk).flags & ~uint32_t(kPfKnown)) == 0u,
          "unknown header flags are dropped");
}


void testChainOrder()
{
    std::printf("\n[chain order]\n");

    // A non-permutation is replaced wholesale, not patched. A duplicate would
    // run a stage twice and an omission would drop an effect whose switch says
    // it is on, and neither is something a user could have asked for.
    {
        ParamBlock b = transparentBlock();
        check(std::memcmp(b.order, kDefaultOrder, sizeof b.order) == 0,
              "the transparent block carries the default order");

        b.order[3] = b.order[4];                 // a duplicate
        ParamBlock s = sanitise(b);
        check(std::memcmp(s.order, kDefaultOrder, sizeof s.order) == 0,
              "a duplicated stage falls back to the default order");

        b = transparentBlock();
        b.order[0] = 99;                         // out of range
        s = sanitise(b);
        check(std::memcmp(s.order, kDefaultOrder, sizeof s.order) == 0,
              "an out-of-range stage falls back to the default order");

        // A genuine permutation survives untouched.
        b = transparentBlock();
        for (int i = 0; i < kStageCount; ++i)
            b.order[i] = uint8_t(kStageCount - 1 - i);
        s = sanitise(b);
        bool reversed = true;
        for (int i = 0; i < kStageCount; ++i)
            reversed = reversed && s.order[i] == uint8_t(kStageCount - 1 - i);
        check(reversed, "a real permutation survives sanitising");
        check(isSane(s), "and the block is sane");
    }

    // The order has to actually change the audio, or it is a setting that does
    // nothing. An equalizer preamp of +12 dB and a limiter at -6 dB: with the
    // equalizer first the limiter catches it, and the other way round it does
    // not, because nothing runs after the ceiling.
    {
        constexpr double sr = 48000.0;
        constexpr int n = 8192;
        const float thr = std::pow(10.0f, -6.0f / 20.0f);

        const auto run = [&](bool eqFirst) {
            ParamBlock p = transparentBlock();
            p.enableMask = kEnEqualizer | kEnLimiter;
            p.eq.preampDb = 12.0f;
            p.eq.bandCount = 0;
            p.limiter.thresholdDb = -6.0f;
            p.limiter.releaseMs = 20.0f;
            p.limiter.lookaheadMs = 1.0f;
            p.generation = eqFirst ? 1u : 2u;

            // Everything else keeps its default position; only these two move.
            int w = 0;
            uint8_t order[kStageCount];
            if (eqFirst) {
                order[w++] = kStageEqualizer;
                order[w++] = kStageLimiter;
            } else {
                order[w++] = kStageLimiter;
                order[w++] = kStageEqualizer;
            }
            for (int i = 0; i < kStageCount; ++i) {
                const uint8_t s = kDefaultOrder[i];
                if (s != kStageEqualizer && s != kStageLimiter)
                    order[w++] = s;
            }
            std::memcpy(p.order, order, sizeof order);
            p = sanitise(p);

            EffectChain chain;
            chain.prepare(sr, 1, n);
            chain.apply(p);

            std::vector<float> x(size_t(n), 0.0f);
            for (int i = 0; i < n; ++i)
                x[size_t(i)] = 0.2f * float(std::sin(2.0 * 3.14159265358979 * 300.0 * i / sr));
            float *ch[1] = { x.data() };
            AudioBuffer buf{ ch, 1, n };
            chain.process(buf);

            float peak = 0.0f;
            for (int i = n / 2; i < n; ++i)
                peak = std::max(peak, std::fabs(x[size_t(i)]));
            return peak;
        };

        const float limited = run(true);
        const float unlimited = run(false);

        check(limited <= thr * 1.0001f,
              fmt("equalizer then limiter: peak %.4f is under the ceiling", double(limited)));
        check(unlimited > thr * 1.5f,
              fmt("limiter then equalizer: peak %.4f is not (%.2f dB higher)",
                  double(unlimited), 20.0 * std::log10(double(unlimited / limited))));
    }
}

// ------------------------------------------------------------ impulse analysis

void testImpulseAnalysis()
{
    std::printf("\n[impulse analysis]\n");

    constexpr double sr = 48000.0;
    const int taps = 8192;

    // The impulse response of a filter whose magnitude response is known in
    // closed form, so the measured curve has something exact to be wrong
    // against.
    auto responseOf = [&](BiquadFilter::Kind kind, double freq, double q, double gainDb) {
        BiquadFilter f;
        f.design(kind, freq, q, gainDb, sr);
        f.reset();
        std::vector<float> ir(size_t(taps), 0.0f);
        ir[0] = f.process(1.0f);
        for (int i = 1; i < taps; ++i)
            ir[size_t(i)] = f.process(0.0f);
        return ir;
    };

    // A flat filter must measure flat.
    {
        std::vector<float> ir(size_t(taps), 0.0f);
        ir[0] = 1.0f;
        const ImpulseCurve c = impulseMagnitudeResponse(ir.data(), taps, sr);
        float worst = 0.0f;
        for (float v : c.magnitudeDb)
            worst = std::max(worst, std::fabs(v));
        check(c.valid() && worst < 0.05f,
              fmt("a bare impulse measures flat (worst %.3f dB)", double(worst)));
    }

    // A peaking filter must show its peak at the right frequency and height.
    {
        const std::vector<float> ir = responseOf(BiquadFilter::Kind::Peak, 1000.0, 2.0, 9.0);
        const ImpulseCurve c = impulseMagnitudeResponse(ir.data(), taps, sr, 512);

        // Referred to the peak, so the peak itself is 0 dB and the flat parts
        // sit at -9. Find where the maximum is.
        int peakAt = 0;
        for (size_t i = 0; i < c.magnitudeDb.size(); ++i)
            if (c.magnitudeDb[i] > c.magnitudeDb[size_t(peakAt)])
                peakAt = int(i);
        const double t = double(peakAt) / double(c.magnitudeDb.size() - 1);
        const double hz = c.minFreqHz * std::pow(double(c.maxFreqHz) / c.minFreqHz, t);
        check(std::fabs(hz - 1000.0) < 60.0,
              fmt("a 1 kHz peak is measured at %.0f Hz", hz));

        // Well below the peak the response is flat, and 9 dB down from it.
        const double lowT = std::log(100.0 / c.minFreqHz)
                            / std::log(double(c.maxFreqHz) / c.minFreqHz);
        const int lowAt = int(lowT * double(c.magnitudeDb.size() - 1));
        check(std::fabs(double(c.magnitudeDb[size_t(lowAt)]) + 9.0) < 0.6,
              fmt("100 Hz sits %.2f dB below the peak (want -9)",
                  double(c.magnitudeDb[size_t(lowAt)])));
    }

    // A high shelf must be low at the bottom and flat at the top.
    {
        const std::vector<float> ir =
            responseOf(BiquadFilter::Kind::HighShelf, 4000.0, 0.707, 12.0);
        const ImpulseCurve c = impulseMagnitudeResponse(ir.data(), taps, sr, 512);
        check(c.magnitudeDb.front() < -10.0f && c.magnitudeDb.back() > -0.5f,
              fmt("a +12 dB high shelf runs from %.1f dB to %.1f dB",
                  double(c.magnitudeDb.front()), double(c.magnitudeDb.back())));
    }

    // Two identical channels must measure the same as one.
    {
        const std::vector<float> one = responseOf(BiquadFilter::Kind::Peak, 2000.0, 1.0, 6.0);
        std::vector<float> two(one.size() * 2, 0.0f);
        std::copy(one.begin(), one.end(), two.begin());
        std::copy(one.begin(), one.end(), two.begin() + long(one.size()));

        const ImpulseCurve a = impulseMagnitudeResponse(one.data(), taps, sr, 256);
        const ImpulseCurve b = impulseMagnitudeResponsePlanar(two.data(), taps, 2, sr, 256);
        double worst = 0.0;
        for (size_t i = 0; i < a.magnitudeDb.size(); ++i)
            worst = std::max(worst, double(std::fabs(a.magnitudeDb[i] - b.magnitudeDb[i])));
        check(worst < 1e-3,
              fmt("a duplicated channel measures identically (worst %.2e dB)", worst));
    }
}

// -------------------------------------------------------- impulse blob format

void testImpulseBlob()
{
    std::printf("\n[impulse blob]\n");

    check(sizeof(IrBlobHeader) == 64, "blob header is 64 bytes");

    uint32_t seed = 0xB10Bu;
    const uint32_t frames = 1000, channels = 2;
    std::vector<float> samples(size_t(frames) * channels, 0.0f);
    for (auto &v : samples)
        v = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;

    const IrBlobHeader header =
        makeIrBlobHeader(samples.data(), frames, channels, 44100u);

    std::vector<uint8_t> blob(sizeof(IrBlobHeader) + samples.size() * sizeof(float));
    std::memcpy(blob.data(), &header, sizeof header);
    std::memcpy(blob.data() + sizeof(IrBlobHeader), samples.data(),
                samples.size() * sizeof(float));

    // The round trip, which is the whole contract.
    {
        IrBlobHeader got;
        IrBlobError why = IrBlobError::None;
        const float *payload = validateIrBlob(blob.data(), blob.size(), &got, &why);
        check(payload != nullptr && why == IrBlobError::None, "a well-formed blob validates");
        check(payload && std::memcmp(payload, samples.data(),
                                     samples.size() * sizeof(float)) == 0,
              "the samples survive the round trip");
        check(got.sampleRate == 44100u && got.frames == frames && got.channels == channels,
              "the geometry survives the round trip");
    }

    // Every rejection path. A blob arrives from a directory the user can write
    // to, so none of these may be assumed away.
    {
        struct Case { const char *what; IrBlobError expect; std::function<void(std::vector<uint8_t> &)> damage; };
        int bad = 0;

        auto expectRejected = [&](const char *what, IrBlobError expect,
                                  std::vector<uint8_t> b) {
            IrBlobError why = IrBlobError::None;
            const float *p = validateIrBlob(b.data(), b.size(), nullptr, &why);
            const bool ok = (p == nullptr) && (why == expect);
            if (!ok)
                ++bad;
            check(ok, what);
        };

        { auto b = blob; b.resize(32); expectRejected("a truncated blob is rejected", IrBlobError::TooSmall, b); }
        { auto b = blob; b[0] ^= 0xFF; expectRejected("a wrong magic is rejected", IrBlobError::BadMagic, b); }
        { auto b = blob; b[4] = 99; expectRejected("a future version is rejected", IrBlobError::BadVersion, b); }
        { auto b = blob; uint32_t z = 0; std::memcpy(b.data() + 16, &z, 4);
          expectRejected("zero frames is rejected", IrBlobError::BadGeometry, b); }
        { auto b = blob; uint32_t big = 99u; std::memcpy(b.data() + 20, &big, 4);
          expectRejected("too many channels is rejected", IrBlobError::BadGeometry, b); }
        { auto b = blob; b.push_back(0);
          expectRejected("a size mismatch is rejected", IrBlobError::SizeMismatch, b); }
        { auto b = blob; b[sizeof(IrBlobHeader) + 40] ^= 0x01;
          expectRejected("a single flipped payload bit is caught", IrBlobError::HashMismatch, b); }
        (void)bad;
    }

    // The hash has to actually distinguish. A generation counter alone would
    // let a stale file masquerade as a new one.
    {
        std::vector<float> other = samples;
        other[500] += 1e-6f;
        uint8_t h1[16], h2[16];
        irHash128(samples.data(), samples.size() * sizeof(float), h1);
        irHash128(other.data(), other.size() * sizeof(float), h2);
        check(std::memcmp(h1, h2, 16) != 0, "a 1e-6 change in one sample changes the hash");
    }
}

// ---------------------------------------------------------- parameter channel

// Offset of the first word that is a NaN or an infinity, or -1 if there is
// none. Returns the offset rather than a bool so a failure names the field
// instead of leaving the reader to bisect the struct by hand.
//
// Walked as raw memory rather than field by field, deliberately: the claim is
// that *nothing* in the block can be non-finite, including fields added later
// that a hand-written list would silently stop covering.
int firstNonFiniteOffset(const ParamBlock &b)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(&b);
    for (size_t off = 24; off + 4 <= sizeof(ParamBlock); off += 4) {
        // Everything in the convolution block past its two floats is integer:
        // a generation counter, three cross-check fields, flags, and a 16-byte
        // content hash. Reading a hash as an exponent is meaningless.
        if (off >= offsetof(ParamBlock, convolution) + 8)
            continue;
        // The four bool groups are not floats and must be skipped, or their
        // byte patterns get misread as exponents.
        if (off == offsetof(ParamBlock, comp) + 24
            || off == offsetof(ParamBlock, bass) + 12
            || off == offsetof(ParamBlock, multiband) + 8 + 24
            || off == offsetof(ParamBlock, multiband) + 8 + 28 + 24
            || off == offsetof(ParamBlock, multiband) + 8 + 56 + 24
            || off == offsetof(ParamBlock, multiband) + 104) {
            continue;
        }
        uint32_t bits;
        std::memcpy(&bits, p + off, 4);
        if ((bits & 0x7F800000u) == 0x7F800000u)   // NaN or infinity
            return int(off);
    }
    return -1;
}

bool everyFloatFinite(const ParamBlock &b) { return firstNonFiniteOffset(b) < 0; }

void testParamBlock()
{
    std::printf("\n[parameter block]\n");

    check(sizeof(ParamBlock) == 1388, fmt("wire size is %.0f bytes", double(sizeof(ParamBlock))));
    check(sizeof(StatusBlock) == 296, "status block is 296 bytes");

    // The transparent default must not be the dsp defaults. This is the test
    // that makes "just value-initialise it" impossible to land: ParamBlock{}
    // carries reverb.wet = 0.3, which is an always-on reverb on every stream.
    const ParamBlock t = transparentBlock();
    ParamBlock valueInit{};
    check(t.enableMask == 0u, "transparent block enables nothing");

    // Adding an effect means widening kEnKnown as well as adding its bit.
    // Forgetting masks the new bit to zero during sanitising, with no error
    // anywhere -- the effect simply never runs and nothing says why.
    {
        ParamBlock b = transparentBlock();
        b.enableMask = kEnConvolution;
        check(sanitise(b).enableMask == kEnConvolution,
              "the newest enable bit survives sanitising");
    }
    check(isSane(t), "the transparent block is itself a sanitised block");
    check(t.reverb.wet == 0.0f, "transparent block has no reverb");
    check(std::memcmp(&t, &valueInit, sizeof t) != 0,
          "transparent block differs from a value-initialised one");

    // Hostile input. Nothing that comes out may be a NaN, an infinity, or
    // outside the documented range, whatever goes in.
    {
        ParamBlock hostile;
        std::memset(&hostile, 0xFF, sizeof hostile);
        const ParamBlock s = sanitise(hostile);
        const int bad = firstNonFiniteOffset(s);
        check(bad < 0, bad < 0 ? "all-0xFF input yields only finite floats"
                               : fmt("all-0xFF input left offset %.0f non-finite", double(bad)));
        check((s.enableMask & ~kEnKnown) == 0u, "unknown enable bits are dropped");
        check(s.magic == kParamMagic && s.sizeBytes == sizeof(ParamBlock),
              "header fields come from the constants, not the input");
        check(s.comp.makeupDb <= 24.0f && s.comp.makeupDb >= -24.0f,
              "makeup gain is bounded");
        check(s.multiband.highCrossHz >= s.multiband.lowCrossHz * 1.5f,
              "crossover ordering survives hostile input");
    }

    // Every float slot individually poisoned.
    {
        const float poisons[] = { std::nanf(""), INFINITY, -INFINITY, -0.0f, 1e30f, -1e30f };
        int bad = 0;
        for (float poison : poisons) {
            for (size_t off = 24; off + 4 <= sizeof(ParamBlock); off += 4) {
                ParamBlock in = transparentBlock();
                std::memcpy(reinterpret_cast<unsigned char *>(&in) + off, &poison, 4);
                const ParamBlock s = sanitise(in);
                if (!everyFloatFinite(s))
                    ++bad;
            }
        }
        check(bad == 0, fmt("every single-field poisoning is neutralised (%.0f bad)", double(bad)));
    }

    // Random bytes, and idempotence -- which is what makes EffectChain's
    // memcmp change-detection stable rather than re-triggering forever.
    {
        uint32_t seed = 0x1234567u;
        int bad = 0, notIdempotent = 0;
        for (int i = 0; i < 20000; ++i) {
            ParamBlock in;
            auto *p = reinterpret_cast<uint32_t *>(&in);
            for (size_t w = 0; w < sizeof(ParamBlock) / 4; ++w)
                p[w] = xorshift(seed);
            const ParamBlock a = sanitise(in);
            if (!everyFloatFinite(a) || (a.enableMask & ~kEnKnown))
                ++bad;
            const ParamBlock b = sanitise(a);
            if (std::memcmp(&a, &b, sizeof a) != 0)
                ++notIdempotent;
        }
        check(bad == 0, fmt("20000 random blocks all sanitise clean (%.0f bad)", double(bad)));
        check(notIdempotent == 0,
              fmt("sanitise is idempotent (%.0f mismatches)", double(notIdempotent)));
    }
}

void testTransparency()
{
    std::printf("\n[transparency]\n");

    const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    const int channelCounts[] = { 1, 2, 6, 8 };
    const int frames = 4096;

    int bad = 0;
    for (double sr : rates) {
        for (int ch : channelCounts) {
            std::vector<std::vector<float>> data(size_t(ch), std::vector<float>(size_t(frames), 0.0f));
            std::vector<std::vector<float>> original;
            uint32_t seed = 0xC0FFEEu + uint32_t(ch);
            for (auto &v : data)
                for (auto &s : v)
                    s = float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;
            original = data;

            // Second argument is not optional: `ptrs(size_t(ch))` parses as a
            // function declaration, not a variable.
            std::vector<float *> ptrs(size_t(ch), nullptr);
            for (int c = 0; c < ch; ++c)
                ptrs[size_t(c)] = data[size_t(c)].data();
            AudioBuffer buf{ ptrs.data(), ch, frames };

            EffectChain chain;
            chain.prepare(sr, ch, frames);
            chain.apply(transparentBlock());
            chain.process(buf);

            for (int c = 0; c < ch; ++c) {
                if (std::memcmp(data[size_t(c)].data(), original[size_t(c)].data(),
                                size_t(frames) * sizeof(float)) != 0) {
                    ++bad;
                }
            }

            // And again after enabling everything and switching it back off:
            // reaching enableMask == 0 from a non-zero state must be equally
            // bit-exact, or "turn it all off" would not restore the original.
            ParamBlock all = transparentBlock();
            all.enableMask = kEnKnown;
            all.generation = 1;
            chain.apply(all);
            ParamBlock off = transparentBlock();
            off.generation = 2;
            chain.apply(off);
            data = original;
            chain.process(buf);
            for (int c = 0; c < ch; ++c) {
                if (std::memcmp(data[size_t(c)].data(), original[size_t(c)].data(),
                                size_t(frames) * sizeof(float)) != 0) {
                    ++bad;
                }
            }
        }
    }
    check(bad == 0, fmt("bit-exact passthrough at 4 rates x 4 channel counts (%.0f bad)",
                        double(bad)));
}

void testNoAllocation()
{
    std::printf("\n[real-time discipline]\n");

    constexpr int ch = 8;
    constexpr int maxFrames = 1056;
    const double sr = 96000.0;

    EffectChain chain;
    chain.prepare(sr, ch, maxFrames);

    std::vector<std::vector<float>> data(ch, std::vector<float>(maxFrames, 0.1f));
    std::vector<float *> ptrs(ch);
    for (int c = 0; c < ch; ++c)
        ptrs[size_t(c)] = data[size_t(c)].data();

    ParamBlock p = transparentBlock();
    p.enableMask = kEnKnown;
    p.generation = 1;
    p.comp.ratio = 4.0f;
    p.reverb.wet = 0.3f;
    p.tube.mix = 0.5f;
    p.exciter.amount = 0.3f;
    p.bass.amount = 0.4f;
    p.width.width = 1.4f;
    p.multiband.band[0].ratio = 2.0f;
    // A full band set, so the equalizer's design path runs under the counter
    // too -- thirty-two designs on a parameter change is exactly the sort of
    // thing that would be tempting to implement with a std::vector.
    for (int i = 0; i < Equalizer::kMaxBands; ++i) {
        p.eq.band[i] = eqBand(FilterKind(i % int(FilterKind::Count)),
                              30.0 * std::pow(1.22, double(i)),
                              (i % 2) ? 4.0 : -4.0, 1.0 + 0.1 * double(i));
    }
    p.eq.bandCount = uint32_t(Equalizer::kMaxBands);
    p.eq.preampDb = -6.0f;
    p = sanitise(p);

    // One warm-up pass outside the count: the first apply legitimately designs
    // every filter, and the point of the test is the steady state.
    chain.apply(p);
    { AudioBuffer b{ ptrs.data(), ch, maxFrames }; chain.process(b); }

    // Deliberately varying block sizes, including growing ones -- that is what
    // used to make MultibandCompressor reallocate its band scratch mid-stream.
    const int sizes[] = { 128, 256, 512, 1056, 480, 1056, 64 };

    g_allocations.store(0, std::memory_order_relaxed);
    g_countAllocations = true;
    for (int round = 0; round < 4; ++round) {
        for (int n : sizes) {
            p.generation = uint32_t(round * 100 + n);
            p.comp.thresholdDb = -float(n % 20);    // force a genuine re-design
            p.eq.band[n % Equalizer::kMaxBands].gainDb = float(n % 13) - 6.0f;
            chain.apply(sanitise(p));
            AudioBuffer b{ ptrs.data(), ch, n };
            chain.process(b);
        }
    }
    g_countAllocations = false;

    const long n = g_allocations.load(std::memory_order_relaxed);
    check(n == 0, fmt("no allocations on the process path (%.0f seen)", double(n)));
}

void testParamSlots()
{
    std::printf("\n[parameter slots]\n");

    ParamSlots slots;
    slots.clear();

    std::atomic<bool> stop{ false };
    std::atomic<long> torn{ 0 };
    std::atomic<long> accepted{ 0 };
    std::atomic<long> refused{ 0 };

    // Every field is a deterministic function of the generation, so any mixture
    // of two publishes is detectable rather than merely improbable.
    auto fill = [](uint32_t gen) {
        ParamBlock b = transparentBlock();
        b.generation = gen;
        b.enableMask = gen & kEnKnown;
        b.comp.ratio = 1.0f + float(gen % 50);
        b.reverb.wet = float(gen % 100) / 100.0f;
        b.tube.drive = 1.0f + float(gen % 30);
        return sanitise(b);
    };

    std::thread writer([&] {
        for (uint32_t g = 1; g <= 200000u; ++g)
            slots.publish(fill(g));
        stop.store(true);
    });

    std::thread reader([&] {
        ParamBlock got;
        while (!stop.load()) {
            if (!slots.read(&got)) {
                refused.fetch_add(1);
                continue;
            }
            // Generation 0 is the transparent block the slots start out
            // holding; the writer never publishes it, so there is nothing to
            // compare it against.
            if (got.generation == 0u)
                continue;
            accepted.fetch_add(1);
            const ParamBlock expected = fill(got.generation);
            if (std::memcmp(&got, &expected, sizeof got) != 0)
                torn.fetch_add(1);
        }
    });

    writer.join();
    reader.join();

    check(torn.load() == 0,
          fmt("no torn block in %.0f reads (%.0f torn)",
              double(accepted.load()), double(torn.load())));
    check(accepted.load() > 0, "the reader actually saw blocks");
}

void testDenormals()
{
    std::printf("\n[denormals]\n");

    const unsigned before = DenormalGuard::rawMode();
    {
        const DenormalGuard guard;
        volatile float tiny = 1e-40f;
        const float product = tiny * 1.0f;
        check(product == 0.0f, "denormals are flushed inside the guard");
    }
    // The half a naive implementation forgets. audiodg shares this thread with
    // other vendors' processing objects; leaving their arithmetic altered would
    // be a genuinely nasty thing to do.
    check(DenormalGuard::rawMode() == before, "the previous mode is restored");
}

int runTests()
{
    std::printf("DreamDSP dsp harness\n");
    testStaticCurve();
    testSoftKnee();
    testTiming();
    testAutoMakeup();
    testStereoLink();
    testSilenceAndSanity();
    testReverb();
    testCrossover();
    testTube();
    testDryWetAlignment();
    testVirtualBass();
    testExciter();
    testStereo();
    testMultiband();
    testFft();
    testResampler();
    testConvolver();
    testEqualizer();
    testGraphicEq();
    testRouting();
    testLoudness();
    testLimiter();
    testDynamicBass();
    testLoudnessMeter();
    testTransient();
    testClipper();
    testNightMode();
    testAutoGain();
    testTruePeak();
    testMonoCompatibility();
    testChainOrder();
    testMasterBypass();
    testImpulseAnalysis();
    testImpulseBlob();
    testParamBlock();
    testTransparency();
    testNoAllocation();
    testParamSlots();
    testDenormals();
    std::printf("\n%s -- %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures;
}

int processFile(const char *in, const char *out)
{
    WavData wav;
    std::string err;
    if (!readWav(in, &wav, &err)) {
        std::printf("read %s: %s\n", in, err.c_str());
        return 1;
    }
    std::printf("in : %d ch, %d Hz, %d frames\n", wav.channelCount(), wav.sampleRate, wav.frames());

    std::vector<float *> ch;
    for (auto &v : wav.channels)
        ch.push_back(v.data());
    AudioBuffer buf{ ch.data(), wav.channelCount(), wav.frames() };

    // Same order as the application: compressing a reverb tail pumps it.
    Compressor c;
    c.prepare(wav.sampleRate, wav.channelCount());
    Compressor::Params p;
    p.thresholdDb = -18.0f;
    p.ratio = 4.0f;
    p.kneeDb = 6.0f;
    p.attackMs = 5.0f;
    p.releaseMs = 80.0f;
    p.autoMakeup = true;
    c.setParams(p);
    c.process(buf);

    Reverb r;
    r.prepare(wav.sampleRate);
    Reverb::Params rp;
    rp.roomSize = 0.6f;
    rp.damping = 0.5f;
    rp.preDelayMs = 20.0f;
    rp.wet = 0.25f;
    rp.dry = 1.0f;
    r.setParams(rp);
    r.reset();
    r.process(buf);

    if (!writeWav(out, wav, &err)) {
        std::printf("write %s: %s\n", out, err.c_str());
        return 1;
    }
    std::printf("out: %s\n", out);
    return 0;
}

} // namespace

// ---------------------------------------------------------------- benchmark

// What each stage costs, as a percentage of one core at real time.
//
// Not a test -- it asserts nothing and is not run by --test. It exists because
// "can we afford this?" is the first question about any new effect, and the
// only honest answer is measured on the machine in question. The number that
// matters is not milliseconds but the fraction of the audio callback's budget:
// at 48 kHz a 480-frame buffer must be finished in 10 ms, and everything else
// on the machine is entitled to the rest.
int runBench()
{
    std::printf("dspharness -- per-stage cost, %% of one core at real time\n");

    struct Case { const char *name; double rate; int channels; int frames; };
    const Case cases[] = {
        { "48 kHz stereo",   48000.0, 2, 480 },
        { "96 kHz stereo",   96000.0, 2, 960 },
        { "96 kHz 7.1",      96000.0, 8, 960 },
    };

    struct Stage { uint32_t bit; const char *name; };
    const Stage stages[] = {
        { kEnEqualizer,   "equalizer (32 bands)" },
        { kEnGraphic,     "graphic eq (31 bands)" },
        { kEnLoudness,    "loudness" },
        { kEnDynBass,     "dynamic bass" },
        { kEnBass,        "virtual bass" },
        { kEnExciter,     "exciter" },
        { kEnTube,        "tube" },
        { kEnComp,        "compressor" },
        { kEnMultiband,   "multiband" },
        { kEnReverb,      "reverb" },
        { kEnWidth,       "width" },
        { kEnCrossfeed,   "crossfeed" },
        { kEnMatrix,      "matrix" },
        { kEnDelay,       "delay" },
        { kEnLimiter,     "limiter (true peak)" },
        { kEnTransient,   "transient shaper" },
        { kEnClipper,     "clipper" },
        { kEnAutoGain,    "auto volume" },
        { kEnNightMode,   "night mode" },
    };

    for (const Case &c : cases) {
        std::printf("\n[%s]  buffer %d frames (%.2f ms)\n",
                    c.name, c.frames, 1000.0 * c.frames / c.rate);

        std::vector<std::vector<float>> data(size_t(c.channels),
                                             std::vector<float>(size_t(c.frames), 0.0f));
        std::vector<float *> ptrs(size_t(c.channels), nullptr);
        for (int ch = 0; ch < c.channels; ++ch)
            ptrs[size_t(ch)] = data[size_t(ch)].data();

        // Real material, not silence: the dynamics stages branch on level and
        // an all-zero buffer measures the cheap path.
        uint32_t seed = 0x5EED1234u;
        const auto refill = [&] {
            for (auto &v : data)
                for (auto &x : v)
                    x = 0.25f * float(int(xorshift(seed) & 0xFFFFu) - 32768) / 32768.0f;
        };

        // Every stage set to something that does work rather than to its
        // neutral value -- a compressor above its threshold, an equalizer with
        // gain in every band.
        ParamBlock base = transparentBlock();
        base.eq.bandCount = uint32_t(Equalizer::kMaxBands);
        for (int i = 0; i < Equalizer::kMaxBands; ++i) {
            base.eq.band[i] = eqBand(FilterKind::PK, 30.0 * std::pow(1.22, double(i)),
                                     (i % 2) ? 3.0 : -3.0, 1.4);
        }
        for (int i = 0; i < GraphicEq::kBands; ++i)
            base.graphic.gainDb[i] = (i % 2) ? 3.0f : -3.0f;
        base.graphic.amount = 1.0f;
        base.comp.thresholdDb = -30.0f;
        base.comp.ratio = 4.0f;
        base.multiband.band[0].ratio = 3.0f;
        base.multiband.band[1].ratio = 3.0f;
        base.multiband.band[2].ratio = 3.0f;
        base.reverb.wet = 0.3f;
        base.tube.mix = 0.6f;
        base.tube.drive = 6.0f;
        base.exciter.amount = 0.5f;
        base.bass.amount = 0.6f;
        base.dynBass.maxGainDb = 9.0f;
        base.width.width = 1.5f;
        base.loudness.referenceDb = 0.0f;
        base.loudness.volumeDb = -20.0f;
        base.loudness.amount = 1.0f;
        base.limiter.thresholdDb = -3.0f;
        base.limiter.gainDb = 12.0f;
        base.transient.attack = 0.6f;
        base.transient.sustain = -0.3f;
        base.clipper.driveDb = 6.0f;
        base.clipper.ceilingDb = -1.0f;
        base.autoGain.targetLufs = -18.0f;
        base.nightMode.profile = DynamicRange::kProfileFilmStandard;
        base.delay.ms[0] = 2.0f;
        base.matrix.gain[0][1] = 0.2f;

        const double budgetNs = 1e9 * double(c.frames) / c.rate;

        const auto measure = [&](uint32_t mask) {
            EffectChain chain;
            chain.prepare(c.rate, c.channels, c.frames);
            ParamBlock p = base;
            p.enableMask = mask;
            p.generation = 1;
            chain.apply(sanitise(p));

            AudioBuffer buf{ ptrs.data(), c.channels, c.frames };
            // Warm up: first call designs filters and touches every page.
            for (int i = 0; i < 16; ++i) { refill(); chain.process(buf); }

            constexpr int kRuns = 400;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < kRuns; ++i) {
                refill();
                chain.process(buf);
            }
            const auto t1 = std::chrono::steady_clock::now();

            // The refill is inside the loop because it also dirties the cache
            // the way a real stream does; measure it alone and subtract.
            const auto t2 = std::chrono::steady_clock::now();
            for (int i = 0; i < kRuns; ++i)
                refill();
            const auto t3 = std::chrono::steady_clock::now();

            const double total = double(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
            const double fill = double(std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count());
            return std::max(0.0, (total - fill) / double(kRuns));
        };

        const double idle = measure(0);
        double sum = 0.0;
        for (const Stage &st : stages) {
            const double ns = measure(st.bit) - idle;
            sum += ns;
            std::printf("  %-24s %8.1f us   %5.2f %% of one core\n",
                        st.name, ns / 1000.0, 100.0 * ns / budgetNs);
        }

        const double all = measure(kEnKnown & ~kEnConvolution) - idle;
        std::printf("  %-24s %8.1f us   %5.2f %% of one core   (sum of parts %.2f %%)\n",
                    "ALL (no convolution)", all / 1000.0, 100.0 * all / budgetNs,
                    100.0 * sum / budgetNs);
    }

    std::printf("\nConvolution is excluded: its cost is set by the impulse response\n"
                "length, not by the stage, and it is measured in [convolution].\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "--test") == 0)
        return runTests();
    if (argc >= 2 && std::strcmp(argv[1], "--bench") == 0)
        return runBench();
    if (argc >= 3)
        return processFile(argv[1], argv[2]);

    std::printf("usage: dspharness --test\n"
                "       dspharness --bench\n"
                "       dspharness <in.wav> <out.wav>\n");
    return runTests();
}
