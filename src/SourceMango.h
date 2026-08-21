// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Source.h"

#include <QStringList>

#include <functional>

namespace bindpeek {

// The variable a running mango sets. Its value is the path of the socket, and
// the socket carries the compositor's process id in its name. Spelled here
// rather than where it is read, because the environment is asked for it in two
// places: once to recognize the session and once to find the configuration.
inline constexpr char kMangoSignatureVar[] = "MANGO_INSTANCE_SIGNATURE";

// The two files mango looks in when it was started without one of its own,
// each under its own name so nothing has to remember which came first.
struct ConfigCandidates {
    // The one under the home directory, which mango reads if it is there.
    QString ownFile;
    // The one its package ships, which it falls back to if it is not.
    QString packagedFile;
};

// Which of the two files mango reads, given a way to ask whether a file is
// there.
//
// Its own before the one its package ships, which is the order mango looks in.
// Where neither is there the one in the home directory is named: a session
// that has no configuration at all is a session somebody is about to write
// one for, and that is where they would write it.
//
// Takes the pair under their names and a question to ask about them, rather
// than four loose arguments and the filesystem. Both of those were a way to
// get it wrong in silence: two strings and two flags can be handed over in
// the wrong order and still compile, and a rule that asks the disk itself can
// only be measured on a machine that happens to have the right files on it.
QString chosenConfig(const ConfigCandidates &candidates,
                     const std::function<bool(const QString &)> &isThere);

// mango offers no query for its binds, so the configuration mango itself reads
// is the source. It is the single source of truth and small.
//
// Which files those are is not assumed but worked out the way mango works it
// out: the running compositor is asked what it was started with, and the file
// it names is followed through its own "source=" lines. A configuration split
// across several files, or one kept under a name of its own, is therefore
// found without anything having to be configured here.
class SourceMango : public Source {
public:
    // Without a path the running compositor decides; see configPath().
    explicit SourceMango(QString path = QString());

    QString name() const override;
    QList<Bind> read(QString *error) const override;

    // The configuration file mango reads.
    //
    // Its own "-c" argument when it was started with one, taken from the
    // command line of the running process, and otherwise the place mango
    // falls back to. Read afresh on every call rather than remembered: a
    // compositor can be restarted with another file under a panel that keeps
    // running, and a remembered answer would then be the wrong one for the
    // rest of the session.
    static QString configPath();

    // Where those two files are, without asking whether they exist.
    //
    // Named separately from the choice between them: the rule is one thing
    // and which path is which is another, and only the second can be got
    // wrong in a way that reads the whole machine's configuration out of the
    // wrong file while every test of the rule still passes.
    static ConfigCandidates configCandidates();

    // Every file that configuration draws on, the file itself first and the
    // ones it pulls in with "source=" after it, each of them once.
    //
    // The order is the order mango reads them in, which is what decides under
    // which heading a bind ends up when two files use the same one.
    static QStringList sourceFiles(const QString &start);

private:
    QString m_path;
};

} // namespace bindpeek
