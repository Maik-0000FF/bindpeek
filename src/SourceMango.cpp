// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SourceMango.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace bindpeek {
namespace {

// Line prefix that pulls another file into the configuration.
constexpr char kPrefixSource[] = "source=";

// The two files mango looks in when it was started without one of its own,
// in the order it looks.
//
// Assembled here rather than asked of QStandardPaths, which is the opposite
// of how the desktop's own file is found. That is deliberate, and measured
// rather than assumed: the compositor's own binary carries both formats,
// "%s/.config/mango/config.conf" filled in from the home directory and
// "%s/mango/config.conf" filled in from the directory it was built to keep
// system configuration in, which is /etc. XDG_CONFIG_HOME appears nowhere in
// it, so a session that moves its configuration by that variable moves the
// compositor's file nowhere, and following it here would look up a file mango
// does not read.
//
// The second is not a formality: a session that never wrote a configuration
// of its own runs entirely off the file its package ships, and a panel that
// only knew the first would report that file as missing while every shortcut
// on the machine works. Should a later version of mango learn the variable,
// or be built with another prefix, these are the lines that have to learn it
// too.
constexpr char kDefaultConfig[] = "/.config/mango/config.conf";
constexpr char kSystemConfig[] = "/etc/mango/config.conf";

// The option mango takes its configuration file with.
constexpr char kConfigOption[] = "-c";

// How far the "source=" chain is followed.
//
// A configuration split over a handful of files is ordinary; a hundred of them
// is a loop that names a new file every turn. The count is a backstop, not a
// budget: the visited set below already stops a file from being read twice, so
// nothing sensible comes near this.
constexpr int kMaxSourceFiles = 64;

// Where the kernel exposes the running processes, and the file that holds the
// command line of one, its arguments separated by zero bytes.
constexpr char kProcDir[] = "/proc";

// The link that names the directory a process is running in.
constexpr char kCwdLink[] = "cwd";
constexpr char kCmdlineFile[] = "cmdline";

// The process id of the running compositor, or 0.
//
// Read out of the socket path the session variable carries, which ends in the
// id: "/run/user/1000/mango-2021.sock". Not looked up by process name, which
// is not a reliable way to find a process at all: a package that installs a
// program as a wrapper around the real binary leaves that binary's name in
// /proc, cut to fifteen characters.
qint64 compositorPid() {
    const QString signature = QProcessEnvironment::systemEnvironment().value(
        QLatin1String(kMangoSignatureVar));
    const QString name = QFileInfo(signature).completeBaseName();
    const qsizetype dash = name.lastIndexOf(QLatin1Char('-'));
    if (dash < 0) {
        return 0;
    }
    bool ok = false;
    const qint64 pid = QStringView(name).mid(dash + 1).toLongLong(&ok);
    return ok ? pid : 0;
}

// The file named after "-c" on a command line, empty when there is none.
//
// Both spellings are accepted because the option is read with getopt, which
// takes the value as the next argument or glued to the letter.
QString configArgument(const QStringList &arguments) {
    const QString option = QLatin1String(kConfigOption);
    for (qsizetype at = 0; at < arguments.size(); ++at) {
        const QString &argument = arguments.at(at);
        if (argument == option) {
            return (at + 1 < arguments.size()) ? arguments.at(at + 1)
                                               : QString();
        }
        if (argument.startsWith(option) && argument.size() > option.size()) {
            return argument.mid(option.size());
        }
    }
    return {};
}

// The directory a process was started in, or nothing when it cannot be read.
//
// The link under /proc answers it. Read for one reason only: an argument on
// somebody else's command line is relative to their directory, never to ours.
QString workingDirectoryOf(qint64 pid) {
    return QFileInfo(QStringLiteral("%1/%2/%3")
                         .arg(QLatin1String(kProcDir))
                         .arg(pid)
                         .arg(QLatin1String(kCwdLink)))
        .symLinkTarget();
}

// The command line of one process, its arguments already split.
QStringList commandLineOf(qint64 pid) {
    QFile cmdline(QStringLiteral("%1/%2/%3")
                      .arg(QLatin1String(kProcDir))
                      .arg(pid)
                      .arg(QLatin1String(kCmdlineFile)));
    if (!cmdline.open(QIODevice::ReadOnly)) {
        // Another user's process, or one that has ended. Neither is an error
        // here: the caller falls back to where mango looks by default.
        return {};
    }
    const QByteArray raw = cmdline.readAll();
    QStringList arguments;
    for (const QByteArray &argument : raw.split('\0')) {
        if (!argument.isEmpty()) {
            arguments.append(QString::fromLocal8Bit(argument));
        }
    }
    return arguments;
}

// A path as written in a configuration file, made absolute.
//
// "~" is the home directory, as every shell and mango itself read it. A
// relative path is taken from the directory of the file that named it, which
// is the only reading under which a configuration can be moved as a whole.
QString resolvePath(const QString &raw, const QString &relativeTo) {
    QString path = raw.trimmed();
    if (path.isEmpty()) {
        return {};
    }
    if (path.startsWith(QLatin1Char('~'))) {
        return QDir::homePath() + path.mid(1);
    }
    if (QDir::isAbsolutePath(path)) {
        return path;
    }
    return QDir(relativeTo).absoluteFilePath(path);
}

// Line prefix that introduces a keyboard shortcut. mango also knows
// "mousebind=" and "axisbind="; neither is a keyboard shortcut and neither
// belongs on a keyboard cheat sheet.
constexpr char kPrefixBind[] = "bind=";

// Section comment of the form "# --- Programs ---". Only this shape is a group
// heading; every other comment line is a note and leaves the group alone.
const QRegularExpression &sectionPattern() {
    static const QRegularExpression pattern(
        QStringLiteral("^#\\s*-{2,}\\s*(.+?)\\s*-{2,}\\s*$"));
    return pattern;
}

// Directions as mango spells them in PARAMS.
QString directionText(const QString &raw) {
    static const QHash<QString, const char *> table = {
        {QStringLiteral("left"), QT_TRANSLATE_NOOP("SourceMango", "left")},
        {QStringLiteral("right"), QT_TRANSLATE_NOOP("SourceMango", "right")},
        {QStringLiteral("up"), QT_TRANSLATE_NOOP("SourceMango", "up")},
        {QStringLiteral("down"), QT_TRANSLATE_NOOP("SourceMango", "down")},
    };
    const char *entry = table.value(raw, nullptr);
    return (entry == nullptr)
               ? raw
               : QCoreApplication::translate("SourceMango", entry);
}

// Description per ACTION. "%1" is replaced by the prepared PARAMS. An ACTION
// that is missing from the table is shown raw together with its PARAMS, so a
// newly added shortcut never goes missing silently.
//
// Looked up as written, which is right here: mango compares an action name
// exactly and never folds its case, so a name spelled differently runs there
// no more than it is described here.
const QHash<QString, const char *> &actionTexts() {
    static const QHash<QString, const char *> table = {
        {QStringLiteral("spawn"), QT_TRANSLATE_NOOP("SourceMango", "%1")},
        {QStringLiteral("togglefloating"),
         QT_TRANSLATE_NOOP("SourceMango", "Toggle floating")},
        {QStringLiteral("togglefullscreen"),
         QT_TRANSLATE_NOOP("SourceMango", "Toggle fullscreen")},
        {QStringLiteral("togglemaximizescreen"),
         QT_TRANSLATE_NOOP("SourceMango", "Maximize to screen")},
        {QStringLiteral("dwindle_toggle_split_direction"),
         QT_TRANSLATE_NOOP("SourceMango", "Toggle split direction")},
        {QStringLiteral("killclient"),
         QT_TRANSLATE_NOOP("SourceMango", "Close window")},
        {QStringLiteral("switch_layout"),
         QT_TRANSLATE_NOOP("SourceMango", "Next layout")},
        {QStringLiteral("setlayout"),
         QT_TRANSLATE_NOOP("SourceMango", "Layout %1")},
        {QStringLiteral("setmfact"),
         QT_TRANSLATE_NOOP("SourceMango", "Master ratio %1")},
        {QStringLiteral("set_proportion"),
         QT_TRANSLATE_NOOP("SourceMango", "Window width %1")},
        {QStringLiteral("focusdir"),
         QT_TRANSLATE_NOOP("SourceMango", "Focus %1")},
        {QStringLiteral("exchange_client"),
         QT_TRANSLATE_NOOP("SourceMango", "Swap window %1")},
        {QStringLiteral("focusmon"),
         QT_TRANSLATE_NOOP("SourceMango", "Monitor %1")},
        {QStringLiteral("tagmon"),
         QT_TRANSLATE_NOOP("SourceMango", "Window to monitor %1")},
        {QStringLiteral("zoom"),
         QT_TRANSLATE_NOOP("SourceMango", "Promote to master")},
        {QStringLiteral("view"), QT_TRANSLATE_NOOP("SourceMango", "Tag %1")},
        {QStringLiteral("tag"),
         QT_TRANSLATE_NOOP("SourceMango", "Window to tag %1")},
        {QStringLiteral("toggle_scratchpad"),
         QT_TRANSLATE_NOOP("SourceMango", "Toggle scratchpad")},
        {QStringLiteral("reload_config"),
         QT_TRANSLATE_NOOP("SourceMango", "Reload configuration")},
        {QStringLiteral("toggleoverview"),
         QT_TRANSLATE_NOOP("SourceMango", "Toggle overview")},
    };
    return table;
}

QString deriveDescription(const QString &action, const QString &params) {
    // "view" and "tag" carry PARAMS of the form "1,0": only the first field is
    // the tag number, the second is a mango-internal switch.
    const QString firstParam = params.section(QLatin1Char(','), 0, 0).trimmed();

    const char *entry = actionTexts().value(action, nullptr);
    if (entry == nullptr) {
        return params.isEmpty() ? action : action + QLatin1Char(' ') + params;
    }

    // Not const: it is returned below, and const would force a copy.
    QString pattern = QCoreApplication::translate("SourceMango", entry);
    if (!pattern.contains(QStringLiteral("%1"))) {
        return pattern;
    }
    // For "spawn" the PARAMS are the whole command line, otherwise the first
    // field is enough, translated when it is a direction.
    const QString value = (action == QStringLiteral("spawn"))
                              ? params.trimmed()
                              : directionText(firstParam);
    return QString(pattern).replace(QStringLiteral("%1"), value);
}

} // namespace

