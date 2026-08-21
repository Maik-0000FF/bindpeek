// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AppInfo.h"
#include "Appearance.h"
#include "OverlayProcess.h"
#include "Settings.h"
#include "SettingsModel.h"
#include "SystemScheme.h"
#include "TrayWait.h"

#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLocale>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTranslator>

using namespace bindpeek;

namespace {

// Names QML addresses the C++ objects by.
constexpr char kModelName[] = "SettingsModel";
constexpr char kAppearanceName[] = "Appearance";
constexpr char kAppInfoName[] = "AppInfo";
// Not "Overlay": QtQuick.Controls already defines a type of that name, which
// would shadow the context property and leave every binding on it empty,
// silently. qmllint catches exactly this.
constexpr char kOverlayName[] = "OverlayControl";

constexpr char kEditorQml[] = "qrc:/qml/editor/Editor.qml";
// The mark on its own: at tray size a wordmark is a smudge. Two inks, picked
// by the desktop scheme, because a tray panel is dark on one desktop and
// light on the next and Qt cannot recolour an icon by itself.
constexpr char kTrayIconLight[] = ":/icons/logo-mark-black.svg";
constexpr char kTrayIconDark[] = ":/icons/logo-mark-white.svg";
constexpr char kTranslationPrefix[] = "bindpeek";
void installTranslators() {
    static QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"),
                          QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(&qtTranslator);
    }
    static QTranslator appTranslator;
    if (appTranslator.load(QLocale(), QLatin1String(kTranslationPrefix),
                           QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(&appTranslator);
    }
}
} // namespace

namespace {

// How long the check below waits for the other instance to answer. A socket on
// the same machine either answers at once or is not there; the wait only
// covers the moment the kernel needs to complete the handshake.
constexpr int kHandoverTimeoutMs = 200;

// How long a tray is waited for before the settings window stands in for it.
//
// Started from a compositor's autostart, this program and the panel that owns
// the tray come up in the same breath, and the panel needs a moment longer to
// say it is there. Asked once, in that moment, the answer is "no tray" on
// every single login and the window opens by itself, which is the one thing
// having a tray is meant to prevent. Waiting costs nothing but the wait: Qt
// hands the icon to the watcher as soon as that appears, whether it does so
// inside this budget or long after it.
constexpr int kTrayWaitMs = 10000;
// How often the wait looks again. Nothing announces a tray that arrives late,
// so the question is simply put again, and it is answered afresh each time
// rather than from something Qt remembered.
constexpr int kTrayPollMs = 500;
// How long before the wait says out loud that it is waiting.
//
// Not on the first look. On the session this is built for the tray is a moment
// behind this program, so a line printed straight away would go into the
// journal at every single login, about a state that is over before anybody
// could read it. Long enough to sit that moment out, short enough that a
// session which really has no tray is not left without a word.
constexpr int kTraySpeakUpMs = 2000;

// Milliseconds to the second, for the one place the wait is said out loud in
// the unit a person reads it in.
constexpr int kMsPerSecond = 1000;

// Hands the request over to a settings window that is already running, and
// says whether that worked.
//
// A second start is not a mistake to be refused. Someone has reached for the
// launcher, and what they want is the settings; the instance that owns them
// shows its window and this one is done.
bool handOverToRunningEditor() {
    QLocalSocket socket;
    socket.connectToServer(AppInfo::editorSocketPath());
    if (!socket.waitForConnected(kHandoverTimeoutMs)) {
        return false;
    }
    // The connection is the whole message: there is one thing to ask for.
    socket.write("\n");
    socket.waitForBytesWritten(kHandoverTimeoutMs);
    return true;
}

} // namespace

