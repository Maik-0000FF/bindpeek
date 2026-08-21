// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace bindpeek {

// The variable naming the per-user runtime directory. The socket check here
// and the Hyprland backend both build paths under it, so it is spelled once
// and read from here in both places.
inline constexpr char kRuntimeDirVar[] = "XDG_RUNTIME_DIR";

// Can this session host the panel at all?
//
// The panel is a wlr-layer-shell surface. Where that protocol is missing, the
// window does not fail to appear, it appears as an ordinary application window
// with a title bar, in the middle of the screen, stealing focus. That looks
// like a broken program rather than an unsupported one, so it is better to say
// so and stop.
struct CompositorSupport {
    bool supported = false;
    // What was detected, for the message. Never empty.
    QString session;
    // Empty when supported. Otherwise one sentence on why not.
    QString reason;

    // The two parts as one line, for wherever the refusal is shown. Built here
    // so the overlay and the tray cannot phrase it differently, and so it goes
    // through the catalogue in both places.
    QString message() const;
};

// Decides from what the session says about itself.
//
// waylandDisplay is the deciding one: WAYLAND_DISPLAY is set by the compositor
// itself. XDG_SESSION_TYPE comes from the login and says "tty" for anyone who
// starts their compositor by hand from a console, which is a perfectly
// ordinary way to run one.
//
// runtimeDir is where the socket named by waylandDisplay would be. Both are
// needed because the variable outlives its compositor in an inherited
// environment: a user service, or a detached terminal from a session that has
// ended. Connecting tells a live socket from the one a compositor killed
// outright leaves behind, which is more than the name alone says.
//
// A session type of "wayland" counts on its own, with no socket to show for
// it. That name is inherited by the same means and can be just as stale, but a
// login naming Wayland is taken at its word rather than second-guessed: a
// compositor still coming up would otherwise be turned away.
//
// All three may be empty, which is treated as no session at all.
//
// A blocklist, not an allowlist: every wlroots-based compositor carries the
// protocol, and so do Hyprland, KWin and niri, each built on something of its
// own. Naming them all would silently exclude the next one, so only the known
// holdouts are listed.
CompositorSupport checkCompositorSupport(const QString &waylandDisplay,
                                         const QString &runtimeDir,
                                         const QString &sessionType,
                                         const QString &currentDesktop);

// The same check against the current environment.
CompositorSupport detectCompositorSupport();

} // namespace bindpeek
