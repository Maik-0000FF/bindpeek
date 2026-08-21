// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Measures the compositor check against the table beside it. The point of the
// check is to fail on the sessions that cannot show a layer surface without
// excluding the ones nobody has heard of yet, so both directions are tested.

#include "Compositor.h"

#include <QFile>
#include <QLocalServer>
#include <QTemporaryDir>
#include <QTest>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>

using namespace bindpeek;

// What sits at the name WAYLAND_DISPLAY carries.
//
// The check connects rather than looking for a file, so these have to be kept
// apart: only Listening answers. Dead is what a compositor killed outright
// leaves behind, the entry outliving the process that bound it, and it is the
// case a mere existence test gets wrong.
enum class Socket {
    None,
    Listening,
    Dead,
    File,
};
Q_DECLARE_METATYPE(Socket)

class TestCompositor : public QObject {
    Q_OBJECT

private slots:
    void support_data();
    void support();
    void reasonNamesTheRightCause();
    void detectionIgnoresTheDisplayString();
    void unsupportedAlwaysGivesAReason();
};

namespace {

// Binds the name and lets the descriptor go again. The entry stays on the file
// system with nothing behind it, which is exactly what a compositor that was
// killed leaves for the next reader to trip over.
bool bindWithoutListening(const QString &path) {
    const QByteArray encoded = QFile::encodeName(path);
    sockaddr_un address = {};
    if (static_cast<size_t>(encoded.size()) >= sizeof(address.sun_path)) {
        return false;
    }
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, encoded.constData(),
                static_cast<size_t>(encoded.size()));

    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    const bool bound = ::bind(fd, reinterpret_cast<const sockaddr *>(&address),
                              sizeof(address)) == 0;
    ::close(fd);
    return bound;
}

} // namespace

// The directory XDG_RUNTIME_DIR points at, holding whatever the case under
// test needs at the compositor's socket name.
class SocketDir {
public:
    bool isValid() const { return m_dir.isValid(); }
    QString path() const { return m_dir.path(); }

    // Puts `what` where the compositor socket would be.
    bool place(Socket what, const QString &name) {
        switch (what) {
        case Socket::None:
            return true;
        case Socket::Listening:
            return m_server.listen(m_dir.filePath(name));
        case Socket::Dead:
            return bindWithoutListening(m_dir.filePath(name));
        case Socket::File: {
            QFile file(m_dir.filePath(name));
            return file.open(QIODevice::WriteOnly);
        }
        }
        return false;
    }

private:
    QTemporaryDir m_dir;
    QLocalServer m_server;
};

void TestCompositor::support_data() {
    QTest::addColumn<QString>("waylandDisplay");
    QTest::addColumn<Socket>("socket");
    QTest::addColumn<QString>("sessionType");
    QTest::addColumn<QString>("desktop");
    QTest::addColumn<bool>("supported");

    // Carries the protocol.
    QTest::newRow("mango") << "wayland-1" << Socket::Listening << "wayland"
                           << "mango" << true;
    QTest::newRow("mango:wlroots") << "wayland-1" << Socket::Listening
                                   << "wayland" << "mango:wlroots" << true;
    QTest::newRow("Hyprland") << "wayland-1" << Socket::Listening << "wayland"
                              << "Hyprland" << true;
    QTest::newRow("KDE") << "wayland-0" << Socket::Listening << "wayland"
                         << "KDE" << true;
    QTest::newRow("sway") << "wayland-1" << Socket::Listening << "wayland"
                          << "sway" << true;
    QTest::newRow("niri") << "wayland-1" << Socket::Listening << "wayland"
                          << "niri" << true;
    QTest::newRow("COSMIC") << "wayland-1" << Socket::Listening << "wayland"
                            << "COSMIC" << true;
    // Unknown to us, and assumed able: an allowlist would shut out the next
    // compositor nobody has thought of.
    QTest::newRow("something new")
        << "wayland-1" << Socket::Listening << "wayland" << "brandnew" << true;
    QTest::newRow("case is ignored")
        << "" << Socket::None << "Wayland" << "KDE" << true;

    // Started by hand from a console. The login called it a tty and never
    // corrected itself, but the socket is there.
    QTest::newRow("compositor started from a tty")
        << "wayland-1" << Socket::Listening << "tty" << "mango" << true;
    QTest::newRow("no session type at all")
        << "wayland-1" << Socket::Listening << "" << "Hyprland" << true;

    // A login that says Wayland is taken at its word, socket or no socket.
    // The name can be as stale as WAYLAND_DISPLAY, but refusing here would
    // also refuse a session that is merely still coming up, and that is the
    // worse of the two mistakes.
    QTest::newRow("login says Wayland, nothing answers")
        << "wayland-1" << Socket::Dead << "wayland" << "sway" << true;

    // Does not carry the protocol.
    QTest::newRow("GNOME") << "wayland-0" << Socket::Listening << "wayland"
                           << "GNOME" << false;
    QTest::newRow("ubuntu:GNOME") << "wayland-0" << Socket::Listening
                                  << "wayland" << "ubuntu:GNOME" << false;
    QTest::newRow("gnome lower case")
        << "wayland-0" << Socket::Listening << "wayland" << "gnome" << false;
    QTest::newRow("Unity") << "wayland-0" << Socket::Listening << "wayland"
                           << "Unity" << false;
    // X11 has no layer-shell at all, whatever runs on it.
    QTest::newRow("X11 with KDE")
        << "" << Socket::None << "x11" << "KDE" << false;
    QTest::newRow("X11 alone") << "" << Socket::None << "x11" << "" << false;
    // Nothing said anywhere: no compositor to talk to.
    QTest::newRow("no session type")
        << "" << Socket::None << "" << "KDE" << false;
    QTest::newRow("nothing at all") << "" << Socket::None << "" << "" << false;
    QTest::newRow("plain tty") << "" << Socket::None << "tty" << "" << false;

    // The variable outlived its compositor: a detached terminal from a session
    // that has ended, or a user service that inherited the environment. The
    // name is set, the socket is gone, and the session really is X11.
    QTest::newRow("stale variable over X11")
        << "wayland-1" << Socket::None << "x11" << "KDE" << false;
    QTest::newRow("stale variable, no session")
        << "wayland-1" << Socket::None << "" << "" << false;

    // Killed outright, so the socket stayed where it was and nothing answers
    // on it any more. Looking for the file alone would call this a live
    // session and let the panel fail later with a platform error.
    QTest::newRow("socket of a killed compositor")
        << "wayland-1" << Socket::Dead << "tty" << "mango" << false;
    QTest::newRow("socket of a killed compositor, no session")
        << "wayland-1" << Socket::Dead << "" << "Hyprland" << false;
    // Not a socket at all, and still no reason to believe in a compositor.
    QTest::newRow("ordinary file under the socket name")
        << "wayland-1" << Socket::File << "" << "mango" << false;
}

