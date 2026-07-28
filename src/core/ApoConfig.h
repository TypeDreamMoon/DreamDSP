#pragma once

#include <QString>
#include <QStringList>

namespace dreamdsp {

// Reading and writing Equalizer APO configuration files.
//
// Two hard rules learned from APO's own parser, both of which silently corrupt
// a configuration if broken:
//   * never write a UTF-8 BOM  -- APO would drop the first command line;
//   * never rewrite lines we do not understand -- user-authored commands must
//     survive untouched, so config.txt is edited as a line list, not re-emitted
//     from a parsed model.
class ApoConfig
{
public:
    // A missing file is not an error: it yields an empty list.
    static bool readLines(const QString &path, QStringList *out, QString *error = nullptr);

    // UTF-8 without BOM, CRLF line endings, written atomically.
    static bool writeLines(const QString &path, const QStringList &lines, QString *error = nullptr);
    static bool writeText(const QString &path, const QString &text, QString *error = nullptr);

    // "Include: <fileName>" management, case-insensitive on the command token.
    static bool hasInclude(const QStringList &lines, const QString &fileName);
    static QStringList withInclude(const QStringList &lines, const QString &fileName);
    static QStringList withoutInclude(const QStringList &lines, const QString &fileName);
};

} // namespace dreamdsp
