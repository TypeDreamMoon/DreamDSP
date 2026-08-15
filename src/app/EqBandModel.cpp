#include "app/EqBandModel.h"

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace dreamdsp {

namespace {

// The 10-band ISO octave set most graphic equalizers use.
const double kDefaultFrequencies[] = {
    31.0, 62.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
};

QString formatFrequency(double hz)
{
    if (hz >= 1000.0) {
        const double k = hz / 1000.0;
        if (std::abs(k - std::round(k)) < 0.05)
            return QStringLiteral("%1k").arg(std::round(k));
        return QStringLiteral("%1k").arg(k, 0, 'f', 1);
    }
    return QString::number(std::round(hz));
}

} // namespace

EqBandModel::EqBandModel(QObject *parent)
    : QAbstractListModel(parent)
{
    restoreDefaults();
}

int EqBandModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_bands.size();
}

QVariant EqBandModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_bands.size())
        return {};

    const PresetBand &b = m_bands.at(index.row());
    switch (role) {
    case FrequencyRole: return b.frequency;
    case GainRole:      return b.gainDb;
    case QRole:         return b.q;
    case TypeRole:      return filterTypeIndex(b.type);
    case TypeTokenRole: return QString::fromLatin1(apoToken(b.type));
    case EnabledRole:   return b.enabled;
    case LabelRole:     return formatFrequency(b.frequency);
    case HasGainRole:   return hasGain(b.type);
    default:            return {};
    }
}

QHash<int, QByteArray> EqBandModel::roleNames() const
{
    return {
        { FrequencyRole, "frequency" },
        { GainRole,      "gain" },
        { QRole,         "quality" },
        { TypeRole,      "filterType" },
        { TypeTokenRole, "filterToken" },
        { EnabledRole,   "bandEnabled" },
        { LabelRole,     "label" },
        { HasGainRole,   "hasGain" },
    };
}

void EqBandModel::emitChanged(int row, const QList<int> &roles)
{
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
    emit bandsChanged();
}

void EqBandModel::setGain(int row, double gainDb)
{
    if (row < 0 || row >= m_bands.size())
        return;
    gainDb = std::clamp(gainDb, -30.0, 30.0);
    if (qFuzzyCompare(m_bands[row].gainDb + 1.0, gainDb + 1.0))
        return;
    m_bands[row].gainDb = gainDb;
    emitChanged(row, { GainRole });
}

void EqBandModel::setFrequency(int row, double hz)
{
    if (row < 0 || row >= m_bands.size())
        return;
    hz = std::clamp(hz, 1.0, 24000.0);
    if (qFuzzyCompare(m_bands[row].frequency + 1.0, hz + 1.0))
        return;
    m_bands[row].frequency = hz;
    emitChanged(row, { FrequencyRole, LabelRole });
}

void EqBandModel::setQ(int row, double q)
{
    if (row < 0 || row >= m_bands.size())
        return;
    q = std::clamp(q, 0.01, 100.0);
    if (qFuzzyCompare(m_bands[row].q + 1.0, q + 1.0))
        return;
    m_bands[row].q = q;
    emitChanged(row, { QRole });
}

void EqBandModel::setType(int row, int typeIndex)
{
    if (row < 0 || row >= m_bands.size())
        return;
    const FilterType t = filterTypeFromIndex(typeIndex);
    if (m_bands[row].type == t)
        return;
    m_bands[row].type = t;
    emitChanged(row, { TypeRole, TypeTokenRole, HasGainRole });
}

void EqBandModel::setBandEnabled(int row, bool on)
{
    if (row < 0 || row >= m_bands.size() || m_bands[row].enabled == on)
        return;
    m_bands[row].enabled = on;
    emitChanged(row, { EnabledRole });
}

double EqBandModel::gain(int row) const
{
    if (row < 0 || row >= m_bands.size())
        return 0.0;
    return m_bands.at(row).gainDb;
}

void EqBandModel::zeroGains()
{
    if (m_bands.isEmpty())
        return;
    bool any = false;
    for (PresetBand &b : m_bands) {
        if (!qFuzzyIsNull(b.gainDb)) {
            b.gainDb = 0.0;
            any = true;
        }
    }
    if (!any)
        return;
    emit dataChanged(index(0), index(m_bands.size() - 1), { GainRole });
    emit bandsChanged();
}

