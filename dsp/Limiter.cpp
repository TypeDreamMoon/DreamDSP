#include "Limiter.h"

#include <cmath>
#include <cstring>

namespace dreamdsp::dsp {

void Limiter::SlidingMin::prepare(int capacity)
{
    if (capacity < 1)
        capacity = 1;
    value.assign(size_t(capacity), 0.0f);
    index.assign(size_t(capacity), 0);
    reset();
}

void Limiter::SlidingMin::reset() noexcept
{
    head = tail = 0;
    n = 0;
}

float Limiter::SlidingMin::push(float v, int window) noexcept
{
    const int cap = int(value.size());
    if (cap <= 0 || window <= 0)
        return v;

    // Anything already in the deque that is not smaller than the new sample can
    // never be the minimum again -- the new one is both smaller and younger.
    while (tail != head) {
        const int prev = (tail - 1 + cap) % cap;
        if (value[size_t(prev)] < v)
            break;
        tail = prev;
    }
    value[size_t(tail)] = v;
    index[size_t(tail)] = n;
    tail = (tail + 1) % cap;
    // The deque can never hold more than `window` entries, so a full ring means
    // the oldest is about to expire anyway; dropping it keeps the invariant.
    if (tail == head)
        head = (head + 1) % cap;

    while (head != tail && index[size_t(head)] <= n - window)
        head = (head + 1) % cap;

    ++n;
    return head != tail ? value[size_t(head)] : v;
}

// ---------------------------------------------------------------------------

void Limiter::prepare(double sampleRate, int channels, int maxFrames)
{
    (void)maxFrames;
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = clampTo(channels, 0, kMaxChannels);

    m_lineLength = int(m_sampleRate * double(kMaxLookaheadMs) / 1000.0 + 0.5) + 2;
    m_line.assign(size_t(m_channels) * size_t(m_lineLength), 0.0f);
    m_peakLine.assign(size_t(m_lineLength), 0.0f);
    m_min.prepare(m_lineLength + 2);
    for (int c = 0; c < m_channels; ++c) {
        m_tp1[c].prepare(m_sampleRate);
        m_tp2[c].prepare(m_sampleRate * 2.0);
    }

    m_p = Params{};
    setParams(m_p);
    reset();
}

void Limiter::reset() noexcept
{
    if (!m_line.empty())
        std::memset(m_line.data(), 0, m_line.size() * sizeof(float));
    if (!m_peakLine.empty())
        std::memset(m_peakLine.data(), 0, m_peakLine.size() * sizeof(float));
    m_min.reset();
    for (int c = 0; c < m_channels; ++c) {
        m_tp1[c].reset();
        m_tp2[c].reset();
    }
    m_write = 0;
    m_gain = 1.0f;
}

void Limiter::setParams(const Params &p) noexcept
{
    m_p = p;
    m_preGain = dbToLin(p.gainDb);
    m_threshold = dbToLin(p.thresholdDb);

    int look = int(double(p.lookaheadMs) * m_sampleRate / 1000.0 + 0.5);
    if (look < 0)
        look = 0;
    if (look > m_lineLength - 2)
        look = m_lineLength - 2;
    m_lookahead = look;

    m_releaseCoef = timeConstant(p.releaseMs, m_sampleRate);
}

double Limiter::reductionDb() const noexcept
{
    return m_gain >= 1.0f ? 0.0 : 20.0 * std::log10(double(m_gain) + 1e-12);
}

void Limiter::process(const AudioBuffer &buf)
{
    if (!buf.valid() || m_lineLength <= 0)
        return;

    const int channels = buf.channelCount < m_channels ? buf.channelCount : m_channels;
    const int frames = buf.frames;
    if (channels <= 0)
        return;

    // Stereo-linked: one gain for every channel. A limiter that moved the
    // channels independently would shift the stereo image every time it worked,
    // which is a far more audible fault than the level it is controlling.
    const bool truePeak = m_p.truePeak;

    for (int f = 0; f < frames; ++f) {
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c) {
            float *x = buf.channels[c];
            if (!x)
                continue;
            const float v = x[f] * m_preGain;
            m_line[size_t(c) * size_t(m_lineLength) + size_t(m_write)] = v;
            const float a = v < 0.0f ? -v : v;
            if (a > peak)
                peak = a;
            if (truePeak) {
                // The interpolated phases are where the overshoot lives; the
                // sample itself is already counted above.
                m_tp1[c].tap(v, [&](float u) {
                    m_tp2[c].tap(u, [&](float w) {
                        const float m = w < 0.0f ? -w : w;
                        if (m > peak)
                            peak = m;
                    });
                });
            }
        }
        m_peakLine[size_t(m_write)] = peak;

        // The gain this sample would need on its own. Clamped at 1 so the
        // limiter can only ever reduce.
        const float target = peak > m_threshold ? m_threshold / peak : 1.0f;

        // The smallest target anywhere in the look-ahead window, which is what
        // the gain has to have reached by the time that sample comes out.
        const float want = m_min.push(target, m_lookahead + 1);

        if (want < m_gain)
            m_gain = want;                                  // attack: immediate
        else
            m_gain = want + (m_gain - want) * m_releaseCoef; // release: smoothed

        // Read the delayed sample and apply the gain. The final min is what
        // turns the threshold from an aim into a guarantee: whatever the
        // envelope is doing, no output sample leaves above it.
        const int r = (m_write - m_lookahead + m_lineLength) % m_lineLength;
        // One clamp for every channel, derived from the frame's linked peak.
        // Deriving it per channel -- which is what this used to do -- can hand
        // the two sides different gains for the one sample where the clamp
        // engages, and a limiter that moves the image is a worse fault than the
        // level it is controlling.
        const float ap = m_peakLine[size_t(r)];
        const float g = (ap * m_gain > m_threshold && ap > 0.0f) ? m_threshold / ap : m_gain;
        for (int c = 0; c < channels; ++c) {
            float *x = buf.channels[c];
            if (!x)
                continue;
            const float d = m_line[size_t(c) * size_t(m_lineLength) + size_t(r)];
            bool bad = false;
            x[f] = railed(d * g, &bad);
            if (bad) {
                m_gain = 1.0f;
                std::memset(m_line.data(), 0, m_line.size() * sizeof(float));
            }
        }

        if (++m_write == m_lineLength)
            m_write = 0;
    }
}

} // namespace dreamdsp::dsp
