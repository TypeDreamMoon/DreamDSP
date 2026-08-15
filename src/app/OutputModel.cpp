#include "app/OutputModel.h"

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dreamdsp {

namespace {

// Windows' channel order for the standard masks. Anything past eight is not
// something a shared-mode endpoint hands to a post-mix object.
const char *kChannelNames[] = { "L", "R", "C", "LFE", "SL", "SR", "BL", "BR" };

constexpr double kSpeedOfSound = 343.0;   // m/s at 20 C

struct MatrixPreset {
    const char *label;
    // gain[out][in] for the first two channels; everything else stays identity.
    float g[2][2];
};

// Named layouts, covering what `Copy:` is used for in practice. Anything else
// is edited in the grid, and then the preset reads as "自定义".
const MatrixPreset kPresets[] = {
    { "正常",        { { 1.0f, 0.0f }, { 0.0f, 1.0f } } },
    { "交换左右",     { { 0.0f, 1.0f }, { 1.0f, 0.0f } } },
    { "合并为单声道",  { { 0.5f, 0.5f }, { 0.5f, 0.5f } } },
    { "只用左声道",    { { 1.0f, 0.0f }, { 1.0f, 0.0f } } },
    { "只用右声道",    { { 0.0f, 1.0f }, { 0.0f, 1.0f } } },
};

dsp::ChannelMatrix::Params presetMatrix(int index)
{
    dsp::ChannelMatrix::Params m = dsp::ChannelMatrix::identity();
    if (index < 0 || index >= int(std::size(kPresets)))
        return m;
    for (int o = 0; o < 2; ++o)
        for (int i = 0; i < 2; ++i)
            m.gain[o][i] = kPresets[index].g[o][i];
    return m;
}

} // namespace

OutputModel::OutputModel(QObject *parent)
    : QObject(parent)
{
    m_matrix = dsp::ChannelMatrix::identity();
    std::memset(&m_delay, 0, sizeof m_delay);
    std::memset(&m_loudness, 0, sizeof m_loudness);
    m_loudness.amount = 1.0f;
    m_loudness.referenceDb = 0.0f;
    m_limiter = dsp::Limiter::Params{};
    refreshShelves();
}

// ------------------------------------------------------------------- matrix

void OutputModel::setMatrixOn(bool on)
{
    if (m_matrixOn == on)
        return;
    m_matrixOn = on;
    emit changed();
}

QVariantList OutputModel::matrixPresets() const
{
    QVariantList out;
    for (int i = 0; i < int(std::size(kPresets)); ++i) {
        out.append(QVariantMap{ { QStringLiteral("value"), i },
                                { QStringLiteral("label"),
                                  QString::fromUtf8(kPresets[i].label) } });
    }
    return out;
}

int OutputModel::matrixPreset() const
{
    for (int i = 0; i < int(std::size(kPresets)); ++i) {
        const dsp::ChannelMatrix::Params p = presetMatrix(i);
        if (std::memcmp(&p, &m_matrix, sizeof p) == 0)
            return i;
    }
    return -1;
}

void OutputModel::applyMatrixPreset(int index)
{
    m_matrix = presetMatrix(index);
    emit changed();
}

double OutputModel::matrixGain(int out, int in) const
{
    if (out < 0 || out >= kMaxChannels || in < 0 || in >= kMaxChannels)
        return 0.0;
    return double(m_matrix.gain[out][in]);
}

void OutputModel::setMatrixGain(int out, int in, double gain)
{
    if (out < 0 || out >= kMaxChannels || in < 0 || in >= kMaxChannels)
        return;
    const float g = float(std::clamp(gain, -4.0, 4.0));
    if (m_matrix.gain[out][in] == g)
        return;
    m_matrix.gain[out][in] = g;
    emit changed();
}

// -------------------------------------------------------------------- delay

void OutputModel::setDelayOn(bool on)
{
    if (m_delayOn == on)
        return;
    m_delayOn = on;
    emit changed();
}

double OutputModel::delayMs(int channel) const
{
    if (channel < 0 || channel >= kMaxChannels)
        return 0.0;
    return double(m_delay.ms[channel]);
}

void OutputModel::setDelayMs(int channel, double ms)
{
    if (channel < 0 || channel >= kMaxChannels)
        return;
    const float v = float(std::clamp(ms, 0.0, double(dsp::ChannelDelay::kMaxMs)));
    if (m_delay.ms[channel] == v)
        return;
    m_delay.ms[channel] = v;
    emit changed();
}

double OutputModel::delayCm(int channel) const
{
    return delayMs(channel) * kSpeedOfSound / 10.0;   // ms * m/s -> cm
}

void OutputModel::setDelayCm(int channel, double cm)
{
    setDelayMs(channel, cm * 10.0 / kSpeedOfSound);
}

double OutputModel::maxDelayMs() const
{
    double m = 0.0;
    for (int c = 0; c < kMaxChannels; ++c)
        m = std::max(m, double(m_delay.ms[c]));
    return m;
}

// ----------------------------------------------------------------- loudness

void OutputModel::setLoudnessOn(bool on)
{
    if (m_loudnessOn == on)
        return;
    m_loudnessOn = on;
    // Switching it on with the reference still at 0 dB would apply the full
    // correction immediately, which is startling and almost never what was
    // meant. Pinning the reference to the level you are already listening at
    // makes the first moment silent and the effect grow as you turn down.
    if (on && m_volumeKnown && m_loudness.referenceDb == 0.0f && m_volumeDb < 0.0)
        m_loudness.referenceDb = float(m_volumeDb);
    refreshShelves();
    emit changed();
}

void OutputModel::setLoudnessAmount(double v)
{
    const float a = float(std::clamp(v, 0.0, 1.0));
    if (m_loudness.amount == a)
        return;
    m_loudness.amount = a;
    refreshShelves();
    emit changed();
}

void OutputModel::setReferenceDb(double v)
{
    const float d = float(std::clamp(v, -120.0, 0.0));
    if (m_loudness.referenceDb == d)
        return;
    m_loudness.referenceDb = d;
    refreshShelves();
    emit changed();
}

void OutputModel::useCurrentVolumeAsReference()
{
    if (!m_volumeKnown)
        return;
    setReferenceDb(m_volumeDb);
}

void OutputModel::setVolume(double db, bool known)
{
    if (m_volumeKnown == known && qFuzzyCompare(m_volumeDb + 1.0, db + 1.0))
        return;
    m_volumeDb = db;
    m_volumeKnown = known;
    emit volumeChanged();

    refreshShelves();
    // The volume is part of the parameter block, so a change to it has to be
    // published like any other -- the stage is a pure function of what it is
    // sent, which is the whole reason audiodg does no COM of its own.
    if (m_loudnessOn)
        emit changed();
}

void OutputModel::refreshShelves()
{
    const dsp::LoudnessCorrection::Params p = loudnessParams();
    double lo = 0.0, hi = 0.0, pre = 0.0;
    dsp::LoudnessCorrection::shelvesFor(p, &lo, &hi, &pre);
    if (qFuzzyCompare(lo + 1.0, m_lowDb + 1.0) && qFuzzyCompare(hi + 1.0, m_highDb + 1.0)
        && qFuzzyCompare(pre + 1.0, m_preampDb + 1.0)) {
        return;
    }
    m_lowDb = lo;
    m_highDb = hi;
    m_preampDb = pre;
    emit shelvesChanged();
}

dsp::LoudnessCorrection::Params OutputModel::loudnessParams() const
{
    dsp::LoudnessCorrection::Params p = m_loudness;
    // An unknown volume means no correction rather than a guess: a reading the
    // interface does not have is not a reading of zero.
    p.volumeDb = m_volumeKnown ? float(m_volumeDb) : p.referenceDb;
    return p;
}

// ----------------------------------------------------------------- limiter

namespace {
bool putF(float &dst, double v, double lo, double hi)
{
    const float f = float(std::clamp(v, lo, hi));
    if (dst == f)
        return false;
    dst = f;
    return true;
}
} // namespace

void OutputModel::setLimiterOn(bool on)
{
    if (m_limiterOn == on)
        return;
    m_limiterOn = on;
    emit changed();
}

void OutputModel::setLimiterGain(double v)      { if (putF(m_limiter.gainDb, v, -24.0, 24.0))     emit changed(); }
void OutputModel::setLimiterThreshold(double v) { if (putF(m_limiter.thresholdDb, v, -30.0, 0.0)) emit changed(); }
void OutputModel::setLimiterRelease(double v)   { if (putF(m_limiter.releaseMs, v, 1.0, 1000.0))  emit changed(); }
void OutputModel::setLimiterLookahead(double v)
{
    if (putF(m_limiter.lookaheadMs, v, 0.0, double(dsp::Limiter::kMaxLookaheadMs)))
        emit changed();
}

// ------------------------------------------------------------------ general

void OutputModel::setChannels(int n)
{
    n = std::clamp(n, 1, kMaxChannels);
    if (m_channels == n)
        return;
    m_channels = n;
    emit channelsChanged();
}

QStringList OutputModel::channelNames() const
{
    QStringList out;
    for (int i = 0; i < m_channels; ++i)
        out << QString::fromLatin1(kChannelNames[i]);
    return out;
}

uint32_t OutputModel::enableBits() const
{
    uint32_t mask = 0;
    // The matrix is skipped when it is the identity even if the switch is on:
    // running a full N x N multiply to copy each channel to itself is work for
    // nothing, and the stage is the one place where "enabled" and "does
    // something" can differ by an entire matrix.
    if (m_matrixOn && !dsp::ChannelMatrix::isIdentity(m_matrix))
        mask |= dsp::kEnMatrix;
    if (m_delayOn && maxDelayMs() > 0.0)
        mask |= dsp::kEnDelay;
    if (m_loudnessOn)
        mask |= dsp::kEnLoudness;
    if (m_limiterOn)
        mask |= dsp::kEnLimiter;
    return mask;
}

void OutputModel::restore(const dsp::ParamBlock &b)
{
    m_matrix = b.matrix;
    m_delay = b.delay;
    m_loudness = b.loudness;
    m_limiter = b.limiter;
    // A block written before the limiter existed carries zeros, and a zero
    // release would make the envelope stick. Falling back to the defaults is
    // the difference between "not configured" and "configured to nothing".
    if (!(m_limiter.releaseMs > 0.0f))
        m_limiter = dsp::Limiter::Params{};
    if (!(m_loudness.amount > 0.0f))
        m_loudness.amount = 1.0f;

    m_matrixOn = (b.enableMask & dsp::kEnMatrix) != 0;
    m_delayOn = (b.enableMask & dsp::kEnDelay) != 0;
    m_loudnessOn = (b.enableMask & dsp::kEnLoudness) != 0;
    m_limiterOn = (b.enableMask & dsp::kEnLimiter) != 0;

    refreshShelves();
    emit changed();
}

} // namespace dreamdsp
