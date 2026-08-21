// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Source.h"

namespace bindpeek {

// The variable Hyprland sets for the running instance. The session detection
// in main.cpp and the socket path below both hang on it, so it is spelled once
// and read from here in both places.
inline constexpr char kHyprlandSignatureVar[] = "HYPRLAND_INSTANCE_SIGNATURE";

// The two parts of the socket path below the runtime directory. Spelled here
// rather than in the implementation because the tests build the same path to
// stand a socket up at it, and a second spelling would let the two drift.
inline constexpr char kHyprlandSocketDir[] = "hypr";
inline constexpr char kHyprlandSocketName[] = ".socket.sock";

// Where hyprctl looks when XDG_RUNTIME_DIR is unset, as it is in an ssh or su
// shell without a user session. The instance is reachable there all the same,
// so the same fallback is made rather than reporting it absent. Here for the
// same reason as the two above: the tests measure the path against it.
inline constexpr char kHyprlandRuntimeDirFallback[] = "/run/user/";

// Hyprland keeps no bind file worth reading: the configuration may pull in
// further files, define variables and build binds from them, so the text on
// disk is not the list that is actually in force. The compositor knows that
// list and hands it out over its own IPC socket, which is what hyprctl talks
// to. That answer is the source, not the configuration.
//
// Asked directly over the socket rather than by running hyprctl: the reply is
// the same, and it works on a session where the tool is not installed.
class SourceHyprland : public Source {
public:
    // Without a path the running compositor is asked. With one, that file is
    // read as a saved reply instead, which is what --source is for: the output
    // of `hyprctl -j binds` kept in a file reproduces a session that is not at
    // hand.
    explicit SourceHyprland(QString path = QString());

    QString name() const override;
    QList<Bind> read(QString *error) const override;

    // $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock, the
    // socket hyprctl itself connects to, and with hyprctl's own fallback to
    // /run/user/$UID when the runtime directory is not named. Empty only when
    // the signature is unset, which is the same as saying no Hyprland is
    // running.
    static QString socketPath();

private:
    // The saved reply to read instead of asking. Empty in normal operation.
    QString m_dumpPath;
};

} // namespace bindpeek
