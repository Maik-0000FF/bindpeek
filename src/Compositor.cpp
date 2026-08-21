// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Compositor.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QProcessEnvironment>

namespace bindpeek {
namespace {

constexpr char kVarWaylandDisplay[] = "WAYLAND_DISPLAY";
// Long enough for a socket on the same machine, short enough that a start on a
// session without one is not noticeably delayed.
constexpr int kConnectTimeoutMs = 200;

constexpr char kVarSessionType[] = "XDG_SESSION_TYPE";
constexpr char kVarCurrentDesktop[] = "XDG_CURRENT_DESKTOP";

constexpr char kWayland[] = "wayland";
constexpr char kX11[] = "x11";

// The compositors that run on Wayland and still do not implement
// wlr-layer-shell. GNOME's Mutter refuses it upstream, and Unity builds on it.
constexpr char kGnome[] = "GNOME";
constexpr char kUnity[] = "Unity";

// Is something actually accepting connections on the compositor socket?
//
// The mere presence of the file proves nothing: a compositor killed outright
// leaves its socket behind, and libwayland keeps a separate lock file for
// exactly that reason. Connecting is the shortest way to tell a live socket
// from a leftover, and the connection is dropped again immediately.
//
// An absolute name is taken as given; that is what the protocol allows and it
// needs no runtime directory.
bool compositorIsListening(const QString &waylandDisplay,
                           const QString &runtimeDir) {
    if (waylandDisplay.isEmpty()) {
        return false;
    }
    const bool absolute = waylandDisplay.startsWith(QLatin1Char('/'));
    if (!absolute && runtimeDir.isEmpty()) {
        return false;
    }
    const QString path = absolute
                             ? waylandDisplay
                             : runtimeDir + QLatin1Char('/') + waylandDisplay;

    QLocalSocket socket;
    socket.connectToServer(path);
    // A local socket either answers at once or is not there; the wait only
    // covers the moment the kernel needs to complete the handshake.
    const bool connected = socket.waitForConnected(kConnectTimeoutMs);
    socket.abort();
    return connected;
}

} // namespace

QString CompositorSupport::message() const {
    if (supported) {
        return {};
    }
    return QCoreApplication::translate("Compositor", "%1: %2")
        .arg(session, reason);
}

CompositorSupport checkCompositorSupport(const QString &waylandDisplay,
                                         const QString &runtimeDir,
                                         const QString &sessionType,
                                         const QString &currentDesktop) {
    CompositorSupport support;

    const QString desktop =
        currentDesktop.isEmpty()
            ? QCoreApplication::translate("Compositor", "unknown desktop")
            : currentDesktop;
    // Something has to answer on the socket, not merely be named in the
    // environment: WAYLAND_DISPLAY is inherited by user services and detached
    // terminals, and a compositor killed outright leaves its socket behind
    // either way. Without this the panel would fail later with a platform
    // error instead of the plain sentence below.
    const bool listening = compositorIsListening(waylandDisplay, runtimeDir);

    // The session type is trusted on its own, stale or not. It is all a
    // compositor that has yet to open its socket has to offer, and turning
    // away a session that is still coming up is the worse mistake.
    const bool isWayland =
        listening ||
        sessionType.compare(QLatin1String(kWayland), Qt::CaseInsensitive) == 0;
    const bool isX11 =
        !isWayland &&
        sessionType.compare(QLatin1String(kX11), Qt::CaseInsensitive) == 0;

    support.session = isWayland ? QStringLiteral("%1 (Wayland)").arg(desktop)
                      : isX11   ? QStringLiteral("%1 (X11)").arg(desktop)
                                : desktop;

    if (isX11) {
        support.reason = QCoreApplication::translate(
            "Compositor", "wlr-layer-shell is a Wayland protocol. An X11 "
                          "session cannot show the panel.");
        return support;
    }

    if (!isWayland) {
        // Said separately from the X11 case: naming X11 here would send the
        // reader looking at a session that was never established.
        support.reason = QCoreApplication::translate(
            "Compositor", "No Wayland session detected. wlr-layer-shell is a "
                          "Wayland protocol.");
        return support;
    }

    // Matched against the raw value, never against `desktop`: that one carries
    // a translated placeholder when the variable is unset, so the detection
    // would depend on the interface language.
    if (currentDesktop.contains(QLatin1String(kGnome), Qt::CaseInsensitive) ||
        currentDesktop.contains(QLatin1String(kUnity), Qt::CaseInsensitive)) {
        support.reason = QCoreApplication::translate(
            "Compositor", "GNOME/Mutter does not implement wlr-layer-shell. "
                          "The panel would appear as an "
                          "ordinary window and take the focus.");
        return support;
    }

    // Everything else is assumed to carry the protocol. Every wlroots-based
    // compositor does, and so do Hyprland, KWin and niri, which are built on
    // something of their own. A rare one that does not would be reported as
    // supported here and fail visibly at startup instead, which is still
    // better than excluding compositors nobody thought to name.
    support.supported = true;
    return support;
}

CompositorSupport detectCompositorSupport() {
    const QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
    return checkCompositorSupport(
        environment.value(QLatin1String(kVarWaylandDisplay)),
        environment.value(QLatin1String(kRuntimeDirVar)),
        environment.value(QLatin1String(kVarSessionType)),
        environment.value(QLatin1String(kVarCurrentDesktop)));
}

} // namespace bindpeek
