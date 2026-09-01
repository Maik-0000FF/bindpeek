// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SourceSway.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace bindpeek {
namespace {

// The protocol. A message is the magic string, then the length of the payload
// and its type as 32-bit numbers in the machine's own byte order, then the
// payload. The reply has the same shape.
constexpr char kMagic[] = "i3-ipc";
constexpr int kMagicLength = 6;
constexpr int kHeaderLength =
    kMagicLength + 2 * static_cast<int>(sizeof(std::uint32_t));

// The message that asks for the configuration as it was loaded.
constexpr std::uint32_t kTypeGetConfig = 9;

// The property the reply carries the configuration text in.
constexpr char kFieldConfig[] = "config";

// The whole exchange has this long, connect and write and read together. A
// budget rather than three timeouts because read() runs on the thread that
// draws: the panel asks every time it is about to appear, and a compositor
// that has stopped answering must not freeze the program for the sum of three
// waits. Kilobytes over a local socket need microseconds, so this is a
// ceiling, never a duration anything reaches.
constexpr int kExchangeBudgetMs = 500;

// Keywords of the configuration.
constexpr char kKeywordBindsym[] = "bindsym";
constexpr char kKeywordBindcode[] = "bindcode";
// The other two ways sway binds something. Neither is a key on a keyboard,
// and neither is counted as left out: a lid and a touchpad were never going
// to appear on a keyboard cheat sheet. sway has exactly these four.
constexpr char kKeywordBindswitch[] = "bindswitch";
constexpr char kKeywordBindgesture[] = "bindgesture";
constexpr char kKeywordSet[] = "set";
constexpr char kKeywordInclude[] = "include";
constexpr char kKeywordMode[] = "mode";
constexpr char kCommentPrefix[] = "#";
constexpr char kBlockOpen[] = "{";
constexpr char kBlockClose[] = "}";
constexpr char kFlagPrefix[] = "--";
constexpr char kVariablePrefix[] = "$";
constexpr char kComboSeparator[] = "+";

// What restricts a bind to one keyboard group. It says nothing about which
// keys are held, so it is dropped and the bind kept.
//
// sway splits the combination at every "+" and tests each part for these, so
// they stand wherever the configuration put them, not only in front. Read from
// its own source, where Mode_switch is an alias for Group2.
constexpr char kGroupPrefix[] = "Group";
constexpr char kGroupAlias[] = "Mode_switch";

// The option a mode block may carry, which is none of its name.
constexpr char kModeMarkupFlag[] = "--pango_markup";

// A ceiling on the length a reply announces before a byte of it is read.
//
// The socket is named by an environment variable, so what answers is not
// guaranteed to be sway, and a length word is four bytes that can say four
// gigabytes. Reading into a buffer of that size happens on the thread that
// draws. A configuration is text a person wrote; eight megabytes is far above
// any of them and far below what hurts.
constexpr int kMaxReplyBytes = 8 * 1024 * 1024;

// Pointer buttons, which are not keyboard shortcuts and do not belong on a
// keyboard cheat sheet. sway writes them either way round.
constexpr char kButtonPrefix[] = "button";
constexpr char kButtonPrefixLong[] = "BTN_";

// What a command is worth in plain words, keyed by its first word.
//
// %1 is filled with the rest of the command, so an entry either spells the
// action out or hands the argument through. sway's own vocabulary, which is
// i3's: the words are the ones written in the configuration.
const QHash<QString, const char *> &actionTexts() {
    static const QHash<QString, const char *> table = {
        {QStringLiteral("exec"), QT_TRANSLATE_NOOP("SourceSway", "%1")},
        {QStringLiteral("exec_always"), QT_TRANSLATE_NOOP("SourceSway", "%1")},
        {QStringLiteral("kill"),
         QT_TRANSLATE_NOOP("SourceSway", "Close window")},
        {QStringLiteral("fullscreen"),
         QT_TRANSLATE_NOOP("SourceSway", "Toggle fullscreen")},
        {QStringLiteral("floating"),
         QT_TRANSLATE_NOOP("SourceSway", "Floating %1")},
        {QStringLiteral("focus"), QT_TRANSLATE_NOOP("SourceSway", "Focus %1")},
        {QStringLiteral("move"), QT_TRANSLATE_NOOP("SourceSway", "Move %1")},
        {QStringLiteral("workspace"),
         QT_TRANSLATE_NOOP("SourceSway", "Workspace %1")},
        {QStringLiteral("splith"),
         QT_TRANSLATE_NOOP("SourceSway", "Split horizontally")},
        {QStringLiteral("splitv"),
         QT_TRANSLATE_NOOP("SourceSway", "Split vertically")},
        {QStringLiteral("split"), QT_TRANSLATE_NOOP("SourceSway", "Split %1")},
        {QStringLiteral("layout"),
         QT_TRANSLATE_NOOP("SourceSway", "Layout %1")},
        {QStringLiteral("resize"),
         QT_TRANSLATE_NOOP("SourceSway", "Resize %1")},
        {QStringLiteral("gaps"), QT_TRANSLATE_NOOP("SourceSway", "Gaps %1")},
        {QStringLiteral("border"),
         QT_TRANSLATE_NOOP("SourceSway", "Border %1")},
        {QStringLiteral("sticky"),
         QT_TRANSLATE_NOOP("SourceSway", "Sticky %1")},
        {QStringLiteral("scratchpad"),
         QT_TRANSLATE_NOOP("SourceSway", "Scratchpad %1")},
        {QStringLiteral("mode"), QT_TRANSLATE_NOOP("SourceSway", "Mode %1")},
        {QStringLiteral("reload"),
         QT_TRANSLATE_NOOP("SourceSway", "Reload configuration")},
        {QStringLiteral("restart"), QT_TRANSLATE_NOOP("SourceSway", "Restart")},
        {QStringLiteral("exit"), QT_TRANSLATE_NOOP("SourceSway", "Exit")},
        {QStringLiteral("bar"), QT_TRANSLATE_NOOP("SourceSway", "Bar %1")},
        {QStringLiteral("input"), QT_TRANSLATE_NOOP("SourceSway", "Input %1")},
        {QStringLiteral("output"),
         QT_TRANSLATE_NOOP("SourceSway", "Output %1")},
        {QStringLiteral("nop"), QT_TRANSLATE_NOOP("SourceSway", "Nothing")},
    };
    return table;
}

// The words that make a command readable, from the command as written.
//
// Quotes go: a command is written with them where it holds blanks, and they
// are punctuation of the configuration, not of the answer.
//
// Every way out of here goes through the one check at the end. Source.h
// promises a description on every bind, and a configuration holds shapes that
// leave nothing to say: "exec" with no argument, a command of two quotes, a
// command of blanks. Checking each branch instead would be one check per
// branch and one branch someone adds later without it.
QString actionText(const QString &command) {
    QString rest = command.trimmed();
    rest.remove(QLatin1Char('"'));
    rest = rest.trimmed();

    const qsizetype cut = rest.indexOf(QLatin1Char(' '));
    const QString head = cut < 0 ? rest : rest.left(cut);
    const QString tail = cut < 0 ? QString() : rest.mid(cut + 1).trimmed();

    QString text;
    const auto found = actionTexts().constFind(head);
    if (found == actionTexts().cend()) {
        // Not a word this knows. The command itself is still the best answer
        // there is, and a shortcut with an unhelpful description beats one
        // that is missing.
        text = rest;
    } else {
        text = QCoreApplication::translate("SourceSway", found.value());
        text = text.contains(QStringLiteral("%1")) ? text.arg(tail) : text;
    }

    text = text.trimmed();
    if (!text.isEmpty()) {
        return text;
    }
    // The word that was written, if there was one, and otherwise the plain
    // truth: something is bound here and it says nothing about itself.
    return head.isEmpty()
               ? QCoreApplication::translate("SourceSway", "no command")
               : head;
}

// Splits a line into words, keeping what is inside double quotes together.
QStringList words(const QString &line) {
    static const QRegularExpression pattern(QStringLiteral("\"[^\"]*\"|\\S+"));
    QStringList out;
    auto it = pattern.globalMatch(line);
    while (it.hasNext()) {
        out << it.next().captured();
    }
    return out;
}

// Puts the value of every variable in place of its name.
//
// Longest name first, so "$mod" does not eat the beginning of "$mode_resize"
// and leave a name nothing answers to.
QString expand(QString text, const QHash<QString, QString> &variables) {
    if (!text.contains(QLatin1Char('$'))) {
        return text;
    }
    QStringList names = variables.keys();
    std::sort(
        names.begin(), names.end(),
        [](const QString &a, const QString &b) { return a.size() > b.size(); });
    for (const QString &name : names) {
        text.replace(name, variables.value(name));
    }
    return text;
}

// Whether a line binds something, whatever it binds it to.
bool bindsSomething(const QString &keyword) {
    return keyword == QLatin1String(kKeywordBindsym) ||
           keyword == QLatin1String(kKeywordBindcode) ||
           keyword == QLatin1String(kKeywordBindswitch) ||
           keyword == QLatin1String(kKeywordBindgesture);
}

// The heading in force right now, empty while no mode is open.
//
// One spelling for both places that ask: what a bind is filed under, which
// turns an empty answer into the default name, and what an inner block
// inherits, which passes it on as it is. Two spellings disagreed about the
// empty case, and a nameless "mode {" fell through the gap between them.
QString openHeading(const QStringList &blocks) {
    return blocks.isEmpty() ? QString() : blocks.constLast();
}

// The lines of a configuration, trimmed, without the empty ones, and with a
// brace that stands on its own put back on the line it belongs to.
//
// sway takes both spellings of a block:
//
//     mode "resize" {          mode "resize"
//                              {
//
// Having read a line that does not already end in a brace, it looks ahead for
// one standing alone and, finding it, hangs it on the end of the line just
// read. The lookahead steps over empty lines and stops at the first line
// holding anything else, a comment among them: a comment between the two
// leaves the brace belonging to nothing, and sway then has no command by that
// name. Read from sway's own source rather than assumed.
//
// Done here rather than in the loop below, which reads one line at a time and
// would have to carry the one before it just for this.
QStringList configLines(const QString &text) {
    QStringList out;
    const QStringList raw = text.split(QLatin1Char('\n'));
    for (const QString &line : raw) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (trimmed == QLatin1String(kBlockOpen) && !out.isEmpty()) {
            const QString &previous = out.constLast();
            if (!previous.startsWith(QLatin1String(kCommentPrefix)) &&
                !previous.endsWith(QLatin1String(kBlockOpen)) &&
                !previous.endsWith(QLatin1String(kBlockClose))) {
                out.last().append(QLatin1Char(' ')).append(trimmed);
                continue;
            }
        }
        out << trimmed;
    }
    return out;
}

