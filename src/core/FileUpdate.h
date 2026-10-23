#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dreamdsp {

// A file-level update: the release workflow publishes what changed since the
// previous version, and this reads that, stages it, and swaps it in.
//
// Why a file list rather than a binary diff of the installer: the payload is
// some 40 MB of Qt runtime that does not change between releases and a few MB
// of our own code that does, so "copy the files whose SHA-256 differs" is most
// of the saving with none of the diff machinery -- and, unlike a patch against
// the old installer, it does not need a copy of the old installer, which
// nobody keeps.
//
// The container is deliberately uncompressed. A delta is a few MB, and every
// format that has to be reproduced independently on both sides -- C++ here,
// PowerShell in the workflow -- is a chance to be wrong exactly once, on the
// release that nobody can install.
//
//   "DDUPD1\n"          7 bytes: magic and format
//   <uint32 BE>         4 bytes: header length
//   <header JSON>       UTF-8
//   <payload>           the files' bytes, concatenated in header order
//
//   {"version":"0.5.1","base":"0.5.0",
//    "files":[{"path":"DreamDSP.exe","size":5970000,"sha256":"..."}],
//    "remove":["qml/old/thing.qml"]}
//
// Verified twice, on purpose. The container as a whole is checked against the
// SHA-256 published in the release notes -- that is a property of the
// download -- and every file inside is checked again against the hash in the
// header, which is a property of the plan about to be applied to a directory
// of the user's programs.
//
// Paths in a package are relative and are validated as such. A package has no
// business naming an absolute path or "..", and one that does is refused
// rather than cleaned up.
struct FileUpdateEntry {
    QString path;      // relative, '/'-separated
    qint64 size = 0;
    QString sha256;
};

struct FileUpdate {
    QString version;   // what this package installs
    QString base;      // the version it applies on top of
    QVector<FileUpdateEntry> files;
    QStringList remove;

    // Where the payload starts in the container; filled in by read().
    qint64 payloadOffset = 0;

    bool isEmpty() const { return files.isEmpty() && remove.isEmpty(); }
    qint64 payloadSize() const;
};

class FileUpdatePackage
{
public:
    // %LOCALAPPDATA%/DreamDSP/pending. Local, not roaming: it holds megabytes
    // of file payload, and it means nothing on another machine.
    static QString stageDirectory();

    static bool read(const QString &containerPath, FileUpdate *out, QString *error = nullptr);

    // Verifies every file and writes the staged copy plus plan.json. Nothing
    // reaches the stage directory before its hash matches, so a half-trusted
    // download cannot leave a half-written tree behind.
    static bool stage(const QString &containerPath, const FileUpdate &pkg,
                      QString *error = nullptr);
    // The same, into a directory of the caller's choosing. The self-test uses
    // this so it cannot walk over an update that is genuinely waiting.
    static bool stageInto(const QString &containerPath, const FileUpdate &pkg,
                          const QString &stageDir, QString *error = nullptr);

    // "Install and restart" drops this before quitting and main() rereads it
    // after the event loop, so the new copy starts without the user going to
    // find the shortcut again. It lives beside the stage directory rather than
    // inside it, because a successful apply deletes the latter.
    static QString restartMarkerPath();

    static bool isStaged();
    static QString stagedVersion();   // empty when nothing is waiting
    // True when a start already tried to apply what is staged and something
    // would not let go. The updater uses it to stop recommending the delta.
    static bool lastApplyFailed();

    struct ApplyResult {
        bool applied = false;      // something was swapped in this run
        bool complete = false;     // nothing left over for the next start
        QString version;
        QStringList replaced;
        QStringList failed;        // still locked; kept for the next start
        QString error;
    };

    // Run at start-up, before anything is loaded out of the directory -- Qt and
    // HuskarUI are already mapped by then, and a loaded image cannot be
    // overwritten, only renamed out of the way.
    static ApplyResult applyPending(const QString &installDir);

    // True when this run already swapped the application directory. The
    // version reported by the process is the old one, so the user would
    // otherwise be told an update is available that has just been installed.
    static bool appliedThisRun();
    static QString appliedVersionThisRun();

    // Builds a container from files that already exist on disk, mirroring what
    // release.yml does in PowerShell. Used by --selftest, so the reader is
    // tested against bytes rather than against its own idea of them.
    static bool writeFromDirectory(const QString &containerPath,
                                   const QString &version, const QString &base,
                                   const QStringList &relativePaths,
                                   const QStringList &remove,
                                   const QString &sourceDir,
                                   QString *error = nullptr);

    // Replaces the running image's directory files with a staged package.
    // Exposed for the self-test, which runs it against a temporary directory.
    static ApplyResult applyFrom(const QString &stageDir, const QString &installDir);
};

} // namespace dreamdsp
