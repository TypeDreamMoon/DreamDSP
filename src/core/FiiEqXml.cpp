#include "core/FiiEqXml.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <cmath>

namespace dreamdsp {

namespace {

// FiiO writes with a period, but the files get hand-edited on machines with a
// comma locale, so accept both.
bool toNumber(const QString &text, double *out)
{
    bool ok = false;
    const double v = text.trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (!ok)
        return false;
    if (out)
        *out = v;
    return true;
}

QString trimNumber(double v, int decimals)
{
    QString s = QString::number(v, 'f', decimals);
    while (s.contains(QLatin1Char('.')) && (s.endsWith(QLatin1Char('0')) || s.endsWith(QLatin1Char('.'))))
        s.chop(1);
    return s == QLatin1String("-0") ? QStringLiteral("0") : s;
}

// The values the file uses, and what they mean. Anything outside these is not a
// curve, it is a register dump.
constexpr double kMinFreq = 1.0;
constexpr double kMaxFreq = 24000.0;
constexpr double kMaxGain = 30.0;
constexpr double kMinQ = 0.02;
constexpr double kMaxQ = 100.0;

} // namespace

bool FiiEqXml::looksLikeFiiXml(const QByteArray &data)
{
    return data.contains("FiiO_DSP");
}

bool FiiEqXml::read(const QString &path, Preset *out, QString *model, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("打不开 %1:%2").arg(path, f.errorString());
        return false;
    }

    Preset p;
    QString deviceModel;
    if (!readData(f.readAll(), &p, &deviceModel, error))
        return false;

    // A file with no <styleName> is still usable; name it after itself.
    if (p.name.isEmpty())
        p.name = QFileInfo(path).completeBaseName();
    if (p.description.isEmpty())
        p.description = deviceModel;

    if (model) *model = deviceModel;
    *out = p;
    return true;
}

bool FiiEqXml::readData(const QByteArray &data, Preset *out, QString *model, QString *error)
{
    const auto fail = [error](const QString &msg) {
        if (error) *error = msg;
        return false;
    };

    if (!out)
        return fail(QStringLiteral("没有可写入的目标"));

    QXmlStreamReader xml(data);

    Preset p;
    QString deviceModel;
    QString styleName;
    QString description;
    QString version;
    bool sawRoot = false;

    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = xml.name();

            if (name == QLatin1String("FiiO_DSP")) {
                sawRoot = true;
                deviceModel = xml.attributes().value(QStringLiteral("model")).toString();
                version = xml.attributes().value(QStringLiteral("version")).toString();
                continue;
            }
            // <styleName> and <description> are siblings of <module>, at the end.
            if (name == QLatin1String("styleName")) {
                styleName = xml.readElementText().trimmed();
                continue;
            }
            if (name == QLatin1String("description")) {
                description = xml.readElementText().trimmed();
                continue;
            }
            // masterGain lives in <eqGroup>; the rest of the <param>s are the
            // DSP's own settings (bypass, current slot) and are ignored.
            if (name == QLatin1String("param")) {
                const QString key = xml.attributes().value(QStringLiteral("name")).toString();
                const QString value = xml.readElementText().trimmed();
                if (key == QLatin1String("masterGain") || key == QLatin1String("globalGain")
                    || key == QLatin1String("preamp")) {
                    double gain = 0.0;
                    if (toNumber(value, &gain))
                        p.preamp = gain;
                }
                continue;
            }
            if (name == QLatin1String("eq")) {
                const int index = xml.attributes().value(QStringLiteral("index")).toInt();

                int typeCode = 0;
                double freq = 0.0;
                double gain = 0.0;
                double q = kStandardShelfQ;
                bool hasFreq = false;
                bool hasGain = false;
                bool hasQ = false;

                while (!xml.atEnd()) {
                    const QXmlStreamReader::TokenType inner = xml.readNext();
                    if (inner == QXmlStreamReader::EndElement && xml.name() == QLatin1String("eq"))
                        break;
                    if (inner != QXmlStreamReader::StartElement || xml.name() != QLatin1String("param"))
                        continue;

                    const QString key = xml.attributes().value(QStringLiteral("name")).toString();
                    const QString value = xml.readElementText().trimmed();
                    if (key == QLatin1String("type"))
                        typeCode = value.toInt();
                    else if (key == QLatin1String("freq"))
                        hasFreq = toNumber(value, &freq);
                    else if (key == QLatin1String("gain"))
                        hasGain = toNumber(value, &gain);
                    else if (key == QLatin1String("q"))
                        hasQ = toNumber(value, &q);
                    // "s" only exists in the version 1.0 encoding; ignored.
                }

                if (!hasFreq) {
                    // Every real export has freq/gain/q on every band; a band
                    // without one is padding, not a filter.
                    continue;
                }

                PresetBand band;
                band.frequency = freq;
                band.gainDb = hasGain ? gain : 0.0;
                band.q = hasQ ? q : kStandardShelfQ;

                switch (typeCode) {
                case 0:
                    band.type = FilterType::PK;
                    break;
                case 1:
                    band.type = std::abs(band.q - kStandardShelfQ) <= 0.01 ? FilterType::LSC
                                                                          : FilterType::LSCQ;
                    break;
                case 2:
                    band.type = std::abs(band.q - kStandardShelfQ) <= 0.01 ? FilterType::HSC
                                                                          : FilterType::HSCQ;
                    break;
                default:
                    return fail(QStringLiteral("第 %1 段的类型 %2 不是峰值/低架/高架之一,无法读入")
                                    .arg(index + 1)
                                    .arg(typeCode));
                }

                if (band.frequency < kMinFreq || band.frequency > kMaxFreq
                    || std::abs(band.gainDb) > kMaxGain
                    || band.q < kMinQ || band.q > kMaxQ) {
                    return fail(QStringLiteral(
                                    "第 %1 段的数值不像频率/增益/Q(%2 Hz, %3 dB, Q %4)。"
                                    "这个文件可能用的是飞傲 K19 的 version 1.0 编码(整数寄存器值),"
                                    "DreamDSP 只支持 0.0.1 的明文数值格式。")
                                    .arg(index + 1)
                                    .arg(band.frequency)
                                    .arg(band.gainDb)
                                    .arg(band.q));
                }

                p.bands.push_back(band);
            }
        }
    }

    if (xml.hasError()) {
        return fail(QStringLiteral("XML 解析失败(第 %1 行):%2")
                        .arg(xml.lineNumber())
                        .arg(xml.errorString()));
    }
    if (!sawRoot)
        return fail(QStringLiteral("这个文件里没有 <FiiO_DSP> 根节点,不是飞傲的调音文件"));
    if (p.bands.isEmpty())
        return fail(QStringLiteral("文件里没有可用的 <eq> 频段"));
    if (p.bands.size() > kMaxBands) {
        return fail(QStringLiteral("文件有 %1 段,飞傲的客户端最多接受 %2 段")
                        .arg(p.bands.size())
                        .arg(kMaxBands));
    }

    p.name = styleName;
    p.description = description;
    if (p.description.isEmpty() && !deviceModel.isEmpty())
        p.description = deviceModel;

    if (model)
        *model = deviceModel;
    *out = p;
    return true;
}

