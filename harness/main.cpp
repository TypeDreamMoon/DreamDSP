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
#include "MultibandCompressor.h"
#include "Reverb.h"
#include "Saturation.h"
#include "Stereo.h"
#include "WavFile.h"

#include <algorithm>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
    check(after2 > before2 * 20.0 + 1e-4,
          "harmonics of the missing fundamental are synthesised");
    check(after3 > 1e-4, "the series extends past the second harmonic");
    check(fund > 1e-3, "the original low tone is still there when not removed");

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
    mb.prepare(sr, 2);
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
    testVirtualBass();
    testExciter();
    testStereo();
    testMultiband();
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

int main(int argc, char **argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "--test") == 0)
        return runTests();
    if (argc >= 3)
        return processFile(argv[1], argv[2]);

    std::printf("usage: dspharness --test\n"
                "       dspharness <in.wav> <out.wav>\n");
    return runTests();
}