SourceMango::SourceMango(QString path) : m_path(std::move(path)) {}

QString SourceMango::name() const { return QStringLiteral("mango"); }

QString SourceMango::configPath() {
    const qint64 pid = compositorPid();
    if (pid > 0) {
        const QString named = configArgument(commandLineOf(pid));
        if (!named.isEmpty()) {
            // Against the compositor's directory, not this program's. A
            // session that runs "cd ~/.config/mango && mango -c custom.conf"
            // hands over a bare name, and this program was started from the
            // tray or a launcher, where the directory is the home or the
            // root. Resolving it here would name a file nobody wrote and
            // report it missing.
            //
            // An absolute name needs no directory, so it is only the relative
            // one that depends on this succeeding. Where the link cannot be
            // read there is no honest base to use, and the default below is
            // the better answer than a guess.
            const QString base = workingDirectoryOf(pid);
            if (QDir::isAbsolutePath(named) || !base.isEmpty()) {
                return resolvePath(named, base);
            }
        }
    }
    // Nothing was named, so mango's own search decides which file it reads.
    return chosenConfig(configCandidates(), [](const QString &path) {
        return QFileInfo::exists(path);
    });
}

ConfigCandidates SourceMango::configCandidates() {
    return {QDir::homePath() + QLatin1String(kDefaultConfig),
            QLatin1String(kSystemConfig)};
}

