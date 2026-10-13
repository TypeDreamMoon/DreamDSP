#include "Binaural.h"
#include "hrtf/KemarDiffuseData.h"

#include <algorithm>
#include <cmath>

#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace dreamdsp::dsp {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRadius = 0.0875;
constexpr double kSpeedOfSound = 343.0;
constexpr int kSincHalf = 16;
constexpr int kResampleHalf = 32;

float finiteClamp(float value, float fallback, float lo, float hi) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    if ((bits & 0x7f800000u) == 0x7f800000u)
        return fallback;
    return clampTo(value, lo, hi);
}

float wrapDegrees(float degrees) noexcept
{
    if (degrees >= 180.0f) degrees -= 360.0f;
    if (degrees < -180.0f) degrees += 360.0f;
    return degrees;
}

float interpolatedDot(const float *a, const float *b, const float *history,
                      float blend, int taps) noexcept
{
#if defined(_M_X64) || defined(__SSE2__)
    // SSE2 is part of the x64 baseline; no AVX-capable CPU is required.
    // Two accumulators shorten the dependency chain. prepare rounds the tap
    // count to 32, and unaligned loads also support a moving history window.
    const __m128 t = _mm_set1_ps(blend);
    __m128 sum0 = _mm_setzero_ps(), sum1 = _mm_setzero_ps();
    for (int k = 0; k < taps; k += 8) {
        const __m128 a0 = _mm_loadu_ps(a + k), a1 = _mm_loadu_ps(a + k + 4);
        const __m128 h0 = _mm_add_ps(a0, _mm_mul_ps(t, _mm_sub_ps(_mm_loadu_ps(b + k), a0)));
        const __m128 h1 = _mm_add_ps(a1, _mm_mul_ps(t, _mm_sub_ps(_mm_loadu_ps(b + k + 4), a1)));
        sum0 = _mm_add_ps(sum0, _mm_mul_ps(h0, _mm_loadu_ps(history + k)));
        sum1 = _mm_add_ps(sum1, _mm_mul_ps(h1, _mm_loadu_ps(history + k + 4)));
    }
    __m128 sum = _mm_add_ps(sum0, sum1);
    sum = _mm_add_ps(sum, _mm_movehl_ps(sum, sum));
    sum = _mm_add_ss(sum, _mm_shuffle_ps(sum, sum, 1));
    return _mm_cvtss_f32(sum);
#else
    float sum = 0.0f;
    for (int k = 0; k < taps; ++k)
        sum += (a[k] + blend * (b[k] - a[k])) * history[k];
    return sum;
#endif
}

float bilinearDot(const float *a, const float *b, const float *c, const float *d,
                  const float *history, float lowAz, float highAz, float elevation,
                  int taps) noexcept
{
#if defined(_M_X64) || defined(__SSE2__)
    const __m128 x = _mm_set1_ps(lowAz), y = _mm_set1_ps(highAz);
    const __m128 z = _mm_set1_ps(elevation);
    __m128 sum0 = _mm_setzero_ps(), sum1 = _mm_setzero_ps();
    for (int k = 0; k < taps; k += 8) {
        const __m128 a0 = _mm_loadu_ps(a + k), a1 = _mm_loadu_ps(a + k + 4);
        const __m128 c0 = _mm_loadu_ps(c + k), c1 = _mm_loadu_ps(c + k + 4);
        const __m128 low0 = _mm_add_ps(a0, _mm_mul_ps(x, _mm_sub_ps(_mm_loadu_ps(b + k), a0)));
        const __m128 low1 = _mm_add_ps(a1, _mm_mul_ps(x, _mm_sub_ps(_mm_loadu_ps(b + k + 4), a1)));
        const __m128 high0 = _mm_add_ps(c0, _mm_mul_ps(y, _mm_sub_ps(_mm_loadu_ps(d + k), c0)));
        const __m128 high1 = _mm_add_ps(c1, _mm_mul_ps(y, _mm_sub_ps(_mm_loadu_ps(d + k + 4), c1)));
        const __m128 h0 = _mm_add_ps(low0, _mm_mul_ps(z, _mm_sub_ps(high0, low0)));
        const __m128 h1 = _mm_add_ps(low1, _mm_mul_ps(z, _mm_sub_ps(high1, low1)));
        sum0 = _mm_add_ps(sum0, _mm_mul_ps(h0, _mm_loadu_ps(history + k)));
        sum1 = _mm_add_ps(sum1, _mm_mul_ps(h1, _mm_loadu_ps(history + k + 4)));
    }
    __m128 sum = _mm_add_ps(sum0, sum1);
    sum = _mm_add_ps(sum, _mm_movehl_ps(sum, sum));
    sum = _mm_add_ss(sum, _mm_shuffle_ps(sum, sum, 1));
    return _mm_cvtss_f32(sum);
#else
    float sum = 0.0f;
    for (int k = 0; k < taps; ++k) {
        const float low = a[k] + lowAz * (b[k] - a[k]);
        const float high = c[k] + highAz * (d[k] - c[k]);
        sum += (low + elevation * (high - low)) * history[k];
    }
    return sum;
#endif
}
} // namespace

