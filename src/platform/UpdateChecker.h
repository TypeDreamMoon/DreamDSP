#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;

namespace dreamdsp {

// Checks GitHub for a newer release, and installs one on request.
//
// The security of this is the whole design, not a detail of it: an updater
// downloads a file and then runs it as the user. Three things guard that here,
// and none of them is optional --
//
//   * only https, only api.github.com and objects.githubusercontent.com, and
//     only the release assets of one hard-coded repository. A redirect
//     anywhere else is refused rather than followed;
//   * the download is checked against a SHA-256 published in the release notes
//     by the build that produced it. A mismatch is a hard failure with the file
//     deleted, never a warning;
//   * installing is a separate, explicit action. Downloading in the background
//     is fine; replacing a component that lives inside audiodg.exe and
//     restarts the machine's audio service is not something to do while
//     somebody is in a call.
//
// Two kinds of update are published for every release after the one that
// introduced deltas: the installer, and a file-level delta that applies on top
// of the immediately previous version. The delta is preferred when this copy
// is on that version, because it is a few MB instead of fifty; both are held to
// the same SHA-256 discipline, and a delta that fails to verify or to stage
// leaves the installer path available rather than dead-ending.
class UpdateChecker : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by AppController")

    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY stateChanged)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(int downloadPercent READ downloadPercent NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(bool downloaded READ downloaded NOTIFY stateChanged)
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY automaticChanged)
    // The delta that applies on top of this copy's version, when the release
    // published one. `deltaStaged` means it is downloaded, verified and waiting
    // for a restart -- not installed, because the files are in use.
    Q_PROPERTY(bool deltaAvailable READ deltaAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool deltaStaged READ deltaStaged NOTIFY stateChanged)
    Q_PROPERTY(double deltaMegabytes READ deltaMegabytes NOTIFY stateChanged)
    // A staged package that the last start could not finish applying, with the
    // version it belongs to. Named so the interface can say which update is
    // waiting instead of "an update".
    Q_PROPERTY(QString pendingVersion READ pendingVersion NOTIFY stateChanged)

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    QString currentVersion() const;
    QString latestVersion() const { return m_latestVersion; }
    QString releaseNotes() const { return m_releaseNotes; }
    QString releaseUrl() const { return m_releaseUrl; }
    bool updateAvailable() const { return m_updateAvailable; }
    bool busy() const { return m_busy; }
    int downloadPercent() const { return m_percent; }
    QString status() const { return m_status; }
    // A package that cannot land does not count as "downloaded": the thing to
    // offer then is the installer.
    bool downloaded() const
    {
        return !m_deltaBlocked && (!m_downloadedPath.isEmpty() || m_deltaStaged);
    }

    // Not offered when a start already tried and could not finish: repeating a
    // package that will not land is not an update, it is a loop.
    bool deltaAvailable() const { return !m_deltaUrl.isEmpty() && !m_deltaBlocked; }
    bool deltaStaged() const { return m_deltaStaged && !m_deltaBlocked; }
    double deltaMegabytes() const { return double(m_deltaSize) / (1024.0 * 1024.0); }
    QString pendingVersion() const { return m_pendingVersion; }

    // Check on start-up, and daily thereafter. On by default; it asks GitHub
    // for one small JSON document and sends nothing about the machine.
    bool automatic() const { return m_automatic; }
    void setAutomatic(bool on);

    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void download();
    // Runs the verified installer and closes this copy so it can be replaced.
    Q_INVOKABLE void installNow();
    Q_INVOKABLE void openReleasePage() const;

    // Compares two "1.2.3" strings. Public because it is worth testing on its
    // own -- a comparison that says 0.10.0 is older than 0.9.0 would keep an
    // installed copy from ever updating again.
    static int compareVersions(const QString &a, const QString &b);

signals:
    void stateChanged();
    void progressChanged();
    void automaticChanged();
    // Raised when the installer is about to run and the application must exit.
    void quitRequested();

private:
    void setStatus(const QString &s);
    void onCheckFinished(QNetworkReply *reply);
    void onDownloadFinished(QNetworkReply *reply);

    QNetworkAccessManager *m_network = nullptr;

    QString m_latestVersion;
    QString m_releaseNotes;
    QString m_releaseUrl;
    QString m_assetUrl;
    QString m_assetName;
    QString m_expectedSha256;
    qint64 m_assetSize = 0;

    QString m_downloadedPath;
    QString m_status;

    // The file-level delta: where it is, what it must hash to, and whether it
    // is staged. `m_deltaMode` is what the current download is, because one
    // code path fetches both kinds.
    QString m_deltaUrl;
    QString m_deltaName;
    QString m_deltaSha256;
    qint64 m_deltaSize = 0;
    bool m_deltaStaged = false;
    bool m_deltaMode = false;
    bool m_deltaBlocked = false;
    QString m_pendingVersion;

    bool m_updateAvailable = false;
    bool m_busy = false;
    bool m_automatic = true;
    int m_percent = 0;
};

} // namespace dreamdsp
