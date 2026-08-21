// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Source.h"

#include <QCoreApplication>
#include <QHash>

#include <utility>

namespace bindpeek {

Source::~Source() = default;

const QStringList &modifierOrder() {
    // Display order. SUPER comes first because it is the main modifier in all
    // three environments and holds the largest group.
    static const QStringList order = {
        QString::fromLatin1(modifier::kSuper),
        QString::fromLatin1(modifier::kCtrl),
        QString::fromLatin1(modifier::kAlt),
        QString::fromLatin1(modifier::kShift),
    };
    return order;
}

// Inputs normalizeModifier() is measured against:
//
//   input          | result | origin
//   ---------------|--------|---------------------------------------------
//   "SUPER"        | SUPER  | mango bind.conf
//   "super"        | SUPER  | lower case, mango accepts it
//   "Meta"         | SUPER  | KDE kglobalshortcutsrc
//   "meta"         | SUPER  | same
//   "Mod4"/"MOD4"  | SUPER  | X11 spelling, shows up in foreign configs
//   "Win"/"Logo"   | SUPER  | other common names for the same key
//   "CTRL"/"Ctrl"  | CTRL   |
//   "Control"      | CTRL   | Qt spelling
//   "ALT"/"Alt"    | ALT    |
//   "Mod1"         | ALT    | X11
//   "SHIFT"/"Shift"| SHIFT  |
//   "t"            | ""     | not a modifier, so it is the key
//   "Volume Down"  | ""     | KDE media key
//   ""             | ""     | empty field, for example "bind=,t,..."
//   "  Meta  "     | SUPER  | surrounding blanks
QString normalizeModifier(const QString &raw) {
    static const QHash<QString, QString> table = {
        {QStringLiteral("super"), QString::fromLatin1(modifier::kSuper)},
        {QStringLiteral("meta"), QString::fromLatin1(modifier::kSuper)},
        {QStringLiteral("mod4"), QString::fromLatin1(modifier::kSuper)},
        {QStringLiteral("win"), QString::fromLatin1(modifier::kSuper)},
        {QStringLiteral("logo"), QString::fromLatin1(modifier::kSuper)},
        {QStringLiteral("ctrl"), QString::fromLatin1(modifier::kCtrl)},
        {QStringLiteral("control"), QString::fromLatin1(modifier::kCtrl)},
        {QStringLiteral("alt"), QString::fromLatin1(modifier::kAlt)},
        {QStringLiteral("mod1"), QString::fromLatin1(modifier::kAlt)},
        {QStringLiteral("shift"), QString::fromLatin1(modifier::kShift)},
    };
    return table.value(raw.trimmed().toLower());
}

QStringList orderModifiers(const QStringList &mods) {
    QStringList ordered;
    for (const QString &canonical : modifierOrder()) {
        if (mods.contains(canonical)) {
            ordered.append(canonical);
        }
    }
    return ordered;
}

// How the key whose character is the separator is written out instead.
constexpr char kPlusKeyName[] = "Plus";

// The keys a configuration names with a word while the keyboard shows a
// character or a symbol. "SUPER+/" is read at a glance; "SUPER+Slash" has to
// be translated back into the key first, and the key is right there under the
// hand.
//
// The keys that print nothing carry the symbol the keycaps use: the blank of
// U+2423 for the space bar, the turning arrow for return, the barred arrow for
// tab. Those live outside the range every font covers, which is why the panel
// picks a family that has them rather than leaving the choice to a fallback;
// Theme.qml says so at the candidate list.
//
// Names with no agreed symbol, Escape and Delete among them, keep their word.
// An arrow nobody recognizes is worse than the word it replaced.
const QHash<QString, QString> &printableKeys() {
    static const QHash<QString, QString> table = {
        // The one key whose own character is the separator between the
        // modifiers, which is why it is the one shown by its name. "SUPER++"
        // cannot be read: nothing tells the reader which of the two joins the
        // keys and which is the key, and a cheat sheet that has to be worked
        // out has failed at the one thing it does. Both spellings arrive
        // here, the name from one configuration and the character from
        // another, and both leave as the word. The character is taken from
        // the separator itself, so the two cannot come apart.
        // Both spellings, and the second is not the formality it looks
        // like. The table is asked in lower case, so every key named here is
        // answered however a configuration happens to write it; the
        // capitalisation at the end of the function is not, it only raises
        // the first letter and leaves "PLUS" as it found it. Measured: with
        // this line "PLUS" comes out as the word, without it as itself.
        {QStringLiteral("plus"), QLatin1String(kPlusKeyName)},
        {QLatin1String(kShortcutSeparator), QLatin1String(kPlusKeyName)},
        {QStringLiteral("slash"), QStringLiteral("/")},
        {QStringLiteral("backslash"), QStringLiteral("\\")},
        {QStringLiteral("equal"), QStringLiteral("=")},
        {QStringLiteral("minus"), QStringLiteral("-")},
        {QStringLiteral("underscore"), QStringLiteral("_")},
        {QStringLiteral("comma"), QStringLiteral(",")},
        {QStringLiteral("period"), QStringLiteral(".")},
        {QStringLiteral("semicolon"), QStringLiteral(";")},
        {QStringLiteral("colon"), QStringLiteral(":")},
        {QStringLiteral("apostrophe"), QStringLiteral("'")},
        {QStringLiteral("quotedbl"), QStringLiteral("\"")},
        {QStringLiteral("grave"), QStringLiteral("`")},
        {QStringLiteral("asciitilde"), QStringLiteral("~")},
        {QStringLiteral("asciicircum"), QStringLiteral("^")},
        {QStringLiteral("bracketleft"), QStringLiteral("[")},
        {QStringLiteral("bracketright"), QStringLiteral("]")},
        {QStringLiteral("braceleft"), QStringLiteral("{")},
        {QStringLiteral("braceright"), QStringLiteral("}")},
        {QStringLiteral("parenleft"), QStringLiteral("(")},
        {QStringLiteral("parenright"), QStringLiteral(")")},
        {QStringLiteral("less"), QStringLiteral("<")},
        {QStringLiteral("greater"), QStringLiteral(">")},
        {QStringLiteral("bar"), QStringLiteral("|")},
        {QStringLiteral("exclam"), QStringLiteral("!")},
        {QStringLiteral("question"), QStringLiteral("?")},
        {QStringLiteral("at"), QStringLiteral("@")},
        {QStringLiteral("numbersign"), QStringLiteral("#")},
        {QStringLiteral("dollar"), QStringLiteral("$")},
        {QStringLiteral("percent"), QStringLiteral("%")},
        {QStringLiteral("ampersand"), QStringLiteral("&")},
        {QStringLiteral("asterisk"), QStringLiteral("*")},

        // The keys that print nothing.
        {QStringLiteral("space"), QStringLiteral("␣")},
        {QStringLiteral("return"), QStringLiteral("↵")},
        {QStringLiteral("enter"), QStringLiteral("↵")},
        {QStringLiteral("kp_enter"), QStringLiteral("↵")},
        {QStringLiteral("tab"), QStringLiteral("⇥")},
        {QStringLiteral("backspace"), QStringLiteral("⌫")},
        {QStringLiteral("left"), QStringLiteral("←")},
        {QStringLiteral("right"), QStringLiteral("→")},
        {QStringLiteral("up"), QStringLiteral("↑")},
        {QStringLiteral("down"), QStringLiteral("↓")},
        // Written out rather than drawn. The characters keyboards use for
        // these two, U+21DE and U+21DF, are arrows with a stroke through
        // them that nobody reads as a page. X11 calls the same keys "Prior"
        // and "Next", which says even less, so both spellings end up here.
        {QStringLiteral("prior"), QStringLiteral("PgUp")},
        {QStringLiteral("page_up"), QStringLiteral("PgUp")},
        {QStringLiteral("next"), QStringLiteral("PgDn")},
        {QStringLiteral("page_down"), QStringLiteral("PgDn")},
    };
    return table;
}

// Inputs normalizeKey() is measured against:
//
//   input          | result       | origin
//   ---------------|--------------|-----------------------------------------
//   "t"            | "T"          | mango, single letter
//   "1"            | "1"          | digit stays
//   "Left"         | "←"          | an arrow key, whatever its spelling
//   "slash"        | "/"          | mango: the key shows a character, so
//                  |              |   the character is what is shown
//   "Return"       | "↵"          | the key prints nothing, its cap says it
//   "L"            | "L"          | KDE
//   "Volume Down"  | "Volume Down"| KDE media key, blank stays
//   "F10"          | "F10"        |
//   "+"            | "Plus"       | the separator's own character, so the
//                  |              |   word stands in for it
//   "plus"         | "Plus"       | the same key spelled out
//   "PLUS"         | "Plus"       | and however else it is written: a named
//                  |              |   key is answered in any case
//   "Grave"        | "`"          | any spelling of a named punctuation key
//   "space"        | "␣"          | a key that prints nothing carries the
//                  |              |   symbol from its keycap
//   "escape"       | "Escape"     | no agreed symbol, so the word stays
//   ""             | ""           | empty field, caller drops the entry
//   "  t  "        | "T"          | surrounding blanks
QString normalizeKey(const QString &raw) {
    // Not const: returning it would then copy instead of move.
    QString clean = raw.trimmed();
    if (clean.isEmpty()) {
        return clean;
    }
    // Not const: returning it would then copy instead of move.
    QString printable = printableKeys().value(clean.toLower());
    if (!printable.isEmpty()) {
        return printable;
    }
    // Only the first letter is raised. A phrase such as "Volume Down" is left
    // alone otherwise, because KDE displays it exactly that way.
    return clean.left(1).toUpper() + clean.mid(1);
}

QString defaultGroupName() {
    return QCoreApplication::translate("Source", "Other");
}

QString shortcutText(const Bind &bind) {
    QStringList parts = bind.modifiers;
    parts.append(bind.key);
    return parts.join(QString::fromLatin1(kShortcutSeparator));
}

// Inputs bindMatchesHeld() is measured against:
//
//   shortcut          | held         | result
//   ------------------|--------------|-------------------------------------
//   SUPER+T           | SUPER        | shown, nothing missing: it fires now
//   SUPER+CTRL+T      | SUPER        | shown, CTRL missing
//   SUPER+CTRL+SHIFT+T| SUPER        | shown, CTRL and SHIFT missing
//   SUPER+CTRL+T      | CTRL+SUPER   | shown, nothing missing: the order the
//                     |              |   hand took is not the shortcut
//   SUPER+T           | SUPER+CTRL   | hidden: CTRL is not part of it, so it
//                     |              |   cannot fire from here
//   CTRL+T            | SUPER        | hidden, same reason
//   T (no modifier)   | SUPER        | hidden
//   anything          | nothing held | hidden
bool bindMatchesHeld(const QStringList &modifiers, const QStringList &held,
                     QStringList *missing) {
    // Cleared before anything can return: a caller that hoists the list out of
    // its loop to save the allocation would otherwise keep the modifiers of
    // the last shortcut that did match and print them in front of the wrong
    // key.
    if (missing != nullptr) {
        missing->clear();
    }
    if (held.isEmpty()) {
        return false;
    }
    for (const QString &name : held) {
        if (!modifiers.contains(name)) {
            return false;
        }
    }
    if (missing != nullptr) {
        QStringList rest;
        // Walked in display order rather than in the shortcut's own, so what
        // is missing reads the same wherever it is printed.
        for (const QString &name : modifierOrder()) {
            if (modifiers.contains(name) && !held.contains(name)) {
                rest.append(name);
            }
        }
        *missing = rest;
    }
    return true;
}

bool HeldModifiers::press(const QString &name) {
    if (m_names.contains(name)) {
        // Already down on another key or another keyboard. Moving it to the
        // end would rewrite a heading the user is reading.
        return false;
    }
    m_names.append(name);
    return true;
}

bool HeldModifiers::release(const QString &name) {
    return m_names.removeAll(name) > 0;
}

bool HeldModifiers::reconcile(const QSet<QString> &actual) {
    QStringList rebuilt;
    for (const QString &name : std::as_const(m_names)) {
        if (actual.contains(name)) {
            rebuilt.append(name);
        }
    }
    for (const QString &name : modifierOrder()) {
        if (actual.contains(name) && !rebuilt.contains(name)) {
            rebuilt.append(name);
        }
    }
    if (rebuilt == m_names) {
        return false;
    }
    m_names = rebuilt;
    return true;
}

const QStringList &HeldModifiers::names() const { return m_names; }

bool HeldModifiers::isEmpty() const { return m_names.isEmpty(); }

QList<BindGroupPositions> groupBindPositions(const QList<Bind> &binds) {
    QList<BindGroupPositions> groups;
    // Where each heading sits in `groups`, so a bind does not walk the list to
    // find its own.
    QHash<QString, qsizetype> position;

    for (qsizetype at = 0; at < binds.size(); ++at) {
        const QString &name = binds.at(at).group;
        if (!position.contains(name)) {
            position.insert(name, groups.size());
            groups.append(BindGroupPositions{name, {}});
        }
        groups[position.value(name)].at.append(at);
    }
    return groups;
}

QList<BindGroup> groupBinds(const QList<Bind> &binds) {
    const QList<BindGroupPositions> found = groupBindPositions(binds);
    QList<BindGroup> groups;
    groups.reserve(found.size());
    for (const BindGroupPositions &group : found) {
        BindGroup filled{group.name, {}};
        filled.binds.reserve(group.at.size());
        for (const qsizetype at : group.at) {
            filled.binds.append(binds.at(at));
        }
        groups.append(std::move(filled));
    }
    return groups;
}

QStringList splitModifiers(const QString &raw, QChar separator,
                           QStringList *unknown) {
    QStringList found;
    const QStringList parts = raw.split(separator, Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString canonical = normalizeModifier(part);
        if (canonical.isEmpty()) {
            if (unknown != nullptr) {
                unknown->append(part.trimmed());
            }
        } else {
            found.append(canonical);
        }
    }
    return orderModifiers(found);
}

} // namespace bindpeek
