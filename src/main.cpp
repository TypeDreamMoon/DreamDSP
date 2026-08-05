#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include "app/AppController.h"
#include "app/SelfTest.h"
#include "platform/ApoInstaller.h"
#include "platform/SingleInstance.h"
#include "platform/TonePlayer.h"

#include <windows.h>
#include <objbase.h>
#include <cstdio>

namespace {

// This is a WIN32-subsystem binary, so it has no console of its own. Borrow the
// launching console -- but only when stdout has not already been redirected to
// a file or pipe, because reopening CONOUT$ would steal the output back from
// that redirection.
void attachConsoleIfNeeded()
{
    const HANDLE outHandle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    const DWORD outType = (outHandle && outHandle != INVALID_HANDLE_VALUE)
                              ? ::GetFileType(outHandle)
                              : FILE_TYPE_UNKNOWN;
    if (outType == FILE_TYPE_DISK || outType == FILE_TYPE_PIPE)
        return;

    if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // HuskarUI relies on an alpha-capable surface for its acrylic/mica effects,
    // and its shaders are authored against the OpenGL backend.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QQuickWindow::setDefaultAlphaBuffer(true);

    QGuiApplication app(argc, argv);
    // Deliberately no organisation name: Qt would then place AppDataLocation at
    // %APPDATA%/DreamDSP/DreamDSP. QSettings is constructed with explicit
    // organisation/application arguments instead.
    app.setApplicationName("DreamDSP");
    app.setApplicationDisplayName("DreamDSP");
    app.setApplicationVersion(DREAMDSP_VERSION);
    // Hiding the main window into the tray must not end the process.
    app.setQuitOnLastWindowClosed(false);

    const QStringList args = app.arguments();

    // Elevated helper modes. The GUI re-runs itself with one of these through
    // ShellExecute "runas" so the user sees a single consent prompt per action
    // instead of being sent to a PowerShell script. They must be handled before
    // anything else: no window, no single-instance guard, no QML engine.
    {
        const bool doInstall = args.contains(QStringLiteral("--apo-install"));
        const bool doUninstall = args.contains(QStringLiteral("--apo-uninstall"));
        const bool doRestart = args.contains(QStringLiteral("--apo-restart-audio"));
        if (doInstall || doUninstall || doRestart) {
            attachConsoleIfNeeded();
            QString err;

            // audiodg.exe holds the staged DLL mapped for as long as the audio
            // service is running, so replacing it has to happen while the
            // service is down. Registration changes need a restart to be
            // noticed anyway, so doing the work in the gap costs nothing extra
            // -- one interruption rather than a failed copy followed by two.
            const bool bracketWithStop = doRestart && (doInstall || doUninstall);
            QStringList stoppedServices;
            if (bracketWithStop)
                err = dreamdsp::stopAudioService(&stoppedServices);

            if (err.isEmpty() && doUninstall)
                err = dreamdsp::performApoUninstall();
            if (err.isEmpty() && doInstall)
                err = dreamdsp::performApoInstall();

            if (bracketWithStop) {
                // Always bring audio back, even if the work above failed:
                // leaving the machine silent would be far worse.
                const QString startErr = dreamdsp::startAudioService(stoppedServices);
                if (err.isEmpty())
                    err = startErr;
            } else if (err.isEmpty() && doRestart) {
                err = dreamdsp::restartAudioService();
            }

            // The elevated copy is a separate, hidden process, so anything it
            // prints is lost. Leaving the outcome on disk is what lets the GUI
            // report why an install failed instead of showing an exit code --
            // and the transcript survives the status bar being overwritten by
            // the next message.
            {
                QFile log(QStringLiteral("C:/ProgramData/DreamDSP/install.log"));
                QDir().mkpath(QStringLiteral("C:/ProgramData/DreamDSP"));
                if (log.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&log);
                    out << QDateTime::currentDateTime().toString(Qt::ISODate) << "  "
                        << (doInstall ? "install " : "") << (doUninstall ? "uninstall " : "")
                        << (doRestart ? "restart" : "") << "\n"
                        << (err.isEmpty() ? QStringLiteral("ok") : err) << "\n";
                }
            }

            if (!err.isEmpty())
                std::printf("%s\n", err.toLocal8Bit().constData());
            std::fflush(stdout);
            return err.isEmpty() ? 0 : 1;
        }
    }

