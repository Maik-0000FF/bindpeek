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
constexpr char kKeywordSet[] = "set";
constexpr char kKeywordInclude[] = "include";
constexpr char kKeywordMode[] = "mode";
constexpr char kCommentPrefix[] = "#";
constexpr char kBlockOpen[] = "{";
constexpr char kBlockClose[] = "}";
constexpr char kFlagPrefix[] = "--";
constexpr char kVariablePrefix[] = "$";
constexpr char kComboSeparator[] = "+";

// The prefix that restricts a bind to one keyboard group. It says nothing
// about which keys are held, so it is dropped and the bind kept.
constexpr char kGroupPrefix[] = "Group";

// Pointer buttons, which are not keyboard shortcuts and do not belong on a
// keyboard cheat sheet. sway writes them either way round.
constexpr char kButtonPrefix[] = "button";
constexpr char kButtonPrefixLong[] = "BTN_";

// Joins several notes into the one message read() hands back.
constexpr char kNoteSeparator[] = "; ";

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
QString actionText(const QString &command) {
    QString rest = command.trimmed();
    if (rest.isEmpty()) {
        return {};
    }

    const qsizetype cut = rest.indexOf(QLatin1Char(' '));
    const QString head = cut < 0 ? rest : rest.left(cut);
    QString tail = cut < 0 ? QString() : rest.mid(cut + 1).trimmed();
    tail.remove(QLatin1Char('"'));

    const auto found = actionTexts().constFind(head);
    if (found == actionTexts().cend()) {
        // Not a word this knows. The command itself is still the best answer
        // there is, and a shortcut with an unhelpful description beats one
        // that is missing.
        rest.remove(QLatin1Char('"'));
        return rest;
    }

    const QString text =
        QCoreApplication::translate("SourceSway", found.value());
    return text.contains(QStringLiteral("%1")) ? text.arg(tail).trimmed()
                                               : text;
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
//   }                                       | back to the default heading
//   # bindsym $mod+q kill                   | a comment, not a bind
//   bindsym $mod+q                          | skipped, nothing to run
//   bindsym                                 | skipped, nothing at all
//
// Variables are replaced before any of this, and a value may name another
// variable. Everything the panel cannot name is skipped rather than shown
// under a combination that would not trigger it, and every skip is counted
// into *note.
QList<Bind> SourceSway::parseConfig(const QString &text, QString *note) {
    QList<Bind> binds;
    QHash<QString, QString> variables;
    QStringList modes; // the open mode blocks, innermost last
    int skippedPointer = 0;
    int skippedCode = 0;
    int skippedModifier = 0;
    int skippedEmpty = 0;
    int included = 0;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String(kCommentPrefix))) {
            continue;
        }

        // A closing brace ends the innermost block, whatever it was.
        if (line.startsWith(QLatin1String(kBlockClose))) {
            if (!modes.isEmpty()) {
                modes.removeLast();
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

        // A mode block opens a heading; "mode <name>" without a brace is a
        // command that switches to one and is not a block at all.
        if (keyword == QLatin1String(kKeywordMode) && !parts.isEmpty() &&
            line.endsWith(QLatin1String(kBlockOpen))) {
            parts.removeAll(QLatin1String(kBlockOpen));
            parts.removeAll(QStringLiteral("--pango_markup"));
            modes.append(unquoted(parts.join(QLatin1Char(' '))).trimmed());
            continue;
        }

        // sway hands out the text of its main file and nothing else: what it
        // read from an included file is not in the answer, and neither are
        // the binds in it. Counted and reported, because a list quietly
        // missing half the shortcuts is worse than one that says so.
        if (keyword == QLatin1String(kKeywordInclude)) {
            ++included;
            continue;
        }

        if (keyword == QLatin1String(kKeywordBindcode)) {
            // A keycode is a number on this keyboard's layout and has no name
            // to put on screen.
            ++skippedCode;
            continue;
        }
        if (keyword != QLatin1String(kKeywordBindsym)) {
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

        // The keyboard group is a restriction, not a key.
        if (combo.startsWith(QLatin1String(kGroupPrefix))) {
            const qsizetype cut = combo.indexOf(QLatin1Char('+'));
            combo = cut < 0 ? QString() : combo.mid(cut + 1);
        }

        QStringList tokens =
            combo.split(QLatin1String(kComboSeparator), Qt::SkipEmptyParts);
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
        bind.group = modes.isEmpty() ? defaultGroupName() : modes.constLast();
        binds.append(bind);
    }

    if (note != nullptr) {
        QStringList notes;
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
        if (included > 0) {
            notes << QCoreApplication::translate(
                "SourceSway",
                "%n included file(s) not read: the compositor hands out its "
                "main file only",
                nullptr, included);
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
        if (!socket.waitForReadyRead(
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
    QString text;

    if (!m_configPath.isEmpty()) {
        QFile file(m_configPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *error = QCoreApplication::translate("SourceSway",
                                                 "%1 could not be read.")
                         .arg(m_configPath);
            return {};
        }
        text = QString::fromUtf8(file.readAll());
    } else {
        const QString path = socketPath();
        if (path.isEmpty()) {
            *error = QCoreApplication::translate(
                "SourceSway",
                "No running sway found: neither SWAYSOCK nor I3SOCK is set.");
            return {};
        }

        QDeadlineTimer deadline(kExchangeBudgetMs);
        QLocalSocket socket;
        socket.connectToServer(path);
        if (!socket.waitForConnected(
                static_cast<int>(deadline.remainingTime()))) {
            *error = QCoreApplication::translate(
                         "SourceSway", "The socket at %1 did not answer.")
                         .arg(path);
            return {};
        }

        socket.write(message(kTypeGetConfig, QByteArray()));
        if (!socket.waitForBytesWritten(
                static_cast<int>(deadline.remainingTime()))) {
            *error = QCoreApplication::translate(
                "SourceSway", "The request could not be sent.");
            return {};
        }

        QString failure;
        const QByteArray payload = reply(socket, deadline, &failure);
        if (!failure.isEmpty()) {
            *error = failure;
            return {};
        }

        QJsonParseError parse{};
        const QJsonDocument document = QJsonDocument::fromJson(payload, &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()) {
            *error = QCoreApplication::translate(
                         "SourceSway", "The answer could not be read: %1")
                         .arg(parse.errorString());
            return {};
        }
        text = document.object().value(QLatin1String(kFieldConfig)).toString();
        if (text.isEmpty()) {
            *error = QCoreApplication::translate(
                "SourceSway", "The compositor reported no configuration.");
            return {};
        }
    }

    QString note;
    QList<Bind> binds = parseConfig(text, &note);
    if (!note.isEmpty()) {
        *error = note;
    }
    return binds;
}

} // namespace bindpeek
