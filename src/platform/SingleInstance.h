#pragma once

#include <QLocalServer>
#include <QObject>

namespace dreamdsp {

// Keeps one DreamDSP per user session.
//
// With a tray icon and global hotkeys, a second copy is actively harmful: two
// icons, two claims on the same hotkeys, and two writers racing on
// dreamdsp.txt. A second launch hands its request to the running instance and
// exits.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);

    // True when this process took ownership. False means another instance is
    // already running and has been told to show itself -- the caller should
    // exit immediately.
    bool acquire();

signals:
    void raiseRequested();

private:
    QLocalServer m_server;
};

} // namespace dreamdsp
