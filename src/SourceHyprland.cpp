// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SourceHyprland.h"

#include "Compositor.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>
#include <QProcessEnvironment>

#include <unistd.h>

#include <cstdint>

namespace bindpeek {
namespace {

// The request. "j/" asks for JSON, the rest is the command; this is the whole
// protocol, the reply follows and the compositor then closes the connection.
constexpr char kRequestBinds[] = "j/binds";

// The whole exchange, connect and write and read together, has this long. It
// is a budget rather than three separate timeouts because read() runs on the
// thread that draws: the panel is asked for it every time it is about to
// appear, and a compositor that has stopped answering must not freeze the
// program for the sum of three waits. Kilobytes over a local socket need
// microseconds, so this is a ceiling, never a duration anything reaches.
constexpr int kExchangeBudgetMs = 500;

// Fields of one entry, as Hyprland writes them.
constexpr char kFieldModmask[] = "modmask";
constexpr char kFieldKey[] = "key";
constexpr char kFieldKeycode[] = "keycode";
constexpr char kFieldCatchAll[] = "catch_all";
constexpr char kFieldDescription[] = "description";
constexpr char kFieldHasDescription[] = "has_description";
constexpr char kFieldDispatcher[] = "dispatcher";
constexpr char kFieldArg[] = "arg";
constexpr char kFieldSubmap[] = "submap";
constexpr char kFieldMouse[] = "mouse";

// Bit values of modmask, from Hyprland's own HL_MODIFIER_* enum. Only the four
// with a canonical name here are listed; the enum also has CAPS (1<<1), MOD2
// (1<<4), MOD3 (1<<5) and MOD5 (1<<7), and a plugin or a later release may add
// more above those.
constexpr std::uint32_t kModShift = 1U << 0;
constexpr std::uint32_t kModCtrl = 1U << 2;
constexpr std::uint32_t kModAlt = 1U << 3;
constexpr std::uint32_t kModMeta = 1U << 6;

// Everything this panel can name. The keyboard watch reports SUPER, CTRL, ALT
// and SHIFT and nothing else, so a bind needing any other bit could never be
// matched; dropping the bit instead would put the shortcut on screen under a
// combination that does not trigger it. Stated as what is known rather than as
// a list of what is not, so a bit nobody has seen yet is refused as well.
constexpr std::uint32_t kModNameable =
    kModShift | kModCtrl | kModAlt | kModMeta;

// The dispatcher every bind written in a Lua configuration carries.
//
// Hyprland registers one handler under this name for all of them and puts the
// Lua registry index of the callback in the argument, so a reply holds
// "__lua" and a number and nothing else. That is true of hl.dsp.exec_cmd() as
// much as of a bare function: hlBind stores handler "__lua" and the reference
// as its argument, and the dispatcher the user actually wrote is gone by the
// time hyprctl is asked. A description given to hl.bind survives, and arrives
// through has_description like any other.
constexpr char kDispatcherLua[] = "__lua";

// Every pointer key name Hyprland knows begins this way: "mouse:272" for the
// buttons, "mouse_up" and its three siblings for the wheel. None of them is a
// keyboard shortcut, and none belongs on a keyboard cheat sheet.
constexpr char kMousePrefix[] = "mouse";

// Joins several notes into the one message read() hands back.
constexpr char kNoteSeparator[] = "; ";

// Hyprland reads only the first character of a direction argument, so "l" and
// "left" are the same instruction. Its own spelling, from eDirection.
QString directionText(const QString &arg) {
    static const QHash<QChar, const char *> table = {
        {QLatin1Char('l'), QT_TRANSLATE_NOOP("SourceHyprland", "left")},
        {QLatin1Char('r'), QT_TRANSLATE_NOOP("SourceHyprland", "right")},
        {QLatin1Char('u'), QT_TRANSLATE_NOOP("SourceHyprland", "up")},
        {QLatin1Char('t'), QT_TRANSLATE_NOOP("SourceHyprland", "up")},
        {QLatin1Char('d'), QT_TRANSLATE_NOOP("SourceHyprland", "down")},
        {QLatin1Char('b'), QT_TRANSLATE_NOOP("SourceHyprland", "down")},
    };
    if (arg.isEmpty()) {
        return arg;
    }
    const char *entry = table.value(arg.at(0).toLower(), nullptr);
    // Not a direction after all: "mon:DP-1" reaches movewindow the same way.
    // Then the argument is shown as written.
    return (entry == nullptr)
               ? arg
               : QCoreApplication::translate("SourceHyprland", entry);
}

// Description per dispatcher. "%1" is replaced by the argument. Only the
// dispatchers whose meaning is plain are listed; a name that is missing is
// shown raw together with its argument, so a bind never goes missing silently
// and a plugin dispatcher still reads sensibly.
const QHash<QString, const char *> &dispatcherTexts() {
    static const QHash<QString, const char *> table = {
        // The argument is the command line itself, so it stands alone.
        {QStringLiteral("exec"), QT_TRANSLATE_NOOP("SourceHyprland", "%1")},
        {QStringLiteral("execr"), QT_TRANSLATE_NOOP("SourceHyprland", "%1")},
        {QStringLiteral("global"), QT_TRANSLATE_NOOP("SourceHyprland", "%1")},

        {QStringLiteral("killactive"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Close window")},
        {QStringLiteral("forcekillactive"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Force close window")},
        {QStringLiteral("closewindow"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Close window %1")},

        {QStringLiteral("togglefloating"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Toggle floating")},
        {QStringLiteral("setfloating"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Float window")},
        {QStringLiteral("settiled"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Tile window")},
        {QStringLiteral("fullscreen"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Toggle fullscreen")},
        {QStringLiteral("fullscreenstate"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Fullscreen state %1")},
        {QStringLiteral("pseudo"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Toggle pseudo tiling")},
        {QStringLiteral("pin"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Pin window")},
        {QStringLiteral("centerwindow"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Center window")},

        {QStringLiteral("workspace"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Workspace %1")},
        {QStringLiteral("movetoworkspace"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Window to workspace %1")},
        {QStringLiteral("movetoworkspacesilent"),
         QT_TRANSLATE_NOOP("SourceHyprland",
                           "Window to workspace %1, stay here")},
        {QStringLiteral("togglespecialworkspace"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Toggle special workspace %1")},
        {QStringLiteral("movecurrentworkspacetomonitor"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Workspace to monitor %1")},

        {QStringLiteral("movefocus"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Focus %1")},
        {QStringLiteral("movewindow"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Move window %1")},
        {QStringLiteral("swapwindow"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Swap window %1")},
        {QStringLiteral("focusmonitor"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Monitor %1")},
        {QStringLiteral("cyclenext"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Next window")},
        {QStringLiteral("swapnext"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Swap with next window")},
        {QStringLiteral("bringactivetotop"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Bring window to front")},
        {QStringLiteral("focuscurrentorlast"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Focus last window")},
        {QStringLiteral("focusurgentorlast"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Focus urgent or last window")},

        {QStringLiteral("resizeactive"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Resize window %1")},
        {QStringLiteral("moveactive"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Move window by %1")},

        {QStringLiteral("togglegroup"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Toggle group")},
        {QStringLiteral("changegroupactive"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Next window in group")},
        {QStringLiteral("moveoutofgroup"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Window out of group")},
        {QStringLiteral("lockgroups"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Lock groups %1")},

        {QStringLiteral("layoutmsg"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Layout: %1")},
        {QStringLiteral("submap"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Submap %1")},
        {QStringLiteral("toggleswallow"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Toggle swallowing")},
        {QStringLiteral("dpms"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Screen %1")},
        {QStringLiteral("forcerendererreload"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Reload renderer")},
        {QStringLiteral("exit"),
         QT_TRANSLATE_NOOP("SourceHyprland", "Exit Hyprland")},
    };
    return table;
}

// The dispatchers whose argument is a direction rather than a name or number.
// Hyprland resolves these through eDirection; every other argument is what the
// user typed and is shown that way.
bool takesDirection(const QString &dispatcher) {
    return dispatcher == QStringLiteral("movefocus") ||
           dispatcher == QStringLiteral("movewindow") ||
           dispatcher == QStringLiteral("swapwindow");
}

QString deriveDescription(const QString &dispatcher, const QString &arg) {
    const char *entry = dispatcherTexts().value(dispatcher, nullptr);
    if (entry == nullptr) {
        return arg.isEmpty() ? dispatcher : dispatcher + QLatin1Char(' ') + arg;
    }

    // Not const: it is returned below, and const would force a copy.
    QString pattern = QCoreApplication::translate("SourceHyprland", entry);
    if (!pattern.contains(QStringLiteral("%1"))) {
        return pattern;
    }
    const QString value = takesDirection(dispatcher) ? directionText(arg) : arg;
    // An argument-taking dispatcher called without one: "Workspace %1" would
    // otherwise keep its placeholder on screen.
    if (value.isEmpty()) {
        return dispatcher;
    }
    return QString(pattern).replace(QStringLiteral("%1"), value);
}

// Splits modmask into the canonical names. False when the mask carries a
// modifier this panel has no name for; the caller then drops the bind rather
// than showing it under the wrong combination.
bool decodeModifiers(std::uint32_t mask, QStringList *mods) {
    if ((mask & ~kModNameable) != 0U) {
        return false;
    }
    QStringList found;
    if ((mask & kModMeta) != 0U) {
        found.append(QString::fromLatin1(modifier::kSuper));
    }
    if ((mask & kModCtrl) != 0U) {
        found.append(QString::fromLatin1(modifier::kCtrl));
    }
    if ((mask & kModAlt) != 0U) {
        found.append(QString::fromLatin1(modifier::kAlt));
    }
    if ((mask & kModShift) != 0U) {
        found.append(QString::fromLatin1(modifier::kShift));
    }
    *mods = orderModifiers(found);
    return true;
}

// What to print for the key of one entry. Empty when there is nothing to show.
QString keyText(const QJsonObject &entry) {
    const QString key = entry.value(QLatin1String(kFieldKey)).toString();
    if (!key.isEmpty()) {
        return normalizeKey(key);
    }
    // A submap catchall stands for every key not bound otherwise, which is
    // exactly what the reader of a submap page wants to know.
    if (entry.value(QLatin1String(kFieldCatchAll)).toBool()) {
        return QCoreApplication::translate("SourceHyprland", "any key");
    }
    // "bind = SUPER, code:28, ..." names the key by its kernel code and
    // Hyprland keeps no name for it. The number is all there is, and it is
    // still better than dropping the shortcut.
    const int keycode = entry.value(QLatin1String(kFieldKeycode)).toInt();
    if (keycode != 0) {
        return QCoreApplication::translate("SourceHyprland", "Code %1")
            .arg(keycode);
    }
    return {};
}

// True for the entries that are not keyboard shortcuts: "bindm" sets the flag,
// and a wheel or button bound with plain "bind" names itself "mouse...".
bool isPointerBind(const QJsonObject &entry) {
    return entry.value(QLatin1String(kFieldMouse)).toBool() ||
           entry.value(QLatin1String(kFieldKey))
               .toString()
               .startsWith(QLatin1String(kMousePrefix), Qt::CaseInsensitive);
}

// Asks the running compositor. Empty reply plus a message when there is none.
QByteArray askCompositor(const QString &path, QString *error) {
    // One deadline for the whole exchange. Each wait gets what is left of it,
    // so a compositor that stops answering halfway costs the budget once
    // instead of once per step.
    QDeadlineTimer deadline(kExchangeBudgetMs);
    const auto left = [&deadline]() {
        return static_cast<int>(deadline.remainingTime());
    };

    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(left())) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("SourceHyprland",
                                                 "%1 does not answer: %2")
                         .arg(path, socket.errorString());
        }
        return {};
    }

    socket.write(kRequestBinds);
    if (!socket.waitForBytesWritten(left())) {
        if (error != nullptr) {
            *error = QCoreApplication::translate(
                         "SourceHyprland", "%1 did not take the request: %2")
                         .arg(path, socket.errorString());
        }
        return {};
    }

    // The end of the reply is the compositor closing the connection; the
    // protocol carries no length and no terminator.
    socket.waitForDisconnected(left());
    // Not const: it is returned below, and const would force a copy.
    QByteArray reply = socket.readAll();

    // Judged by the state rather than by what waitForDisconnected returned:
    // a peer that closes before the call is entered is reported as a failure
    // there while having done exactly the right thing.
    if (socket.state() != QLocalSocket::UnconnectedState) {
        // Still holding the connection open, so whatever arrived is a
        // fragment. Handing it to the parser would blame the answer for what
        // the clock did and tell the reader to look for broken JSON.
        if (error != nullptr) {
            *error =
                QCoreApplication::translate(
                    "SourceHyprland", "%1 did not finish answering in time")
                    .arg(path);
        }
        return {};
    }
    if (reply.isEmpty() && error != nullptr) {
        *error = QCoreApplication::translate(
                     "SourceHyprland", "%1 closed the connection without an "
                                       "answer")
                     .arg(path);
    }
    return reply;
}

// Reads a saved reply from disk. Empty plus a message when it cannot be read.
QByteArray readDump(const QString &path, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("SourceHyprland",
                                                 "%1 is not readable: %2")
                         .arg(path, file.errorString());
        }
        return {};
    }
    const QByteArray content = file.readAll();
    // Said here rather than left to the caller: every other unreadable case in
    // this backend names itself, and an empty message would reach the screen
    // as a blank line.
    if (content.isEmpty() && error != nullptr) {
        *error = QCoreApplication::translate("SourceHyprland", "%1 is empty")
                     .arg(path);
    }
    return content;
}

} // namespace

SourceHyprland::SourceHyprland(QString path) : m_dumpPath(std::move(path)) {}

QString SourceHyprland::name() const { return QStringLiteral("Hyprland"); }

QString SourceHyprland::socketPath() {
    const QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
    const QString signature =
        environment.value(QLatin1String(kHyprlandSignatureVar));
    if (signature.isEmpty()) {
        return {};
    }
    QString runtimeDir = environment.value(QLatin1String(kRuntimeDirVar));
    if (runtimeDir.isEmpty()) {
        runtimeDir = QLatin1String(kHyprlandRuntimeDirFallback) +
                     QString::number(::getuid());
    }
    return runtimeDir + QLatin1Char('/') + QLatin1String(kHyprlandSocketDir) +
           QLatin1Char('/') + signature + QLatin1Char('/') +
           QLatin1String(kHyprlandSocketName);
}

// Inputs read() is measured against. The shapes are those of `hyprctl -j
// binds`, whose fields are taken from Hyprland's own emitter; the broken ones
// from asking what a hand-edited dump or a plugin can produce:
//
//   entry (only the deciding fields)                    | result
//   -----------------------------------------------------|---------------------
//   modmask 64, key "T", exec, "ghostty"                | SUPER+T, "ghostty"
//   modmask 65, key "T"                                 | SUPER+SHIFT+T
//   modmask 76, key "T"                                 | SUPER+CTRL+ALT+T
//   modmask 0, key "XF86AudioRaiseVolume"               | key without modifier
//   has_description, description "Open terminal"        | the description wins
//   description set but has_description false           | ignored, derived text
//   killactive, arg ""                                  | "Close window"
//   movefocus, arg "l"                                  | "Focus left"
//   movefocus, arg "left"                               | same, only the first
//                                                       |   character counts
//   movewindow, arg "mon:DP-1"                          | not a direction, raw
//   workspace, arg "e+1"                                | "Workspace e+1"
//   workspace, arg ""                                   | "workspace", no "%1"
//   unknown dispatcher "myplugin", arg "x"              | "myplugin x"
//   dispatcher "__lua", arg "12"                        | "Lua action" and
//                                                       |   counted: a Lua
//                                                       |   configuration keeps
//                                                       |   the real dispatcher
//                                                       |   to itself
//   the same with a description                         | the description wins
//   no dispatcher and no argument either                | the key, because the
//                                                       |   contract promises
//                                                       |   a description
//   submap "resize"                                     | group "resize"
//   submap ""                                           | group "Other"
//   mouse true, key "mouse:272"                         | dropped, not a key
//   key "mouse_down" with plain bind                    | dropped as well
//   key "", keycode 28                                  | "Code 28"
//   key "", catch_all true                              | "any key"
//   key "", keycode 0, catch_all false                  | skipped, counted
//   modmask 128 (MOD5)                                  | skipped, counted:
//                                                       |   no name for it here
//   modmask 256 (nothing known has that bit)            | skipped as well
//   modmask "64" as a string, or null, or true          | skipped: read as 0 it
//                                                       |   would turn into an
//                                                       |   unmodified bind
//   no modmask field at all                             | no modifier, which is
//                                                       |   what an omitted
//                                                       |   number means
//   entry is not an object                              | skipped, counted
//   nothing but skipped entries                         | empty list, and the
//                                                       |   counts say why
//   reply "[]"                                          | empty list + message
//   reply is an object, not a list                      | empty list + message
//   reply is not JSON at all                            | empty list + message
//   no instance running / signature unset               | empty list + message
//   socket there but nothing behind it                  | empty list + message
//   reply stops halfway and the socket stays open       | empty list, and the
//                                                       |   message names the
//                                                       |   time, not the JSON
//   connection accepted, then closed without a word     | empty list + message,
//                                                       |   said apart from the
//                                                       |   one above: nothing
//                                                       |   was cut short here
//   the request itself cannot be handed over            | empty list + message.
//                                                       |   No test: it needs
//                                                       |   the peer to go away
//                                                       |   between accept and
//                                                       |   flush, which a
//                                                       |   request this small
//                                                       |   cannot be made to
//                                                       |   do on demand
//   dump file exists but is empty                       | empty list + message
QList<Bind> SourceHyprland::read(QString *error) const {
    QList<Bind> binds;

    const bool fromDump = !m_dumpPath.isEmpty();
    const QString origin = fromDump ? m_dumpPath : socketPath();
    if (origin.isEmpty()) {
        if (error != nullptr) {
            *error = QCoreApplication::translate(
                         "SourceHyprland",
                         "No running Hyprland found: %1 is not set")
                         .arg(QLatin1String(kHyprlandSignatureVar));
        }
        return binds;
    }

    QString fetchError;
    const QByteArray payload = fromDump ? readDump(origin, &fetchError)
                                        : askCompositor(origin, &fetchError);
    if (payload.isEmpty()) {
        if (error != nullptr) {
            *error = fetchError;
        }
        return binds;
    }

    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("SourceHyprland",
                                                 "%1: not valid JSON (%2)")
                         .arg(origin, parseError.errorString());
        }
        return binds;
    }
    if (!document.isArray()) {
        if (error != nullptr) {
            *error =
                QCoreApplication::translate(
                    "SourceHyprland", "%1: the reply is not a list of binds")
                    .arg(origin);
        }
        return binds;
    }

    int invalid = 0;
    int foreign = 0;
    int unnamed = 0;

    const QJsonArray entries = document.array();
    // `auto`, not QJsonValue: the iterator hands out a QJsonValueConstRef,
    // and naming the value type would copy every entry to bind the reference.
    for (const auto &value : entries) {
        if (!value.isObject()) {
            ++invalid;
            continue;
        }
        const QJsonObject entry = value.toObject();
        if (isPointerBind(entry)) {
            continue;
        }

        // A modmask of the wrong type would read as 0, which turns the bind
        // into an unmodified one; the overlay matches the held modifiers
        // exactly, so such an entry would silently never appear. An absent
        // field is a different thing and means no modifier, which is how an
        // omitted number reads everywhere else.
        const QJsonValue modmask = entry.value(QLatin1String(kFieldModmask));
        if (!modmask.isUndefined() && !modmask.isDouble()) {
            ++invalid;
            continue;
        }

        Bind bind;
        if (!decodeModifiers(static_cast<std::uint32_t>(modmask.toInteger()),
                             &bind.modifiers)) {
            ++foreign;
            continue;
        }

        bind.key = keyText(entry);
        if (bind.key.isEmpty()) {
            ++invalid;
            continue;
        }

        // The description is only meant when the bind was written as "bindd";
        // Hyprland says so with its own flag rather than by leaving the field
        // empty, so the flag is what decides.
        const QString written =
            entry.value(QLatin1String(kFieldDescription)).toString().trimmed();
        const bool hasDescription =
            entry.value(QLatin1String(kFieldHasDescription)).toBool();
        const QString dispatcher =
            entry.value(QLatin1String(kFieldDispatcher)).toString();
        if (hasDescription && !written.isEmpty()) {
            bind.description = written;
        } else if (dispatcher == QLatin1String(kDispatcherLua)) {
            // There is nothing to derive from, see kDispatcherLua. Shown as
            // what it is and counted, because a configuration written in Lua
            // reads the same on every line otherwise and nothing would say
            // where the names are missing from.
            bind.description =
                QCoreApplication::translate("SourceHyprland", "Lua action");
            ++unnamed;
        } else {
            bind.description = deriveDescription(
                dispatcher,
                entry.value(QLatin1String(kFieldArg)).toString().trimmed());
        }
        if (bind.description.isEmpty()) {
            // An entry with neither dispatcher nor argument, which only a
            // plugin or a hand-edited dump produces. The contract promises a
            // description, and the key is the best answer left.
            bind.description = bind.key;
        }

        // A submap is a mode of its own: those binds only work while it is
        // active, so its name is the heading they belong under. Hyprland
        // groups nothing else, and inventing a grouping from the dispatcher
        // would put a heading on screen the user never wrote.
        const QString submap =
            entry.value(QLatin1String(kFieldSubmap)).toString().trimmed();
        bind.group = submap.isEmpty() ? defaultGroupName() : submap;

        binds.append(bind);
    }

    if (error != nullptr) {
        QStringList notes;
        // Said first, and never instead of the counts: an answer that yielded
        // nothing but skipped entries has to name them, or the reader is told
        // there are no shortcuts when the truth is that none could be shown.
        if (binds.isEmpty()) {
            notes.append(QCoreApplication::translate(
                "SourceHyprland", "holds no keyboard shortcut"));
        }
        if (invalid > 0) {
            notes.append(QCoreApplication::translate(
                "SourceHyprland", "skipped %n unreadable entry/entries",
                nullptr, invalid));
        }
        if (foreign > 0) {
            notes.append(QCoreApplication::translate(
                "SourceHyprland",
                "skipped %n shortcut(s) needing a modifier the panel "
                "cannot follow",
                nullptr, foreign));
        }
        if (unnamed > 0) {
            // Says where the name would come from, because there is nothing
            // wrong here to fix: the reply is complete, the Lua configuration
            // simply never wrote one down.
            notes.append(QCoreApplication::translate(
                "SourceHyprland",
                "%n shortcut(s) from the Lua configuration carry no name; "
                "hl.bind takes description = \"...\"",
                nullptr, unnamed));
        }
        if (!notes.isEmpty()) {
            *error = QStringLiteral("%1: %2").arg(
                origin, notes.join(QLatin1String(kNoteSeparator)));
        }
    }
    return binds;
}

} // namespace bindpeek