QString chosenConfig(const ConfigCandidates &candidates,
                     const std::function<bool(const QString &)> &isThere) {
    if (isThere(candidates.ownFile)) {
        return candidates.ownFile;
    }
    if (isThere(candidates.packagedFile)) {
        return candidates.packagedFile;
    }
    // Neither is there. The one in the home directory is the one named,
    // because it is the one somebody would write; a message about a file
    // nobody was going to create helps nobody.
    return candidates.ownFile;
}

// Inputs sourceFiles() is measured against:
//
//   configuration                       | result
//   ------------------------------------|------------------------------------
//   no source= line                     | the file itself
//   source=~/.config/mango/bind.conf    | both, in that order
//   source=bind.conf                    | the same, taken from the directory
//                                       |   of the file that named it
//   source= with nothing after it       | ignored, there is no file
//   a file naming itself                | read once, not forever
//   two files naming each other         | both once
//   a file that is not there            | still listed: the reader says so
//                                       |   rather than leaving it out
//                                       |   silently
QStringList SourceMango::sourceFiles(const QString &start) {
    QStringList files;
    QStringList pending{start};
    // Compared by the path with every link followed, so the same file reached
    // two ways is still the same file and is read once. Cleaning up "." and
    // ".." is not enough for that: a file pulled in once under its own name
    // and once through a link to it would pass as two, and every bind in it
    // would be listed twice.
    QSet<QString> seen;

    while (!pending.isEmpty() && files.size() < kMaxSourceFiles) {
        const QString path = pending.takeFirst();
        // A file that is not there has no canonical name, and the empty
        // string it answers with would make every missing file the same one.
        // The absolute name stands in, which is enough to keep a chain of
        // missing files from being followed twice.
        const QFileInfo info(path);
        const QString canonical =
            info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
        if (seen.contains(canonical)) {
            continue;
        }
        seen.insert(canonical);
        files.append(path);

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Reported by the reader, which has an error to write it into.
            continue;
        }
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        const QString directory = info.absolutePath();
        // Inserted in front of what is still pending rather than appended: a
        // file is read the moment it is named, which is the order mango reads
        // them in and therefore the order the headings come out in.
        QStringList named;
        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (!line.startsWith(QLatin1String(kPrefixSource))) {
                continue;
            }
            const QString resolved = resolvePath(
                line.mid(static_cast<int>(sizeof(kPrefixSource)) - 1),
                directory);
            if (!resolved.isEmpty()) {
                named.append(resolved);
            }
        }
        pending = named + pending;
    }
    return files;
}