QString unquoted(QString text) {
    text.remove(QLatin1Char('"'));
    return text;
}

} // namespace

// The lines parseConfig() is measured against:
//
//   input                                   | result
//   ----------------------------------------|-----------------------------
//   set $mod Mod4                           | remembered, not a bind
//   bindsym $mod+Return exec foot           | SUPER+Return, "foot"
//   bindsym Mod1+Shift+q kill               | ALT+SHIFT+Q, "Close window"
//   bindsym --to-code $mod+d exec menu      | flags skipped, SUPER+D
//   bindsym --release Print exec grim       | flags skipped, Print
//   bindsym Group2+$mod+x exec x            | group prefix dropped, SUPER+X
//   bindsym $mod+button1 kill               | skipped, a pointer button
//   bindsym --whole-window $mod+BTN_LEFT .. | skipped, a pointer button
//   bindcode $mod+24 exec foo               | skipped, a keycode has no name
//   bindsym Mod3+x exec foo                 | skipped, no name for that key
//   bindsym Mod5+x exec foo                 | skipped, same
//   mode "resize" {                         | heading for what follows
//   bindsym Left resize shrink width 10px   | under that heading
//   bar {                                   | a block that is not a mode
//   }                                       | ends that block, not the mode
//   }                                       | back to the default heading
//   bindsym Left exec foo {                 | a command, not a block: kept
//   bindswitch lid:on exec foo {            | never a key, and not a block
//   bindgesture swipe:3:right exec foo {    | the same
//   bindcode 24 exec foo {                  | a key without a name, counted,
//                                           | and not a block either
//   include /etc/sway/config.d/*            | reported: what it pulls in is
//                                           | not part of what was read, and
//                                           | one line is counted once
//                                           | however many files it names
//   bindsym Group2 exec foo                 | skipped: a group and no key
//   bindsym $mod+x ""                       | kept, named "no command"
//   bindsym $mod+x exec ""                  | kept, named "exec": the word
//                                           | that was written is what is
//                                           | left to say
//   # bindsym $mod+q kill                   | a comment, not a bind
//   bindsym $mod+q                          | skipped, nothing to run
//   bindsym                                 | skipped, nothing at all
//
// Variables are replaced before any of this, and a value may name another
// variable. Everything the panel cannot name is skipped rather than shown
// under a combination that would not trigger it.
//
// *note collects what is missing from the list and why, one sentence each:
// a line that would have been a keyboard shortcut and could not be shown as
// one (a pointer button, a keycode, a modifier with no name, a line with
// nothing left to bind or nothing to run), and a line that pulls further
// configuration in, whose binds are missing for a different reason: they are
// not part of what was read here at all.
//
// A switch and a gesture are in neither group. Neither was ever going to
// appear on a keyboard cheat sheet, and a note about them would say something
// is missing where nothing is.
QList<Bind> SourceSway::parseConfig(const QString &text, QString *note) {
    QList<Bind> binds;
    QHash<QString, QString> variables;
    // One entry per open block, innermost last, and each entry is the heading
    // that holds inside it: the name of a mode block, and for any other block
    // whatever it inherited. That way the top of the stack is always the
    // answer, and a mode keeps its heading across a "bar" nested in it.
    //
    // sway has blocks that are not modes, and counting their braces as a
    // mode's would end a heading early and file the binds after it wrongly.
    QStringList blocks;
    int skippedPointer = 0;
    int skippedCode = 0;
    int skippedModifier = 0;
    int skippedEmpty = 0;
    int includeLines = 0;

    const QStringList lines = configLines(text);
    for (const QString &raw : lines) {
        QString line = raw;
        if (line.startsWith(QLatin1String(kCommentPrefix))) {
            continue;
        }

        // A closing brace ends the innermost block, whatever it was.
        if (line.startsWith(QLatin1String(kBlockClose))) {
            if (!blocks.isEmpty()) {
                blocks.removeLast();
            }
            continue;
        }

        // A set line is read before anything is replaced in it, because the
        // name it introduces may begin like one that already exists: with
        // "$mod" known, replacing inside "set $mode_resize resize" would turn
        // the name into "Mod4e_resize" and store the value under something
        // nothing ever asks for. Only the value is expanded, which is also
        // the order sway reads in.
        QStringList parts = words(line);
        if (parts.isEmpty()) {
            continue;
        }
        if (parts.constFirst() == QLatin1String(kKeywordSet) &&
            parts.size() >= 3) {
            const QString &name = parts.at(1);
            if (name.startsWith(QLatin1String(kVariablePrefix))) {
                const QString value =
                    expand(QStringList(parts.mid(2)).join(QLatin1Char(' ')),
                           variables);
                variables.insert(name, unquoted(value));
            }
            continue;
        }

        // Everywhere else the value stands in for the name, and what comes out
        // is read afresh: a variable may hold a whole combination.
        line = expand(line, variables);
        parts = words(line);
        if (parts.isEmpty()) {
            continue;
        }
        const QString keyword = parts.takeFirst();

        // A line whose last word is a brace of its own opens a block, and the
        // block is named by everything before that brace.
        //
        // Asked first and of every line, a bind among them, because that is
        // the order sway asks in: config_command() looks for the brace before
        // it looks for a handler for the first word. A "bindsym ... {" opens a
        // block there and binds nothing, so reading it as a bind here would
        // put a shortcut on the panel that sway never registered, and hand the
        // brace closing that block to the mode around it.
        //
        // The word, not the last character of the line: sway splits the line
        // into words and compares the last of them against "{", so
        // "exec_always ~/bin/x{" runs a command and opens nothing. A word
        // before the brace is wanted for the same reason, a line of nothing
        // but a brace naming no block sway could enter.
        if (!parts.isEmpty() &&
            parts.constLast() == QLatin1String(kBlockOpen)) {
            parts.removeLast();
            QString heading = openHeading(blocks);
            if (keyword == QLatin1String(kKeywordMode) && !parts.isEmpty()) {
                parts.removeAll(QLatin1String(kModeMarkupFlag));
                heading = unquoted(parts.join(QLatin1Char(' '))).trimmed();
            }
            blocks.append(heading);
            continue;
        }

        // What such a line pulls in is not part of what was read here, and
        // neither are the binds in it. Counted and reported, because a list
        // quietly missing half the shortcuts is worse than one that says so.
        //
        // Neither path follows the line. sway does not hand out what an
        // include pulls in, and that is not an assumption: the GET_CONFIG
        // branch of sway's IPC server puts one property in the reply,
        // "config", filled from the buffer that is written only while the main
        // file is being read. i3 grew a second property for the included
        // files; sway, whose IPC follows i3's, has not. Given a file through
        // --source, the line is not followed either.
        if (keyword == QLatin1String(kKeywordInclude)) {
            // The line, not the files: one of these may name a whole
            // directory, and how many files that is cannot be known from
            // here.
            ++includeLines;
            continue;
        }

        // What binds nothing has nothing more to give here.
        //
        // One question, asked once, so a fifth binding word added later is
        // wrong in one place instead of quietly passing for a command.
        if (!bindsSomething(keyword)) {
            continue;
        }

        // Of the four, only one puts a key on screen.
        if (keyword != QLatin1String(kKeywordBindsym)) {
            if (keyword == QLatin1String(kKeywordBindcode)) {
                // A keycode is a number on this keyboard's layout and has no
                // name to put on screen. Counted, because it would have been
                // a keyboard shortcut.
                ++skippedCode;
            }
            // A lid and a touchpad are not keys either, and neither was ever
            // going to appear here, so neither is counted as left out.
            continue;
        }

        // The flags come before the combination and say when a bind fires,
        // never which keys it wants.
        while (!parts.isEmpty() &&
               parts.constFirst().startsWith(QLatin1String(kFlagPrefix))) {
            parts.removeFirst();
        }
        if (parts.size() < 2) {
            ++skippedEmpty;
            continue;
        }

        QString combo = parts.takeFirst();
        const QString command = parts.join(QLatin1Char(' '));

        QStringList tokens =
            combo.split(QLatin1String(kComboSeparator), Qt::SkipEmptyParts);
        // The keyboard group is a restriction, not a key, and it stands
        // wherever it was written rather than only in front.
        tokens.removeIf([](const QString &token) {
            return token.startsWith(QLatin1String(kGroupPrefix)) ||
                   token == QLatin1String(kGroupAlias);
        });
        if (tokens.isEmpty()) {
            ++skippedEmpty;
            continue;
        }

        const QString key = tokens.takeLast();
        if (key.startsWith(QLatin1String(kButtonPrefix), Qt::CaseInsensitive) ||
            key.startsWith(QLatin1String(kButtonPrefixLong))) {
            ++skippedPointer;
            continue;
        }

        QStringList modifiers;
        bool nameable = true;
        for (const QString &token : tokens) {
            const QString canonical = normalizeModifier(token);
            if (canonical.isEmpty()) {
                // Mod2, Mod3, Mod5 and Lock have no name the keyboard watch
                // reports, so a shortcut needing one could never be matched.
                // Dropping the token instead would put it on screen under a
                // combination that does not trigger it.
                nameable = false;
                break;
            }
            modifiers << canonical;
        }
        if (!nameable) {
            ++skippedModifier;
            continue;
        }

        Bind bind;
        bind.modifiers = orderModifiers(modifiers);
        bind.key = normalizeKey(key);
        bind.description = actionText(command);
        const QString heading = openHeading(blocks);
        bind.group = heading.isEmpty() ? defaultGroupName() : heading;
        binds.append(bind);
    }

    if (note != nullptr) {
        QStringList notes;
        // Said first, and never instead of the counts: a configuration that
        // yielded nothing but skipped lines has to name them, or the reader is
        // told there are no shortcuts when the truth is that none could be
        // shown.
        //
        // Said at all because every counter can stand at zero and still leave
        // nothing to show: a configuration whose only binding lines are
        // bindswitch or bindgesture is read to the end and counts neither, on
        // purpose. The caller takes an empty list for a failure and prints the
        // note on a line of its own, so an empty note reaches the screen as a
        // blank line.
        if (binds.isEmpty()) {
            notes << QCoreApplication::translate(
                "SourceSway", "the configuration holds no keyboard shortcut");
        }
        if (skippedPointer > 0) {
            notes << QCoreApplication::translate("SourceSway",
                                                 "%n pointer button(s) skipped",
                                                 nullptr, skippedPointer);
        }
        if (skippedCode > 0) {
            notes << QCoreApplication::translate("SourceSway",
                                                 "%n keycode bind(s) skipped",
                                                 nullptr, skippedCode);
        }
        if (skippedModifier > 0) {
            notes << QCoreApplication::translate(
                "SourceSway",
                "%n bind(s) skipped that need a modifier this panel "
                "cannot name",
                nullptr, skippedModifier);
        }
        if (includeLines > 0) {
            notes << QCoreApplication::translate(
                "SourceSway",
                "%n include line(s) not followed: what is pulled in is not "
                "part of the answer",
                nullptr, includeLines);
        }
        if (skippedEmpty > 0) {
            notes << QCoreApplication::translate(
                "SourceSway", "%n incomplete bind(s) skipped", nullptr,
                skippedEmpty);
        }
        *note = notes.join(QLatin1String(kNoteSeparator));
    }
    return binds;
}

