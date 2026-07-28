#include "core/ApoConfig.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThread>

namespace dreamdsp {

namespace {

// APO splits a line at the first colon, trims the command, and compares
// case-insensitively. Mirror that so we recognise "include:" and " Include :".
bool isIncludeOf(const QString &line, const QString &fileName)
{
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return false;
    if (line.left(colon).trimmed().compare(QLatin1String("Include"), Qt::CaseInsensitive) != 0)
        return false;
    return line.mid(colon + 1).trimmed().compare(fileName, Qt::CaseInsensitive) == 0;
}

} // namespace

bool ApoConfig::readLines(const QString &path, QStringList *out, QString *error)
{
    if (out)
        out->clear();
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("empty path");
        return false;
    }
    if (!QFileInfo::exists(path))
        return true; // absent == empty, not an error

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }

    QByteArray raw = f.readAll();
    // Defensive: if some other tool wrote a BOM, strip it rather than making it
    // part of the first command.
    if (raw.startsWith("\xEF\xBB\xBF"))
        raw.remove(0, 3);

    const QString text = QString::fromUtf8(raw);
    if (out)
        *out = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));

    // split() on a trailing newline leaves an empty last element; drop it so
    // round-tripping does not grow the file by one blank line each time.
    if (out && !out->isEmpty() && out->last().isEmpty())
        out->removeLast();
    return true;
}

bool ApoConfig::writeText(const QString &path, const QString &text, QString *error)
{
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("empty path");
        return false;
    }

    const QByteArray bytes = text.toUtf8(); // deliberately no BOM

    // Equalizer APO is reading this directory continuously, and another editor
    // (or a second copy of us) may be writing it. Both ends lose the race
    // occasionally, which surfaces as a sharing violation on the rename.
    // APO's own editor handles this by retrying; do the same rather than
    // reporting a failure the user can do nothing about.
    constexpr int kAttempts = 8;
    QString lastError;

    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)
            && f.write(bytes) == bytes.size()
            && f.commit()) {
            return true;
        }
        lastError = f.errorString();
        f.cancelWriting();
        QThread::msleep(4 * (attempt + 1));   // 4, 8, 12 ... ms
    }

    if (error)
        *error = lastError;
    return false;
}

bool ApoConfig::writeLines(const QString &path, const QStringList &lines, QString *error)
{
    QString text = lines.join(QStringLiteral("\r\n"));
    if (!text.isEmpty())
        text += QStringLiteral("\r\n");
    return writeText(path, text, error);
}

bool ApoConfig::hasInclude(const QStringList &lines, const QString &fileName)
{
    for (const QString &line : lines) {
        if (isIncludeOf(line, fileName))
            return true;
    }
    return false;
}

QStringList ApoConfig::withInclude(const QStringList &lines, const QString &fileName)
{
    if (hasInclude(lines, fileName))
        return lines;

    QStringList out = lines;
    // Append rather than prepend: later filters stack on top of whatever the
    // user (or Peace) already had, which is the least surprising behaviour.
    while (!out.isEmpty() && out.last().trimmed().isEmpty())
        out.removeLast();
    out << QStringLiteral("Include: %1").arg(fileName);
    return out;
}

QStringList ApoConfig::withoutInclude(const QStringList &lines, const QString &fileName)
{
    QStringList out;
    out.reserve(lines.size());
    for (const QString &line : lines) {
        if (!isIncludeOf(line, fileName))
            out << line;
    }
    return out;
}

} // namespace dreamdsp