    const bool wantSelfTest = args.contains(QStringLiteral("--selftest"));
    const bool wantSliderTest = args.contains(QStringLiteral("--slidertest"));
    if (wantSelfTest || wantSliderTest || args.contains(QStringLiteral("--playtone"))
        || args.contains(QStringLiteral("--applyir"))) {
        attachConsoleIfNeeded();
    }

    // Diagnostic runs deliberately bypass the single-instance guard so they can
    // be used while a normal copy is running.
    const bool diagnostic = wantSelfTest || wantSliderTest
                            || args.contains(QStringLiteral("--grab"))
                            || args.contains(QStringLiteral("--applyir"));

    dreamdsp::SingleInstance instance;
    if (!diagnostic && !instance.acquire())
        return 0;   // an instance was already running and has been raised

    // The MMDevice API is used from the GUI thread only.
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comReady = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    int rc = -1;

    // --playtone <endpointId|""> [seconds] renders a tone to one endpoint.
    // A render stream is the only thing that makes the audio engine build a
    // device's effect chain, so this is how you find out whether an APO loaded.
    const int toneAt = args.indexOf(QStringLiteral("--playtone"));
    if (toneAt >= 0) {
        const QString id = (toneAt + 1 < args.size()) ? args.at(toneAt + 1) : QString();
        const double secs = (toneAt + 2 < args.size()) ? args.at(toneAt + 2).toDouble() : 3.0;
        std::printf("playing %.1f s of 440 Hz to \"%s\"\n",
                    secs, id.isEmpty() ? "(default)" : id.toLocal8Bit().constData());
        const QString err = dreamdsp::playTone(id, secs, 440.0);
        std::printf("%s\n", err.isEmpty() ? "ok" : err.toLocal8Bit().constData());
        std::fflush(stdout);
        if (comReady && hr != RPC_E_CHANGED_MODE)
            ::CoUninitialize();
        return err.isEmpty() ? 0 : 1;
    }

    if (wantSelfTest) {
        rc = dreamdsp::runSelfTest();
    } else {
        QQmlApplicationEngine engine;

        // There is an older HuskarUI installed into the Qt SDK's own qml
        // directory, which sits ahead of anything addImportPath() appends and
        // would shadow the version we build against. Prepend explicitly.
        QStringList importPaths = engine.importPathList();
        importPaths.prepend(QStringLiteral(HUSKARUI_IMPORT_PATH));
        engine.setImportPathList(importPaths);

        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                         &app, [] { QCoreApplication::exit(-1); },
                         Qt::QueuedConnection);