void EqBandModel::restoreDefaults()
{
    setBandCount(static_cast<int>(std::size(kDefaultFrequencies)));
}

QVariantList EqBandModel::filterTypeOptions() const
{
    // Short Chinese gloss after the APO token, so the dropdown is readable
    // without having to remember what "LSCQ" stands for.
    static const char *kDescriptions[] = {
        "峰值",           // PK
        "低通 (Q)",       // LPQ
        "高通 (Q)",       // HPQ
        "带通",           // BP
        "低架",           // LS
        "高架",           // HS
        "陷波",           // NO
        "全通",           // AP
        "低架 (中心频率)", // LSC
        "高架 (中心频率)", // HSC
        "低通 Butterworth",
        "高通 Butterworth",
        "低通 Linkwitz-Riley",
        "高通 Linkwitz-Riley",
        "低架 (中心频率, Q)", // LSCQ
        "高架 (中心频率, Q)", // HSCQ
        "低架 (Q)",       // LSQ
        "高架 (Q)",       // HSQ
    };
    static_assert(std::size(kDescriptions) == static_cast<size_t>(FilterType::Count),
                  "descriptions must stay aligned with FilterType");

    QVariantList out;
    for (int i = 0; i < static_cast<int>(FilterType::Count); ++i) {
        out.append(QVariantMap{
            { QStringLiteral("value"), i },
            { QStringLiteral("label"), QStringLiteral("%1 · %2")
                  .arg(QString::fromLatin1(apoToken(filterTypeFromIndex(i))),
                       QString::fromUtf8(kDescriptions[i])) },
        });
    }
    return out;
}

void EqBandModel::insertBandAfter(int row)
{
    if (m_bands.size() >= kMaxBands)
        return;
    row = std::clamp(row, -1, static_cast<int>(m_bands.size()) - 1);

    PresetBand b;
    const int next = row + 1;
    if (row >= 0 && next < m_bands.size()) {
        // Geometric mean: the visual midpoint on a log frequency axis.
        b.frequency = std::sqrt(m_bands[row].frequency * m_bands[next].frequency);
    } else if (row >= 0) {
        b.frequency = std::min(20000.0, m_bands[row].frequency * 2.0);
    } else if (!m_bands.isEmpty()) {
        b.frequency = std::max(20.0, m_bands.first().frequency / 2.0);
    }

    beginInsertRows({}, next, next);
    m_bands.insert(next, b);
    endInsertRows();

    emit countChanged();
    emit bandsChanged();
}

void EqBandModel::removeBand(int row)
{
    if (row < 0 || row >= m_bands.size() || m_bands.size() <= 1)
        return;

    beginRemoveRows({}, row, row);
    m_bands.remove(row);
    endRemoveRows();

    emit countChanged();
    emit bandsChanged();
}

void EqBandModel::setBands(const QVector<PresetBand> &bands)
{
    if (bands.isEmpty())
        return;

    beginResetModel();
    m_bands = bands;
    // An imported .peace file or APO config can name more filters than the
    // parameter block has room for. Cutting here rather than in the publisher
    // means the bands on screen are the bands being applied -- a model holding
    // forty entries while thirty-two reach the audio would make the curve a
    // lie.
    if (m_bands.size() > kMaxBands)
        m_bands.resize(kMaxBands);
    endResetModel();

    emit countChanged();
    emit bandsChanged();
}

void EqBandModel::setBandCount(int count)
{
    count = std::clamp(count, 1, kMaxBands);

    beginResetModel();
    m_bands.clear();
    m_bands.reserve(count);

    const int defaults = static_cast<int>(std::size(kDefaultFrequencies));
    for (int i = 0; i < count; ++i) {
        PresetBand b;
        if (count == defaults) {
            b.frequency = kDefaultFrequencies[i];
        } else {
            // Log-spaced across the audible band when the count is custom.
            const double t = (count == 1) ? 0.5 : double(i) / double(count - 1);
            b.frequency = 20.0 * std::pow(20000.0 / 20.0, t);
        }
        m_bands.push_back(b);
    }
    endResetModel();

    emit countChanged();
    emit bandsChanged();
}

} // namespace dreamdsp