int main(int argc, char **argv) {
    // Same reason as in the overlay: the Nix store stamps every file with the
    // epoch, so Qt's on-disk QML cache never notices a rebuild.
    qputenv("QML_DISABLE_DISK_CACHE", "1");
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("bindpeek-editor"));
    // The application id a compositor sees. Without it the id would follow the
    // name above, and "bindpeek-editor" matches no installed entry: the
    // settings window would show a placeholder icon in the task bar and never
    // group under the launcher it was started from. The overlay needs no such
    // line, its name already is the id.
    QGuiApplication::setDesktopFileName(QStringLiteral(BINDPEEK_DESKTOP_ID));
    QCoreApplication::setApplicationVersion(QStringLiteral(BINDPEEK_VERSION));

    // A settings window that is already running takes this request over and
    // this start is done. Two tray icons and two windows for one program are
    // not two instances of anything useful, they are one program that looks
    // broken.
    //
    // Asked after the application object rather than before it. Measured, it
    // works either way, but a socket that waits belongs to an event
    // dispatcher and there is none before the application exists; the earlier
    // place would be leaning on something Qt does not promise. What it costs
    // is one application object for a start that ends a line later.
    if (handOverToRunningEditor()) {
        return 0;
    }
    // Closing the window leaves the tray icon behind, which is the point of
    // having one; without this the process would end with the last window.
    QApplication::setQuitOnLastWindowClosed(false);
    installTranslators();
    SettingsModel model;
    OverlayProcess overlay;
    // The panel comes up with the program. It is what bindpeek is for, and a
    // tray icon that has to be found and clicked before anything happens
    // looks like a program that did not start. Switched off once, it stays
    // off: the setting outlives the session and is asked here.
    if (model.overlayEnabled()) {
        overlay.start();
    }
    // One reader for the desktop light/dark setting, shared by the tray icon
    // and by the preview.
    SystemScheme systemScheme;
    AppInfo appInfo;
    // The preview is driven by the very class the overlay uses, so the two
    // cannot show different things.
    Appearance appearance{Settings(), &systemScheme};
    QObject::connect(
        &model, &SettingsModel::changed, &appearance,
        [&model, &appearance]() { appearance.apply(model.current()); });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QLatin1String(kModelName), &model);
    engine.rootContext()->setContextProperty(QLatin1String(kAppearanceName),
                                             &appearance);
    engine.rootContext()->setContextProperty(QLatin1String(kOverlayName),
                                             &overlay);
    engine.rootContext()->setContextProperty(QLatin1String(kAppInfoName),
                                             &appInfo);
    engine.load(QUrl(QLatin1String(kEditorQml)));
    if (engine.rootObjects().isEmpty()) {
        QTextStream(stderr)
            << QCoreApplication::translate(
                   "main", "The settings window could not be loaded.")
            << Qt::endl;
        return 1;
    }

    auto *window =
        qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    if (window == nullptr) {
        QTextStream(stderr)
            << QCoreApplication::translate(
                   "main", "The settings window could not be loaded.")
            << Qt::endl;
        return 1;
    }

    // --- Tray ------------------------------------------------------------
    QSystemTrayIcon tray;
    // A dark panel needs the light mark and the other way round. The scheme
    // comes from SystemScheme, not from QStyleHints: on a plain wlroots
    // session Qt reports nothing and the icon would always come out dark.
    // Re-evaluated on a switch so it does not vanish into its own background
    // halfway through a session.
    const auto applyTrayIcon = [&tray, &systemScheme]() {
        const bool darkPanel =
            systemScheme.scheme() == SystemScheme::Scheme::Dark;
        tray.setIcon(
            QIcon(QLatin1String(darkPanel ? kTrayIconDark : kTrayIconLight)));
    };
    applyTrayIcon();
    QObject::connect(&systemScheme, &SystemScheme::schemeChanged, &tray,
                     applyTrayIcon);
    QMenu menu;
    // Not a checkable entry.
    //
    // A tick in a tray menu travels over DBusMenu, and the implementations
    // differ in whether they draw it at all and whether they redraw it when it
    // changes; where they do, every other entry is indented past a column that
    // only this one uses. The entry says what the click will do instead, which
    // is a plain label and arrives everywhere.
    auto *toggleAction = menu.addAction(QString());

    // The state of the entry, re-derived rather than set once.
    //
    // Two things can change under it: the panel can start or stop, and the
    // editor may have been started somewhere that knows nothing of the
    // compositor, a user service or a detached shell, while a panel is plainly
    // running. Deciding this once at startup would leave a greyed-out entry
    // that cannot stop a panel everyone can see.
    //
    // The reason goes into the entry's own text, not a tooltip: a tray menu is
    // exported over DBusMenu on the desktops where this matters, and that
    // protocol carries no per-item tooltip. The tray icon keeps it as well,
    // which is the one place a tooltip does arrive.
    const auto applyTrayState = [&overlay, toggleAction, &tray]() {
        const bool running = overlay.isRunning();
        const bool usable = overlay.isUsable();

        toggleAction->setEnabled(usable);
        toggleAction->setText(
            !usable ? overlay.unsupportedReason()
            : running
                ? QCoreApplication::translate("main", "Switch the overlay off")
                : QCoreApplication::translate("main", "Switch the overlay on"));
        tray.setToolTip(usable ? QCoreApplication::translate("main", "bindpeek")
                               : overlay.unsupportedReason());
    };
    applyTrayState();
    QObject::connect(&overlay, &OverlayProcess::runningChanged, toggleAction,
                     applyTrayState);

    // What the entry then says is not decided here. A start fails when the
    // input group is missing or another instance holds the lock, and a label
    // written on the click would be a promise the click cannot keep: at that
    // moment neither the start nor the stop has taken effect. The settle check
    // reports what is actually true and writes the label from that.
    //
    // What the click itself does, and in which order, is requestToggle().
    QObject::connect(toggleAction, &QAction::triggered, &overlay,
                     [&overlay, &model]() { overlay.requestToggle(&model); });
    auto *settingsAction =
        menu.addAction(QCoreApplication::translate("main", "Settings..."));
    QObject::connect(settingsAction, &QAction::triggered, window, [window]() {
        window->show();
        window->raise();
        window->requestActivate();
    });

    menu.addSeparator();
    auto *quitAction =
        menu.addAction(QCoreApplication::translate("main", "Quit"));
    // Quitting ends the panel as well, and the two are not the same thing:
    // the panel is its own process and would otherwise stay on the keyboard
    // with nothing left to switch it off from. What quitting means here is
    // that bindpeek is done until it is started again.
    //
    // The setting is left alone. Whether the panel is wanted is answered by
    // the switch above it, and ending the program is not an answer to that
    // question; the next start brings the panel back with it.
    QObject::connect(quitAction, &QAction::triggered, &app, [&overlay]() {
        overlay.stop();
        QApplication::quit();
    });
    tray.setContextMenu(&menu);
    // A left click on the icon opens the settings, which is what people try
    // first; the menu stays on the right button.
    QObject::connect(&tray, &QSystemTrayIcon::activated, window,
                     [window](QSystemTrayIcon::ActivationReason reason) {
                         if (reason != QSystemTrayIcon::Trigger) {
                             return;
                         }
                         if (window->isVisible()) {
                             window->hide();
                         } else {
                             window->show();
                             window->raise();
                             window->requestActivate();
                         }
                     });
    // A second start of the settings window is answered by the first.
    //
    // Listening rather than locking: the socket both marks the instance that
    // owns the settings and carries the request to it.
    QLocalServer settingsServer;
    if (!settingsServer.listen(AppInfo::editorSocketPath()) &&
        settingsServer.serverError() == QAbstractSocket::AddressInUseError) {
        // The name is taken. Either somebody is listening on it or a process
        // was killed outright and left the file behind, and only the second
        // is a file to remove.
        //
        // Asked again here rather than trusting the ask at the top of main():
        // that one is a moment old, and in that moment another start may have
        // taken the name. Removing a socket somebody is listening on would
        // take the settings away from the window that owns them and leave two
        // running, which is the very thing this is here to prevent.
        if (handOverToRunningEditor()) {
            return 0;
        }
        QLocalServer::removeServer(AppInfo::editorSocketPath());
        settingsServer.listen(AppInfo::editorSocketPath());
    }
    if (!settingsServer.isListening()) {
        // Not a reason to stop: the settings themselves work. Only a later
        // start will build a window of its own instead of raising this one,
        // and that is worth saying out loud rather than leaving to be found.
        QTextStream(stderr)
            << QCoreApplication::translate(
                   "main", "A second start cannot be handed to this window: %1")
                   .arg(settingsServer.errorString())
            << Qt::endl;
    }
    QObject::connect(&settingsServer, &QLocalServer::newConnection, window,
                     [&settingsServer, window]() {
                         // The connection is the message; nothing is read.
                         QLocalSocket *caller =
                             settingsServer.nextPendingConnection();
                         if (caller != nullptr) {
                             caller->deleteLater();
                         }
                         window->show();
                         window->raise();
                         window->requestActivate();
                     });

    tray.show();
    // Without a tray the window would wait for an icon that never appears and
    // the editor could not be opened at all. A tray that is not there yet
    // looks exactly like one that never comes, so the two are told apart by
    // waiting rather than by this first answer.
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        const auto trayIsThere = []() {
            return QSystemTrayIcon::isSystemTrayAvailable();
        };

        // Two waits on the same question, and each answers a different half of
        // it: the short one only speaks, the long one stands in.
        //
        // Speaking at all is for the start by hand on a session that carries
        // no tray. Without it this program shows nothing whatsoever until the
        // long wait is up: no icon, no window, not a word, which reads like
        // one that failed to come up. Speaking late is so that the ordinary
        // login, where the tray is merely a moment behind, says nothing at
        // all.
        waitForTray(
            window, trayIsThere,
            []() {
                QTextStream(stderr)
                    << QCoreApplication::translate(
                           "main",
                           "Still no system tray. The settings window opens by "
                           "itself if none appears within %1 seconds.")
                           // What is left of the wait, not the whole of it.
                           // This is read at the moment it is printed, and by
                           // then the short wait above has already been sat
                           // out.
                           .arg((kTrayWaitMs - kTraySpeakUpMs) / kMsPerSecond)
                    << Qt::endl;
            },
            kTraySpeakUpMs, kTrayPollMs);

        waitForTray(
            window, trayIsThere,
            [window]() {
                // Said out loud. A window that opens by itself a while after
                // the start is otherwise indistinguishable from a defect, and
                // this is the only place that knows the reason for it.
                QTextStream(stderr)
                    << QCoreApplication::translate(
                           "main", "No system tray was found, so the settings "
                                   "window is opened instead.")
                    << Qt::endl;
                window->show();
            },
            kTrayWaitMs, kTrayPollMs);
    }

    return QApplication::exec();
}