// Inputs read() is measured against (every accepted shape is taken from a real
// bind.conf, the broken ones from asking what a typo produces):
//
//   line                                    | result
//   ----------------------------------------|-------------------------------
//   bind=SUPER,t,spawn,ghostty              | SUPER+T, "ghostty"
//   bind=SUPER+CTRL,t,spawn,kitty           | SUPER+CTRL+T
//   bind=SUPER,v,togglefloating,            | empty PARAMS, text from ACTION
//   bind=SUPER+CTRL,d,dwindle_toggle_split_direction | 3 fields only, valid
//   bind=SUPER,1,view,1,0                   | PARAMS "1,0" -> "Tag 1"
//   bind=SUPER,e,spawn,ghostty -e yazi      | blanks inside PARAMS survive
//   bind=SUPER+ALT,h,setlayout,scroller     | "Layout scroller"
//   bind=SUPER,Left,focusdir,left           | "Focus left"
//   # --- Programs ---                      | group changes
//   # any note                              | ignored, group unchanged
//   # ... containing bind= in its text      | ignored (line starts with #)
//   mousebind=SUPER,btn_left,...            | ignored, not a keyboard shortcut
//   axisbind=SUPER,UP,...                   | ignored
//   (blank line)                            | ignored
//     bind=SUPER,t,spawn,ghostty            | leading blanks allowed
//   bind=SUPER,t,spawn,ghostty\r            | CRLF: the \r is stripped
//   bind=                                   | invalid, counted
//   bind=SUPER                              | invalid (no comma)
//   bind=SUPER,,spawn,x                     | invalid (empty key)
//   bind=,t,spawn,x                         | valid, shortcut without modifier
//   file missing / unreadable               | empty list + message
//   binary garbage                          | empty list + message, no crash
QList<Bind> SourceMango::read(QString *error) const {
    QList<Bind> binds;

    // Where to start: the file that was handed in, or the one the running
    // compositor reads. Everything that file draws on follows from it.
    const QString start = m_path.isEmpty() ? configPath() : m_path;
    const QStringList files = sourceFiles(start);

    int invalid = 0;
    QStringList unreadable;

    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Named rather than skipped: a configuration that points at a file
            // which is not there is missing the shortcuts in it, and a panel
            // that simply shows fewer of them says nothing about why.
            unreadable.append(path);
            continue;
        }

        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);

        // Headings do not carry from one file into the next: a file that opens
        // with binds before any section comment starts under the default
        // heading, not under whatever the file before it ended with.
        QString group = defaultGroupName();

        while (!stream.atEnd()) {
            const QString line = stream.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            if (line.startsWith(QLatin1Char('#'))) {
                const auto match = sectionPattern().match(line);
                if (match.hasMatch()) {
                    group = match.captured(1);
                }
                continue;
            }
            if (!line.startsWith(QLatin1String(kPrefixBind))) {
                continue;
            }

            // MOD,KEY,ACTION,PARAMS: only the first three commas separate
            // fields, everything after them belongs to PARAMS (see
            // "bind=SUPER,1,view,1,0").
            const QString rest =
                line.mid(static_cast<int>(sizeof(kPrefixBind)) - 1);
            const qsizetype c1 = rest.indexOf(QLatin1Char(','));
            const qsizetype c2 =
                (c1 < 0) ? -1 : rest.indexOf(QLatin1Char(','), c1 + 1);
            if (c1 < 0 || c2 < 0) {
                ++invalid;
                continue;
            }
            const qsizetype c3 = rest.indexOf(QLatin1Char(','), c2 + 1);

            Bind bind;
            bind.modifiers = splitModifiers(rest.left(c1), QLatin1Char('+'));
            bind.key = normalizeKey(rest.mid(c1 + 1, c2 - c1 - 1));
            if (bind.key.isEmpty()) {
                ++invalid;
                continue;
            }

            const QString action =
                (c3 < 0) ? rest.mid(c2 + 1).trimmed()
                         : rest.mid(c2 + 1, c3 - c2 - 1).trimmed();
            const QString params =
                (c3 < 0) ? QString() : rest.mid(c3 + 1).trimmed();
            if (action.isEmpty()) {
                ++invalid;
                continue;
            }

            bind.description = deriveDescription(action, params);
            bind.group = group;
            binds.append(bind);
        }
    }

    if (error != nullptr) {
        QStringList notes;
        // The start of the chain first, so a reader knows which configuration
        // is being talked about before hearing what is wrong with it.
        if (binds.isEmpty()) {
            notes.append(QCoreApplication::translate(
                             "SourceMango", "%1 holds no bind= line at all")
                             .arg(start));
        }
        if (!unreadable.isEmpty()) {
            notes.append(
                QCoreApplication::translate("SourceMango", "cannot read %1")
                    .arg(unreadable.join(QLatin1String(", "))));
        }
        if (invalid > 0) {
            notes.append(QCoreApplication::translate(
                "SourceMango", "skipped %n unreadable bind= line(s)", nullptr,
                invalid));
        }
        *error = notes.join(QLatin1String(kNoteSeparator));
    }
    return binds;
}

} // namespace bindpeek
