#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStringList>
#include <QTimer>

#include "app/AppController.h"
#include "app/SelfTest.h"
#include "platform/SingleInstance.h"

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
    const bool wantSelfTest = args.contains(QStringLiteral("--selftest"));
    const bool wantSliderTest = args.contains(QStringLiteral("--slidertest"));
    if (wantSelfTest || wantSliderTest)
        attachConsoleIfNeeded();

    // Diagnostic runs deliberately bypass the single-instance guard so they can
    // be used while a normal copy is running.
    const bool diagnostic = wantSelfTest || wantSliderTest
                            || args.contains(QStringLiteral("--grab"));

    dreamdsp::SingleInstance instance;
    if (!diagnostic && !instance.acquire())
        return 0;   // an instance was already running and has been raised

    // The MMDevice API is used from the GUI thread only.
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comReady = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    int rc = -1;

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

                    QTimer::singleShot(1200, &app, [root, win, target, wantMenu] {
                        if (!wantMenu) {
                            if (win)
                                win->grabWindow().save(target);
                            QCoreApplication::quit();
                            return;
                        }

                        // The tray menu is its own window; show it, then grab
                        // whichever visible window is not the main one.
                        QMetaObject::invokeMethod(root, "showTrayMenuAt",
                                                  Q_ARG(QVariant, 600), Q_ARG(QVariant, 600));
                        QTimer::singleShot(600, qApp, [win, target] {
                            for (QWindow *w : QGuiApplication::allWindows()) {
                                auto *qw = qobject_cast<QQuickWindow *>(w);
                                if (qw && qw != win && qw->isVisible()) {
                                    qw->grabWindow().save(target);
                                    break;
                                }
                            }
                            QCoreApplication::quit();
                        });
                    });
                }
            }
            rc = app.exec();
        }
    }

    if (comReady && hr != RPC_E_CHANGED_MODE)
        ::CoUninitialize();
    return rc;
}
