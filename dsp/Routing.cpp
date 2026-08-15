#include "Routing.h"

#include <cmath>
#include <cstring>

namespace dreamdsp::dsp {

// ------------------------------------------------------------ channel matrix

ChannelMatrix::Params ChannelMatrix::identity() noexcept
{
    Params p;
    std::memset(&p, 0, sizeof p);
    for (int i = 0; i < kMaxChannels; ++i)
        p.gain[i][i] = 1.0f;
    return p;
}

bool ChannelMatrix::isIdentity(const Params &p) noexcept
{
    for (int o = 0; o < kMaxChannels; ++o)
        for (int i = 0; i < kMaxChannels; ++i)
            if (p.gain[o][i] != (o == i ? 1.0f : 0.0f))
                return false;
    return true;
}

void ChannelMatrix::prepare(double sampleRate, int channels, int maxFrames)
{
    (void)sampleRate;
    m_channels = clampTo(channels, 0, kMaxChannels);
    m_scratch.assign(size_t(m_channels) * size_t(maxFrames > 0 ? maxFrames : 0), 0.0f);
    m_p = identity();
    m_identity = true;
}

void ChannelMatrix::setParams(const Params &p) noexcept
{
    m_p = p;
    m_identity = isIdentity(p);
}

void ChannelMatrix::process(const AudioBuffer &buf)
{
    if (m_identity || !buf.valid())
        return;

    const int channels = buf.channelCount < m_channels ? buf.channelCount : m_channels;
    const int frames = buf.frames;
    if (channels <= 0 || frames <= 0)
        return;

    // Bail rather than write past the buffer sized in prepare(). A stream that
    // hands over more frames than it declared is a broken host, and truncating
    // its audio silently would be worse than leaving it alone.
    if (size_t(channels) * size_t(frames) > m_scratch.size())
        return;

    float *scratch = m_scratch.data();
    for (int c = 0; c < channels; ++c) {
        if (!buf.channels[c])
            return;
        std::memcpy(scratch + size_t(c) * size_t(frames), buf.channels[c],
                    size_t(frames) * sizeof(float));
    }

    bool bad = false;
    for (int o = 0; o < channels; ++o) {
        float *dst = buf.channels[o];
        const float *row = m_p.gain[o];

        for (int f = 0; f < frames; ++f) {
            float acc = 0.0f;
            for (int i = 0; i < channels; ++i)
                acc += row[i] * scratch[size_t(i) * size_t(frames) + size_t(f)];
            // Eight inputs at full weight is +18 dB, and the matrix sits after
            // everything else in the chain, so nothing upstream bounds it.
            dst[f] = railed(acc, &bad);
        }
    }
}

// --------------------------------------------------------------- delay lines

void ChannelDelay::prepare(double sampleRate, int channels, int maxFrames)
{
    (void)maxFrames;
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = clampTo(channels, 0, kMaxChannels);

    // Three extra samples: the interpolator reads one ahead of the integer tap
    // and two behind it, and the line must never let a read overtake the write.
    m_lineLength = int(m_sampleRate * double(kMaxMs) / 1000.0 + 0.5) + 4;

    m_line.assign(size_t(m_channels) * size_t(m_lineLength), 0.0f);
    std::memset(&m_p, 0, sizeof m_p);
    std::memset(m_intDelay, 0, sizeof m_intDelay);
    std::memset(m_coeff, 0, sizeof m_coeff);
    std::memset(m_active, 0, sizeof m_active);
    m_maxDelaySamples = 0;
    m_write = 0;
}

void ChannelDelay::reset() noexcept
{
    std::memset(m_line.data(), 0, m_line.size() * sizeof(float));
    m_write = 0;
}

void ChannelDelay::setParams(const Params &p) noexcept
{
    m_p = p;
    m_maxDelaySamples = 0;

    for (int c = 0; c < kMaxChannels; ++c) {
        double ms = double(p.ms[c]);
        if (!(ms > 0.0)) {
            m_active[c] = false;
            m_intDelay[c] = 0;
            continue;
        }
        if (ms > double(kMaxMs))
            ms = double(kMaxMs);

        double samples = ms * m_sampleRate / 1000.0;
        // The interpolator needs one sample ahead of the integer tap, so the
        // smallest delay it can express is one sample. Snapping up rather than
        // down keeps a small positive delay from becoming no delay at all.
        if (samples < 1.0)
            samples = 1.0;
        if (samples > double(m_lineLength - 3))
            samples = double(m_lineLength - 3);

        const int whole = int(samples);
        const double u = samples - double(whole);

        // Third-order Lagrange, taps at n-D+1, n-D, n-D-1, n-D-2. At u = 0 the
        // coefficients collapse to (0, 1, 0, 0), i.e. an exact integer delay --
        // which is what makes a whole-sample setting bit-exact rather than
        // merely close.
        m_coeff[c][0] = float(-u * (u - 1.0) * (u - 2.0) / 6.0);
        m_coeff[c][1] = float((u + 1.0) * (u - 1.0) * (u - 2.0) / 2.0);
        m_coeff[c][2] = float(-(u + 1.0) * u * (u - 2.0) / 2.0);
        m_coeff[c][3] = float((u + 1.0) * u * (u - 1.0) / 6.0);

        m_intDelay[c] = whole;
        m_active[c] = true;
        if (whole + 2 > m_maxDelaySamples)
            m_maxDelaySamples = whole + 2;
    }
}

void ChannelDelay::process(const AudioBuffer &buf)
{
    if (!buf.valid() || m_lineLength <= 0)
        return;

    const int channels = buf.channelCount < m_channels ? buf.channelCount : m_channels;
    const int frames = buf.frames;

    for (int c = 0; c < channels; ++c) {
        float *x = buf.channels[c];
        if (!x)
            continue;

        float *line = m_line.data() + size_t(c) * size_t(m_lineLength);
        int w = m_write;

        // A channel with no delay still has to be written into the line, or
        // switching it on later would read history that was never recorded and
        // start with a burst of whatever was in the buffer.
        if (!m_active[c]) {
            for (int f = 0; f < frames; ++f) {
                line[w] = x[f];
                if (++w == m_lineLength)
                    w = 0;
            }
            continue;
        }

        const int d = m_intDelay[c];
        const float k0 = m_coeff[c][0], k1 = m_coeff[c][1];
        const float k2 = m_coeff[c][2], k3 = m_coeff[c][3];
        bool bad = false;

        for (int f = 0; f < frames; ++f) {
            line[w] = x[f];

            // Read positions relative to the sample just written. Modulo by
            // conditional subtraction after adding the length, so the index is
            // always in range without a division in the inner loop.
            int r = w - d + 1;
            if (r < 0) r += m_lineLength;
            const float a0 = line[r];
            if (--r < 0) r += m_lineLength;
            const float a1 = line[r];
            if (--r < 0) r += m_lineLength;
            const float a2 = line[r];
            if (--r < 0) r += m_lineLength;
            const float a3 = line[r];

            x[f] = railed(k0 * a0 + k1 * a1 + k2 * a2 + k3 * a3, &bad);

            if (++w == m_lineLength)
                w = 0;
        }

        // A NaN written into the line would be read back for as long as the
        // line is deep. Clearing the channel costs one buffer and makes it
        // self-healing, exactly as in the equalizer.
        if (bad)
            std::memset(line, 0, size_t(m_lineLength) * sizeof(float));
    }

    // Every channel advances the same write cursor, so it is stepped once here
    // rather than per channel -- the per-channel loops all start from the same
    // m_write and must all end at the same place.
    m_write += frames;
    m_write %= m_lineLength;
}

} // namespace dreamdsp::dsp
