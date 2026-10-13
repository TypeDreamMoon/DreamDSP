#include "FdnReverb.h"
#include <algorithm>

namespace dreamdsp::dsp {
namespace {
float safeParameter(float value, float lo, float hi, float fallback) noexcept
{
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    if ((bits & 0x7F800000u) == 0x7F800000u) return fallback;
    return clampTo(value, lo, hi);
}
double safeRate(double rate) noexcept
{
    uint64_t bits;
    std::memcpy(&bits, &rate, sizeof bits);
    if ((bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull
        || rate < 8000.0 || rate > 384000.0) return 48000.0;
    return rate;
}
} // namespace
void FdnReverb::prepare(double rate, int)
{
    m_rate = safeRate(rate);
    constexpr int lengths[] = {1493, 1601, 1867, 2053, 2251, 2399, 2749, 3011};
    for (int j = 0; j < 8; ++j) {
        m_lines[j].assign(size_t(std::max(1, int(lengths[j] * m_rate / 48000.0))), 0.0f);
        m_gain[j].prepare(m_rate, 50.0f);
    }
    m_pre.assign(size_t(m_rate * 0.2) + 3, 0.0f);
    for (auto *s : {&m_damp, &m_preTime, &m_mix, &m_width, &m_output}) s->prepare(m_rate);
    reset();
}
void FdnReverb::setParams(const Params &p) noexcept
{
    const Params fallback;
    m_p.decaySeconds = safeParameter(p.decaySeconds, 0.1f, 12.0f, fallback.decaySeconds);
    m_p.dampingHz = safeParameter(p.dampingHz, 200.0f, float(m_rate * 0.45),
                                std::min(fallback.dampingHz, float(m_rate * 0.45)));
    m_p.preDelayMs = safeParameter(p.preDelayMs, 0.0f, 200.0f, fallback.preDelayMs);
    m_p.mix = safeParameter(p.mix, 0.0f, 1.0f, 0.0f);
    m_p.width = safeParameter(p.width, 0.0f, 1.0f, fallback.width);
    m_p.outputDb = safeParameter(p.outputDb, -24.0f, 6.0f, 0.0f);
    const float decay = m_p.decaySeconds;
    for (int j = 0; j < 8; ++j)
        m_gain[j].setTarget(float(std::pow(10.0, -3.0 * m_lines[j].size() / (m_rate * decay))));
    m_damp.setTarget(float(std::exp(-6.283185307179586 * m_p.dampingHz / m_rate)));
    m_preTime.setTarget(m_p.preDelayMs * float(m_rate * 0.001));
    m_mix.setTarget(m_p.mix);
    m_width.setTarget(m_p.width);
    m_output.setTarget(dbToLin(m_p.outputDb));
}
void FdnReverb::reset() noexcept
{
    for (auto &line : m_lines) std::fill(line.begin(), line.end(), 0.0f);
    std::fill(m_pre.begin(), m_pre.end(), 0.0f);
    std::memset(m_pos, 0, sizeof m_pos);
    std::memset(m_low, 0, sizeof m_low);
    m_prePos = 0;
    const float decay = clampTo(m_p.decaySeconds, 0.1f, 12.0f);
    for (int j = 0; j < 8; ++j)
        m_gain[j].reset(float(std::pow(10.0, -3.0 * m_lines[j].size() / (m_rate * decay))));
    m_damp.reset(float(std::exp(-6.283185307179586 * clampTo(m_p.dampingHz, 200.0f, float(m_rate * 0.45)) / m_rate)));
    m_preTime.reset(clampTo(m_p.preDelayMs, 0.0f, 200.0f) * float(m_rate * 0.001));
    m_mix.reset(clampTo(m_p.mix, 0.0f, 1.0f));
    m_width.reset(clampTo(m_p.width, 0.0f, 1.0f));
    m_output.reset(dbToLin(clampTo(m_p.outputDb, -24.0f, 6.0f)));
}
void FdnReverb::process(const AudioBuffer &buf) noexcept
{
    if (!buf.valid() || !buf.channels[0] || m_pre.empty()) return;
    const bool stereo = buf.channelCount > 1 && buf.channels[1];
    constexpr float inject[8] = {1, -1, 1, -1, -1, 1, 1, -1};
    constexpr float left[8] = {1, 1, -1, -1, 1, 1, -1, -1};
    constexpr float right[8] = {1, -1, -1, 1, -1, 1, 1, -1};
    for (int i = 0; i < buf.frames; ++i) {
        bool bad = false;
        const float l = railed(buf.channels[0][i], &bad);
        const float r = stereo ? railed(buf.channels[1][i], &bad) : l;
        m_pre[size_t(m_prePos)] = 0.5f * (l + r);
        float p = float(m_prePos) - m_preTime.next();
        if (p < 0.0f) p += float(m_pre.size());
        const int a = int(p), b = (a + 1) % int(m_pre.size());
        const float f = p - float(a);
        const float input = m_pre[size_t(a)] * (1.0f - f) + m_pre[size_t(b)] * f;
        m_prePos = (m_prePos + 1) % int(m_pre.size());
        const float damp = m_damp.next(), mix = m_mix.next();
        const float width = m_width.next(), output = m_output.next();
        float v[8], sum = 0.0f, wetL = 0.0f, wetR = 0.0f;
        for (int j = 0; j < 8; ++j) {
            const float read = m_lines[j][size_t(m_pos[j])];
            m_low[j] = (1.0f - damp) * read + damp * m_low[j];
            v[j] = m_low[j] * m_gain[j].next();
            sum += v[j];
            wetL += left[j] * read * 0.125f;
            wetR += right[j] * read * 0.125f;
        }
        for (int j = 0; j < 8; ++j) {
            m_lines[j][size_t(m_pos[j])] = railed(0.353553391f * inject[j] * input + v[j] - 0.25f * sum, &bad);
            m_pos[j] = (m_pos[j] + 1) % int(m_lines[j].size());
            if (bad) m_low[j] = 0.0f;
        }
        const float mid = 0.5f * (wetL + wetR), side = 0.5f * width * (wetL - wetR);
        buf.channels[0][i] = mix == 0.0f && output == 1.0f ? l
            : railed(output * (l + mix * (mid + side - l)), &bad);
        if (stereo) buf.channels[1][i] = mix == 0.0f && output == 1.0f ? r
            : railed(output * (r + mix * (mid - side - r)), &bad);
    }
}
} // namespace dreamdsp::dsp