void Binaural::prepare(double sampleRate, int channels)
{
    m_supported = false;
    m_taps = m_latency = m_analyticTaps = m_measuredTaps = m_measuredLatency = 0;
    uint64_t rateBits;
    std::memcpy(&rateBits, &sampleRate, sizeof rateBits);
    if ((rateBits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull
        || channels != 2 || !(sampleRate >= 8000.0 && sampleRate <= 192000.0))
        return;

    // Independent implementation of Brown & Duda (1998), head-shadow section
    // only: https://users.umiacs.umd.edu/~ramani/cmsc828d_audio/BrownDuda.pdf
    // H(s) = (alpha*s + 2*c/a)/(s + 2*c/a). The time constant is a/(2*c),
    // NOT 2*a/c (a typo in some subsequent descriptions of this model).
    const double omega0 = kSpeedOfSound / kRadius;
    const double a1 = (omega0 - sampleRate) / (omega0 + sampleRate);
    const int decayLength = std::abs(a1) > 1e-12
        ? int(std::ceil(std::log(1e-8) / std::log(std::abs(a1)))) + 2 : 2;
    m_latency = int(std::ceil(kRadius / kSpeedOfSound * sampleRate)) + kSincHalf;
    const int maxDelay = int(std::ceil(m_latency + kRadius / kSpeedOfSound
                                                  * sampleRate * kPi * 0.5));
    m_taps = ((maxDelay + kSincHalf + decayLength + 31) / 32) * 32;
    if (m_taps > kMaxTaps) {
        m_taps = m_latency = 0;
        return; // Never silently truncate an unsupported filter.
    }
    m_filters.assign(size_t(kDirections * 2 * m_taps), 0.0f);
    std::array<double, kMaxTaps> shadow{};
    std::array<double, kMaxTaps> delay{};
    for (int direction = 0; direction < kDirections; ++direction) {
        const double azimuth = double(direction - 180) * kPi / 180.0;
        for (int ear = 0; ear < 2; ++ear) {
            const double side = ear == 0 ? 1.0 : -1.0;
            const double theta = std::acos(clampTo(side * std::sin(azimuth), -1.0, 1.0));
            const double alpha = 1.05 + 0.95 * std::cos(theta * kPi / (5.0 * kPi / 6.0));
            const double b0 = (omega0 + alpha * sampleRate) / (omega0 + sampleRate);
            const double b1 = (omega0 - alpha * sampleRate) / (omega0 + sampleRate);
            shadow.fill(0.0);
            shadow[0] = b0;
            shadow[1] = b1 - a1 * b0;
            for (int k = 2; k < m_taps; ++k)
                shadow[size_t(k)] = -a1 * shadow[size_t(k - 1)];

            // Woodworth path delay relative to head centre, made causal with
            // one COMMON offset. Keeping independent ear delays preserves ITD.
            const double tau = kRadius / kSpeedOfSound *
                (theta < kPi * 0.5 ? -std::cos(theta) : theta - kPi * 0.5);
            const double samples = double(m_latency) + tau * sampleRate;
            delay.fill(0.0);
            double sum = 0.0;
            for (int k = 0; k < m_taps; ++k) {
                const double offset = double(k) - samples;
                if (std::abs(offset) >= double(kSincHalf)) continue;
                const double sinc = std::abs(offset) < 1e-12 ? 1.0
                    : std::sin(kPi * offset) / (kPi * offset);
                const double window = 0.5 + 0.5 * std::cos(kPi * offset / kSincHalf);
                delay[size_t(k)] = sinc * window;
                sum += delay[size_t(k)];
            }
            float *filter = m_filters.data() + (direction * 2 + ear) * m_taps;
            for (int k = 0; k < m_taps; ++k) {
                double value = 0.0;
                for (int j = 0; j <= k; ++j)
                    value += delay[size_t(j)] * shadow[size_t(k - j)];
                // Reverse taps for a forward, contiguous history dot product.
                filter[m_taps - 1 - k] = float(value / sum);
            }
        }
    }
    m_analyticTaps = m_taps;
    prepareMeasured(sampleRate);
    m_taps = std::max(m_analyticTaps, m_measuredTaps);
    if (m_taps > kMaxTaps) {
        m_taps = m_analyticTaps = m_measuredTaps = m_latency = m_measuredLatency = 0;
        return;
    }
    m_directionRate = 1.0f - timeConstant(30.0f, sampleRate);
    m_levelRate = 1.0f - timeConstant(10.0f, sampleRate);
    m_modelStep = float(1.0 / (sampleRate * 0.02));
    m_supported = true;
    setParams(m_p);
    reset();
}

void Binaural::prepareMeasured(double sampleRate)
{
    // Resample the impulse response, not the audio stream. Keeping 1/ratio
    // preserves filter gain, while common causal padding retains native ITD.
    const double ratio = sampleRate / kemar_diffuse::kSampleRate;
    const double cutoff = std::min(1.0, ratio);
    const double radius = kResampleHalf / cutoff;
    m_measuredLatency = int(std::ceil(radius * ratio));
    const int last = int(std::ceil(m_measuredLatency + (kemar_diffuse::kTaps - 1 + radius) * ratio));
    m_measuredTaps = ((last + 1 + 31) / 32) * 32;
    if (m_measuredTaps > kMaxTaps) return;
    m_measured.assign(size_t(kemar_diffuse::kMeasurements * 2 * m_measuredTaps), 0.0f);
    if (sampleRate == kemar_diffuse::kSampleRate) {
        for (int measurement = 0; measurement < kemar_diffuse::kMeasurements; ++measurement)
            for (int ear = 0; ear < 2; ++ear) {
                float *out = m_measured.data() + (measurement * 2 + ear) * m_measuredTaps;
                for (int n = 0; n < kemar_diffuse::kTaps; ++n)
                    out[m_measuredTaps - 1 - m_measuredLatency - n] =
                        kemar_diffuse::kHrir[measurement][ear][n] / 32768.0f;
            }
        return;
    }
    // This matrix is built once per preparation; sin/cos never run per HRIR
    // or in process(). Padding includes the entire finite resampling support.
    std::vector<double> matrix(size_t(m_measuredTaps * kemar_diffuse::kTaps), 0.0);
    for (int k = 0; k < m_measuredTaps; ++k) {
        const double sourceTime = (k - m_measuredLatency) / ratio;
        for (int n = 0; n < kemar_diffuse::kTaps; ++n) {
            const double x = sourceTime - n;
            if (std::abs(x) >= radius) continue;
            const double argument = cutoff * x;
            const double sinc = std::abs(argument) < 1e-12 ? 1.0
                : std::sin(kPi * argument) / (kPi * argument);
            const double window = 0.5 + 0.5 * std::cos(kPi * x / radius);
            matrix[size_t(k * kemar_diffuse::kTaps + n)] = cutoff / ratio * sinc * window / 32768.0;
        }
    }
    for (int measurement = 0; measurement < kemar_diffuse::kMeasurements; ++measurement)
        for (int ear = 0; ear < 2; ++ear) {
            float *out = m_measured.data() + (measurement * 2 + ear) * m_measuredTaps;
            for (int k = 0; k < m_measuredTaps; ++k) {
                double value = 0.0;
                const double *row = matrix.data() + k * kemar_diffuse::kTaps;
                for (int n = 0; n < kemar_diffuse::kTaps; ++n)
                    value += row[n] * kemar_diffuse::kHrir[measurement][ear][n];
                out[m_measuredTaps - 1 - k] = float(value);
            }
        }
}

const float *Binaural::measuredFilter(int ring, int index, int ear) const noexcept
{
    const auto &r = kemar_diffuse::kRings[ring];
    if (index >= r.halfCount) { index = r.count - index; ear = 1 - ear; }
    return m_measured.data() + ((r.first + index) * 2 + ear) * m_measuredTaps;
}

void Binaural::setParams(const Params &p) noexcept
{
    m_p.speakerAngleDeg = finiteClamp(p.speakerAngleDeg, 30.0f, 0.0f, 90.0f);
    m_p.yawDeg = finiteClamp(p.yawDeg, 0.0f, -180.0f, 180.0f);
    m_p.mix = finiteClamp(p.mix, 1.0f, 0.0f, 1.0f);
    m_p.outputDb = finiteClamp(p.outputDb, -6.0f, -24.0f, 6.0f);
    m_p.elevationDeg = finiteClamp(p.elevationDeg, 0.0f, -40.0f, 90.0f);
    m_p.model = p.model == 0 ? 0u : 1u;
    m_targetGain = dbToLin(m_p.outputDb);
    if (!m_hasProcessed) {
        m_angle = m_p.speakerAngleDeg;
        m_yaw = m_p.yawDeg;
        m_elevation = m_p.elevationDeg;
        m_modelBlend = float(m_p.model);
        m_mix = m_p.mix;
        m_gain = m_targetGain;
    }
}

void Binaural::reset() noexcept
{
    for (auto &history : m_history) history.fill(0.0f);
    m_write = 0;
    m_hasProcessed = false;
    m_angle = m_p.speakerAngleDeg;
    m_yaw = m_p.yawDeg;
    m_elevation = m_p.elevationDeg;
    m_modelBlend = float(m_p.model);
    m_mix = m_p.mix;
    m_gain = m_targetGain;
}

void Binaural::process(const AudioBuffer &buf) noexcept
{
    if (!m_supported || !buf.valid() || buf.channelCount != 2
        || !buf.channels[0] || !buf.channels[1]) return;
    m_hasProcessed = true;
    for (int i = 0; i < buf.frames; ++i) {
        m_angle += m_directionRate * (m_p.speakerAngleDeg - m_angle);
        m_yaw = wrapDegrees(m_yaw + m_directionRate * wrapDegrees(m_p.yawDeg - m_yaw));
        m_elevation += m_directionRate * (m_p.elevationDeg - m_elevation);
        m_modelBlend += clampTo(float(m_p.model) - m_modelBlend, -m_modelStep, m_modelStep);
        m_mix += m_levelRate * (m_p.mix - m_mix);
        m_gain += m_levelRate * (m_targetGain - m_gain);
        for (int source = 0; source < 2; ++source) {
            bool bad = false;
            const float x = railed(buf.channels[source][i], &bad);
            m_history[size_t(source)][size_t(m_write)] = x;
            m_history[size_t(source)][size_t(m_write + m_taps)] = x;
        }

        float analytic[2] = {}, measured[2] = {};
        const float elevationPosition = clampTo((m_elevation + 40.0f) * 0.1f, 0.0f, 13.0f);
        const int lowRing = int(elevationPosition), highRing = std::min(lowRing + 1, 13);
        const float elevationBlend = elevationPosition - float(lowRing);
        for (int source = 0; source < 2; ++source) {
            const float azimuth = wrapDegrees((source == 0 ? m_angle : -m_angle) - m_yaw);
            const float *endHistory = m_history[size_t(source)].data() + m_write + 1 + m_taps;
            if (m_modelBlend < 1.0f) {
                const float position = azimuth + 180.0f;
                const int first = clampTo(int(position), 0, kDirections - 1);
                const int second = (first + 1) % kDirections;
                const float blend = position - float(first);
                for (int ear = 0; ear < 2; ++ear) {
                    const float *a = m_filters.data() + (first * 2 + ear) * m_analyticTaps;
                    const float *b = m_filters.data() + (second * 2 + ear) * m_analyticTaps;
                    analytic[ear] += interpolatedDot(a, b, endHistory - m_analyticTaps, blend, m_analyticTaps);
                }
            }
            if (m_modelBlend > 0.0f) {
                // MIT uses clockwise/right-positive azimuth; the rack's DSP
                // geometry is left-positive. Convert once, then use true
                // 360/count steps rather than rounded WAV filename angles.
                const double clockwise = azimuth > 0.0f ? 360.0 - double(azimuth) : -double(azimuth);
                const int loCount = kemar_diffuse::kRings[lowRing].count;
                const int hiCount = kemar_diffuse::kRings[highRing].count;
                const double low = clockwise * (double(loCount) / 360.0);
                const double high = clockwise * (double(hiCount) / 360.0);
                const int la = std::min(int(low), loCount - 1), lb = (la + 1) % loCount;
                const int ha = std::min(int(high), hiCount - 1), hb = (ha + 1) % hiCount;
                for (int ear = 0; ear < 2; ++ear)
                    measured[ear] += bilinearDot(measuredFilter(lowRing, la, ear),
                        measuredFilter(lowRing, lb, ear), measuredFilter(highRing, ha, ear),
                        measuredFilter(highRing, hb, ear), endHistory - m_measuredTaps,
                        float(low - la), float(high - ha), elevationBlend, m_measuredTaps);
            }
        }
        for (int ear = 0; ear < 2; ++ear) {
            // Spatial-response delay is part of the effect, not an added
            // block latency. Keep zero mix transparent; partial mixes have
            // the expected interference between direct and virtual sound.
            const float dry = m_history[size_t(ear)][size_t(m_write)];
            const float wet = analytic[ear] + m_modelBlend * (measured[ear] - analytic[ear]);
            bool bad = false;
            buf.channels[ear][i] = m_mix == 0.0f && m_gain == 1.0f ? dry
                : railed(m_gain * (dry + m_mix * (wet - dry)), &bad);
        }
        m_write = (m_write + 1) % m_taps;
    }
}

} // namespace dreamdsp::dsp