bool FiiEqXml::write(const QString &path, const Preset &preset, const QString &model, QString *error)
{
    if (!preset.isValid()) {
        if (error) *error = QStringLiteral("当前没有可导出的曲线");
        return false;
    }
    if (preset.bands.size() > kMaxBands) {
        if (error) {
            *error = QStringLiteral("当前有 %1 段,飞傲的客户端最多接受 %2 段")
                         .arg(preset.bands.size())
                         .arg(kMaxBands);
        }
        return false;
    }

    // The XML has no room for a filter FiiO's equalizer cannot show, so say so
    // instead of writing a curve that is quietly missing it.
    for (int i = 0; i < preset.bands.size(); ++i) {
        const FilterType type = preset.bands[i].type;
        if (type != FilterType::PK && type != FilterType::LS && type != FilterType::LSC
            && type != FilterType::LSQ && type != FilterType::LSCQ && type != FilterType::HS
            && type != FilterType::HSC && type != FilterType::HSQ && type != FilterType::HSCQ) {
            if (error) {
                *error = QStringLiteral("第 %1 段是 %2,飞傲的调音文件只支持峰值、低架和高架")
                             .arg(i + 1)
                             .arg(QString::fromLatin1(apoToken(type)));
            }
            return false;
        }
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }

    QXmlStreamWriter xml(&f);
    xml.setAutoFormatting(true);
    xml.writeStartDocument(QStringLiteral("1.0"));

    xml.writeStartElement(QStringLiteral("FiiO_DSP"));
    xml.writeAttribute(QStringLiteral("model"),
                       model.isEmpty() ? QStringLiteral("DreamDSP") : model);
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("0.0.1"));

    xml.writeStartElement(QStringLiteral("module"));
    xml.writeAttribute(QStringLiteral("name"), QStringLiteral("EQ"));
    xml.writeStartElement(QStringLiteral("eqGroup"));

    xml.writeStartElement(QStringLiteral("param"));
    xml.writeAttribute(QStringLiteral("name"), QStringLiteral("masterGain"));
    xml.writeCharacters(trimNumber(preset.preamp, 2));
    xml.writeEndElement();   // param

    xml.writeStartElement(QStringLiteral("eqList"));
    for (int i = 0; i < preset.bands.size(); ++i) {
        const PresetBand &b = preset.bands[i];

        int typeCode = 0;
        if (b.type == FilterType::LS || b.type == FilterType::LSC || b.type == FilterType::LSQ
            || b.type == FilterType::LSCQ) {
            typeCode = 1;
        } else if (b.type == FilterType::HS || b.type == FilterType::HSC || b.type == FilterType::HSQ
                   || b.type == FilterType::HSCQ) {
            typeCode = 2;
        }

        xml.writeStartElement(QStringLiteral("eq"));
        xml.writeAttribute(QStringLiteral("index"), QString::number(i));

        const auto param = [&xml](const char *name, const QString &value) {
            xml.writeStartElement(QStringLiteral("param"));
            xml.writeAttribute(QStringLiteral("name"), QLatin1StringView(name));
            xml.writeCharacters(value);
            xml.writeEndElement();   // param
        };

        param("type", QString::number(typeCode));
        param("freq", trimNumber(b.frequency, 2));
        param("gain", trimNumber(b.gainDb, 2));
        param("q", trimNumber(b.q, 3));

        xml.writeEndElement();   // eq
    }
    xml.writeEndElement();   // eqList
    xml.writeEndElement();   // eqGroup
    xml.writeEndElement();   // module

    xml.writeTextElement(QStringLiteral("styleName"),
                         preset.name.isEmpty() ? QStringLiteral("DreamDSP") : preset.name);
    xml.writeTextElement(QStringLiteral("description"), preset.description);

    xml.writeEndElement();   // FiiO_DSP
    xml.writeEndDocument();

    // QXmlStreamWriter reports failure as a flag and nothing else; the useful
    // diagnostic is the file we were writing.
    if (xml.hasError()) {
        if (error) *error = QStringLiteral("写入 XML 失败:%1").arg(path);
        return false;
    }
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace dreamdsp
