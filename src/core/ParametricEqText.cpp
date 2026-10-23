#include "core/ParametricEqText.h"

#include "core/Biquad.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace dreamdsp {

namespace {

const QRegularExpression &preampExpression()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*preamp\s*:\s*(-?[\d.,]+)\s*db)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

// "Filter 3: ON LSC Fc 105 Hz Gain 3.0 dB Q 0.70"
// The index, the ON/OFF flag and the gain are all optional in the wild; Hz, dB
// and the Q are required to mean anything, and Fc is what identifies the line.
const QRegularExpression &filterExpression()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*filter\s*\d*\s*:\s*(?:(on|off)\s+)?([A-Za-z]+)\s+fc\s+(-?[\d.,]+)\s*hz(?:(?:\s+gain\s+(-?[\d.,]+)\s*db)?)(?:\s+q\s+([\d.,]+))?)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

double toNumber(const QString &text, double fallback)
{
    bool ok = false;
    const double v = text.trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok ? v : fallback;
}

} // namespace

bool ParametricEqText::looksLikeParametricEq(const QString &text)
{
    // Cheap: the keyword plus the parameter that makes a line a filter. A
    // GraphicEQ file has neither.
    return text.contains(QLatin1String("Filter"), Qt::CaseInsensitive)
           && text.contains(QLatin1String("Fc"), Qt::CaseInsensitive);
}

bool ParametricEqText::read(const QString &text, Preset *out, QString *error)
{
    if (!out) {
        if (error) *error = QStringLiteral("没有可写入的目标");
        return false;
    }

    Preset p;
    // One record per line, so an import of a whole review's table (several
    // curves in one file) stops at the first block and does not splice curves
    // that were never meant to be combined.
    bool seenFilter = false;
    bool sawAnything = false;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        if (!seenFilter) {
            const auto pre = preampExpression().match(line);
            if (pre.hasMatch()) {
                p.preamp = toNumber(pre.captured(1), 0.0);
                sawAnything = true;
                continue;
            }
        }

        const auto m = filterExpression().match(line);
        if (!m.hasMatch())
            continue;

        bool ok = false;
        const FilterType type = filterTypeFromToken(m.captured(2), &ok);
        if (!ok)
            continue;

        PresetBand b;
        b.type = type;
        b.frequency = std::clamp(toNumber(m.captured(3), 1000.0), 1.0, 24000.0);
        b.gainDb = std::clamp(m.captured(4).isEmpty() ? 0.0 : toNumber(m.captured(4), 0.0),
                              -40.0, 40.0);
        b.q = std::clamp(m.captured(5).isEmpty() ? 0.707 : toNumber(m.captured(5), 0.707),
                         0.05, 100.0);
        b.enabled = m.captured(1).compare(QLatin1String("off"), Qt::CaseInsensitive) != 0;

        p.bands.push_back(b);
        seenFilter = true;
        sawAnything = true;
    }

    if (p.bands.isEmpty()) {
        if (error) {
            *error = sawAnything
                         ? QStringLiteral("只读到了 Preamp,没有可用的 Filter 行")
                         : QStringLiteral("这段文本里没有 Filter 行");
        }
        return false;
    }

    *out = p;
    return true;
}

} // namespace dreamdsp