SourceSway::SourceSway(QString path) : m_configPath(std::move(path)) {}

QString SourceSway::name() const { return QStringLiteral("sway"); }

QString SourceSway::socketPath() {
    const QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
    QString own = environment.value(QLatin1String(kSwaySocketVar));
    if (!own.isEmpty()) {
        return own;
    }
    return environment.value(QLatin1String(kSwaySocketVarLegacy));
}

namespace {

// Builds one message: magic, length, type, payload. The two numbers go in the
// machine's own byte order, which is what the protocol asks for.
QByteArray message(std::uint32_t type, const QByteArray &payload) {
    QByteArray out(kMagic, kMagicLength);
    const auto length = static_cast<std::uint32_t>(payload.size());
    out.append(reinterpret_cast<const char *>(&length), sizeof(length));
    out.append(reinterpret_cast<const char *>(&type), sizeof(type));
    out.append(payload);
    return out;
}

// Reads exactly one reply and hands back its payload. Empty with *error set
// when the exchange did not finish inside the budget.
QByteArray reply(QLocalSocket &socket, QDeadlineTimer deadline,
                 QString *error) {
    QByteArray header;
    while (header.size() < kHeaderLength) {
        // Asked only when nothing is buffered. The reply may well have
        // arrived while the request was still being written, and waiting for
        // more that never comes would spend the whole budget and then report
        // silence over an answer that is already in hand.
        if (socket.bytesAvailable() == 0 &&
            !socket.waitForReadyRead(
                static_cast<int>(deadline.remainingTime()))) {
            *error = QCoreApplication::translate(
                "SourceSway", "The compositor did not answer.");
            return {};
        }
        header.append(socket.read(kHeaderLength - header.size()));
    }
    if (std::memcmp(header.constData(), kMagic, kMagicLength) != 0) {
        *error = QCoreApplication::translate(
            "SourceSway", "The compositor answered in a shape this does not "
                          "understand.");
        return {};
    }

    std::uint32_t length = 0;
    std::memcpy(&length, header.constData() + kMagicLength, sizeof(length));
    // Believed only as far as it is plausible. What is read next is asked for
    // in one go, so a length word of four gigabytes would be four gigabytes
    // asked of the thread that draws, from a socket whose address came out of
    // the environment.
    if (length > static_cast<std::uint32_t>(kMaxReplyBytes)) {
        *error =
            QCoreApplication::translate(
                "SourceSway",
                "The compositor announced an answer of %1 bytes, which is more "
                "than a configuration ever is.")
                .arg(length);
        return {};
    }

    QByteArray payload;
    while (static_cast<std::uint32_t>(payload.size()) < length) {
        if (socket.bytesAvailable() == 0 &&
            !socket.waitForReadyRead(
                static_cast<int>(deadline.remainingTime()))) {
            *error = QCoreApplication::translate(
                "SourceSway", "The compositor stopped half way through its "
                              "answer.");
            return {};
        }
        payload.append(
            socket.read(static_cast<qint64>(length) - payload.size()));
    }
    return payload;
}

} // namespace