void TestCompositor::support() {
    QFETCH(QString, waylandDisplay);
    QFETCH(Socket, socket);
    QFETCH(QString, sessionType);
    QFETCH(QString, desktop);
    QFETCH(bool, supported);

    SocketDir runtime;
    QVERIFY(runtime.isValid());
    QVERIFY(runtime.place(socket, waylandDisplay));

    const CompositorSupport result = checkCompositorSupport(
        waylandDisplay, runtime.path(), sessionType, desktop);
    QCOMPARE(result.supported, supported);
    // The session is always named, or the message would not say what was
    // detected.
    QVERIFY(!result.session.isEmpty());
}

void TestCompositor::reasonNamesTheRightCause() {
    SocketDir runtime;
    QVERIFY(runtime.isValid());
    QVERIFY(runtime.place(Socket::Listening, QStringLiteral("wayland-0")));

    const CompositorSupport x11 =
        checkCompositorSupport(QString(), runtime.path(), QStringLiteral("x11"),
                               QStringLiteral("KDE"));
    QVERIFY(x11.reason.contains(QStringLiteral("X11")));

    const CompositorSupport nothing =
        checkCompositorSupport(QString(), runtime.path(), QString(), QString());
    QVERIFY(!nothing.reason.contains(QStringLiteral("X11")));
    QVERIFY(nothing.reason.contains(QStringLiteral("Wayland")));

    const CompositorSupport gnome = checkCompositorSupport(
        QStringLiteral("wayland-0"), runtime.path(), QStringLiteral("wayland"),
        QStringLiteral("GNOME"));
    QVERIFY(gnome.reason.contains(QStringLiteral("GNOME")));
}

// The blocklist has to look at the environment variable, not at the string
// built for the message: that one is translated when nothing was set, and a
// detection that changes with the interface language is no detection.
void TestCompositor::detectionIgnoresTheDisplayString() {
    SocketDir runtime;
    QVERIFY(runtime.isValid());
    QVERIFY(runtime.place(Socket::Listening, QStringLiteral("wayland-1")));

    // An empty desktop on Wayland is unknown, not GNOME, whatever the
    // placeholder happens to read.
    QVERIFY(checkCompositorSupport(QStringLiteral("wayland-1"), runtime.path(),
                                   QStringLiteral("wayland"), QString())
                .supported);
}

void TestCompositor::unsupportedAlwaysGivesAReason() {
    // A refusal without a reason is indistinguishable from a crash.
    struct Refused {
        QString display;
        QString session;
        QString desktop;
    };
    const QList<Refused> refused = {
        {QStringLiteral("wayland-0"), QStringLiteral("wayland"),
         QStringLiteral("GNOME")},
        {QString(), QStringLiteral("x11"), QStringLiteral("KDE")},
        {QString(), QString(), QString()},
    };
    SocketDir runtime;
    QVERIFY(runtime.isValid());
    QVERIFY(runtime.place(Socket::Listening, QStringLiteral("wayland-0")));
    for (const Refused &entry : refused) {
        const CompositorSupport result = checkCompositorSupport(
            entry.display, runtime.path(), entry.session, entry.desktop);
        QVERIFY(!result.supported);
        QVERIFY2(!result.reason.isEmpty(),
                 qPrintable(entry.session + entry.desktop));
    }

    // And a supported session never carries one.
    QVERIFY(checkCompositorSupport(QStringLiteral("wayland-0"), runtime.path(),
                                   QStringLiteral("wayland"),
                                   QStringLiteral("mango"))
                .reason.isEmpty());
}

// Not APPLESS: QLocalServer and QLocalSocket both create socket notifiers, and
// those need an application object with an event dispatcher behind them.
QTEST_GUILESS_MAIN(TestCompositor)
#include "test_compositor.moc"
