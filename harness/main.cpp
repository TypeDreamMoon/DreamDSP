// Offline exerciser for the DSP layer.
//
//   dspharness --test              run the assertions, exit code = failures
//   dspharness in.wav out.wav      compress a file with the default settings
//
// The point of this binary is that DSP code gets proven here, on synthetic
// signals with closed-form expected answers, long before it is loaded into a
// live audio graph where a mistake costs the machine its sound.

#include "Compressor.h"
#include "WavFile.h"

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

int runTests()
{
    std::printf("DreamDSP dsp harness\n");
    testStaticCurve();
    testSoftKnee();
    testTiming();
    testAutoMakeup();
    testStereoLink();
    testSilenceAndSanity();
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

    std::vector<float *> ch;
    for (auto &v : wav.channels)
        ch.push_back(v.data());
    AudioBuffer buf{ ch.data(), wav.channelCount(), wav.frames() };
    c.process(buf);

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