        engine.load(QUrl(QStringLiteral("qrc:/DreamDSP/qml/Main.qml")));
        if (!engine.rootObjects().isEmpty()) {
            QObject *root = engine.rootObjects().first();

            // Diagnostic runs have to leave the same way the tray menu does.
            // Calling QCoreApplication::quit() directly does not work: the main
            // window vetoes the close that quit() performs, and the event loop
            // simply keeps running.
            auto *controller = engine.singletonInstance<dreamdsp::AppController *>(
                "DreamDSP", "AppController");
            const auto leave = [controller] {
                if (controller)
                    controller->quitApplication();
                else
                    QCoreApplication::quit();
            };

            // A second launch asks the running copy to come forward.
            QObject::connect(&instance, &dreamdsp::SingleInstance::raiseRequested,
                             root, [root] {
                                 QMetaObject::invokeMethod(root, "restoreFromTray");
                             });

            // --view selects the section, for grabbing and slider testing alike.
            {
                const int viewAt = args.indexOf(QStringLiteral("--view"));
                if (viewAt >= 0 && viewAt + 1 < args.size())
                    root->setProperty("page", args.at(viewAt + 1).toInt());
            }

            if (wantSliderTest) {
                // Give the scene a moment to lay out before poking at it.
                QTimer::singleShot(1200, &app, [root] {
                    QCoreApplication::exit(dreamdsp::runSliderTest(root));
                });
            } else {
                // Dev utility: --grab <png> renders our own window to a file and
                // exits. Uses grabWindow() rather than a screen capture, so it
                // can only ever contain this application's own pixels.
                // --view <n> selects the equalizer view before grabbing, so both
                // the graphic and the parameter view can be captured.
                const int viewAt = args.indexOf(QStringLiteral("--view"));
                if (viewAt >= 0 && viewAt + 1 < args.size())
                    root->setProperty("page", args.at(viewAt + 1).toInt());

                // --applyir <wav> selects an impulse response and exits, so
                // the whole publish-convert-install path can be exercised
                // without driving the interface -- and so a support request
                // can be reproduced from a command line.
                const int irAt = args.indexOf(QStringLiteral("--applyir"));
                if (irAt >= 0 && irAt + 1 < args.size()) {
                    const QString irPath = args.at(irAt + 1);
                    QTimer::singleShot(300, &app, [controller, irPath, leave] {
                        if (controller) {
                            const bool ok = controller->applyImpulseFile(irPath);
                            std::printf("%s: %s\n", ok ? "applied" : "failed",
                                        controller->lastError().isEmpty()
                                            ? controller->lastMessage().toLocal8Bit().constData()
                                            : controller->lastError().toLocal8Bit().constData());
                            std::fflush(stdout);
                        }
                        leave();
                    });
                }

                // --spectrum turns the analyser on before the grab, so a
                // screenshot can show it with audio actually playing.
                if (args.contains(QStringLiteral("--spectrum"))) {
                    if (auto *ctl = engine.singletonInstance<dreamdsp::AppController *>(
                            "DreamDSP", "AppController")) {
                        ctl->setSpectrumEnabled(true);
                    }
                }

                const int grabAt = args.indexOf(QStringLiteral("--grab"));
                if (grabAt >= 0 && grabAt + 1 < args.size()) {
                    const QString target = args.at(grabAt + 1);
                    const bool wantMenu = args.contains(QStringLiteral("--traymenu"));
                    auto *win = qobject_cast<QQuickWindow *>(root);

                    QTimer::singleShot(1200, &app, [root, win, target, wantMenu, leave] {
                        if (!wantMenu) {
                            if (win)
                                win->grabWindow().save(target);
                            leave();
                            return;
                        }

                        // The tray menu is its own window; show it, then grab
                        // whichever visible window is not the main one.
                        QMetaObject::invokeMethod(root, "showTrayMenuAt",
                                                  Q_ARG(QVariant, 600), Q_ARG(QVariant, 600));
                        QTimer::singleShot(600, qApp, [win, target, leave] {
                            for (QWindow *w : QGuiApplication::allWindows()) {
                                auto *qw = qobject_cast<QQuickWindow *>(w);
                                if (qw && qw != win && qw->isVisible()) {
                                    qw->grabWindow().save(target);
                                    break;
                                }
                            }
                            leave();
                        });
                    });
                }
            }
            rc = app.exec();
            // These two lines are how the tray-quit bug was found, and they are
            // the fastest way to recognise it coming back: a process that will
            // not exit is either stuck in the event loop (neither line prints)
            // or stuck tearing down (only the first prints). Diagnostic runs
            // only, so nothing is written during normal use.
            if (diagnostic) {
                std::fprintf(stderr, "[main] exec returned %d\n", rc);
                std::fflush(stderr);
            }
        }
        if (diagnostic) {
            std::fprintf(stderr, "[main] engine destroyed\n");
            std::fflush(stderr);
        }
    }

    if (comReady && hr != RPC_E_CHANGED_MODE)
        ::CoUninitialize();
    return rc;
}