QList<Bind> SourceSway::read(QString *error) const {
    // Written through one place rather than at each turn: the pointer is
    // allowed to be null, which every backend here honours, and six separate
    // checks are six chances to forget one.
    const auto report = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
    };

    QString text;

    if (!m_configPath.isEmpty()) {
        QFile file(m_configPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            report(QCoreApplication::translate("SourceSway",
                                               "%1 could not be read.")
                       .arg(m_configPath));
            return {};
        }
        text = QString::fromUtf8(file.readAll());
    } else {
        const QString path = socketPath();
        if (path.isEmpty()) {
            report(QCoreApplication::translate(
                "SourceSway",
                "No running sway found: neither SWAYSOCK nor I3SOCK is set."));
            return {};
        }

        QDeadlineTimer deadline(kExchangeBudgetMs);
        QLocalSocket socket;
        socket.connectToServer(path);
        if (!socket.waitForConnected(
                static_cast<int>(deadline.remainingTime()))) {
            report(QCoreApplication::translate(
                       "SourceSway", "The socket at %1 did not answer.")
                       .arg(path));
            return {};
        }

        socket.write(message(kTypeGetConfig, QByteArray()));
        if (!socket.waitForBytesWritten(
                static_cast<int>(deadline.remainingTime()))) {
            report(QCoreApplication::translate(
                "SourceSway", "The request could not be sent."));
            return {};
        }

        QString failure;
        const QByteArray payload = reply(socket, deadline, &failure);
        if (!failure.isEmpty()) {
            report(failure);
            return {};
        }

        QJsonParseError parse{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()) {
            report(QCoreApplication::translate(
                       "SourceSway", "The answer could not be read: %1")
                       .arg(parse.errorString()));
            return {};
        }
        text = document.object().value(QLatin1String(kFieldConfig)).toString();
        if (text.isEmpty()) {
            report(QCoreApplication::translate(
                "SourceSway", "The compositor reported no configuration."));
            return {};
        }
    }

    QString note;
    QList<Bind> binds = parseConfig(text, &note);
    if (!note.isEmpty()) {
        report(note);
    }
    return binds;
}

} // namespace bindpeek
