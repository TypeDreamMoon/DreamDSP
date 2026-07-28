#include "platform/SingleInstance.h"

#include <QLocalSocket>

namespace dreamdsp {

namespace {
const char *kServerName = "DreamDSP.singleton.v1";
constexpr int kTimeoutMs = 300;
} // namespace

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *client = m_server.nextPendingConnection()) {
            client->deleteLater();
            emit raiseRequested();
        }
    });
}

bool SingleInstance::acquire()
{
    QLocalSocket probe;
    probe.connectToServer(QString::fromLatin1(kServerName));
    if (probe.waitForConnected(kTimeoutMs)) {
        // Someone is home. Connecting is itself the message.
        probe.disconnectFromServer();
        return false;
    }

    // A previous run that was killed leaves a stale socket behind; on Windows
    // this is a no-op when nothing is listening, and required when there is.
    QLocalServer::removeServer(QString::fromLatin1(kServerName));
    m_server.listen(QString::fromLatin1(kServerName));
    return true;
}

} // namespace dreamdsp
