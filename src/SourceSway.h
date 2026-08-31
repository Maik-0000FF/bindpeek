// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Source.h"

namespace bindpeek {

// The variable sway puts its socket path in, and the one it also sets for
// compatibility with i3. The session detection in main.cpp and the socket
// path below both hang on them, so they are spelled once and read from here.
//
// Either one names a running compositor: sway sets both, i3 only the second,
// and a session that has neither is not one this backend can ask.
inline constexpr char kSwaySocketVar[] = "SWAYSOCK";
inline constexpr char kSwaySocketVarLegacy[] = "I3SOCK";

// sway hands its configuration out over the same socket its own client talks
// to, and hands out what it actually loaded rather than what is on disk: the
// file may pull in further files and build its binds from variables, so the
// text on disk is not the list in force. That answer is the source.
//
// Asked directly over the socket rather than by running swaymsg: the reply is
// the same, and it works on a session where the tool is not installed.
class SourceSway : public Source {
public:
    // Without a path the running compositor is asked. With one, that file is
    // read as configuration text instead, which is what --source is for: a
    // configuration kept in a file reproduces a session that is not at hand.
    explicit SourceSway(QString path = QString());

    QString name() const override;
    QList<Bind> read(QString *error) const override;

    // The socket the compositor listens on, from SWAYSOCK or I3SOCK. Empty
    // when neither is set, which is the same as saying no sway is running.
    static QString socketPath();

    // The binds in a configuration text, as read() would hand them out.
    //
    // Exposed because this is the whole of the backend that can be measured
    // without a compositor: everything else is the socket around it. What
    // goes in is what sway answers with, and the tests put the same text in
    // by hand.
    //
    // *note collects what had to be skipped, one sentence per reason.
    static QList<Bind> parseConfig(const QString &text, QString *note);

private:
    // The configuration to read instead of asking. Empty in normal operation.
    QString m_configPath;
};

} // namespace bindpeek
