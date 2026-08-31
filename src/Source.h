// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// Shared contract of every backend.
//
// Each environment keeps its shortcuts somewhere else (mango: bind.conf,
// Hyprland: hyprctl, KDE: kglobalshortcutsrc). Everything above this contract
// is environment-agnostic: there is exactly one display and filter path, not
// three. A backend delivers normalized entries, nothing more.
namespace bindpeek {

// Note on the translation contexts below and in the .cpp files: the context is
// always spelled out as a literal, never handed in through a constant. lupdate
// is a static extractor and skips every string whose context it cannot read at
// parse time, which silently leaves those strings out of the catalogs.

// Canonical modifier names. The single source for spelling and order: display,
// grouping and comparison use these values only, so that KDE's "Meta" and
// mango's "SUPER" end up looking identical.
namespace modifier {
inline constexpr char kSuper[] = "SUPER";
inline constexpr char kCtrl[] = "CTRL";
inline constexpr char kAlt[] = "ALT";
inline constexpr char kShift[] = "SHIFT";
} // namespace modifier

// Display and sort order of the modifiers.
const QStringList &modifierOrder();

// Separator between modifiers and key in the display ("SUPER+CTRL+T").
inline constexpr char kShortcutSeparator[] = "+";

// Separator between the sentences of one report.
//
// A backend that had to leave several kinds of thing out says so in one line,
// and the tests count the sentences in it by this. Spelled here because three
// places would otherwise spell it: the two backends that report this way, and
// the test that reads what they wrote.
inline constexpr char kNoteSeparator[] = "; ";

// Group name used when a source does not provide one. Translated, so it is a
// function and not a constant.
QString defaultGroupName();

// A single shortcut, independent of the environment it came from.
struct Bind {
    QStringList modifiers; // normalized, sorted by modifierOrder()
    QString key;           // normalized, without modifiers
    QString
        description; // plain text, never empty (backend supplies a fallback)
    QString group;   // section heading of the source
};

// A heading with the binds that belong under it.
struct BindGroup {
    QString name;
    QList<Bind> binds;
};

// Sorts binds under their headings, keeping the order in which the headings
// first occur.
//
// Never by name: the sections of a bind.conf carry meaning in the order they
// were written. And never by comparing each entry only with the one before it,
// which is the obvious way and the wrong one. A backend is free to hand its
// binds out ungrouped, and Hyprland's does, because a submap sits wherever the
// configuration puts it; every interruption would then print the same heading
// again.
//
// Both the text output and the panel go through this, so a list read one way
// cannot be grouped differently from the same list read the other.
QList<BindGroup> groupBinds(const QList<Bind> &binds);

// A heading and the binds under it, each named by where it sits in the list
// that was grouped rather than copied out of it.
struct BindGroupPositions {
    QString name;
    QList<qsizetype> at;
};

// The same grouping, by position.
//
// For the caller that has worked something out about each bind and wants to
// keep it: the panel asks every bind which modifiers it is still missing, and
// a group of copies leaves it no way of telling which answer belonged to
// which. Positions carry that across, so the question is asked once instead of
// once here and once again on the other side of the grouping.
//
// groupBinds() is this function with the binds put back, so the two orders
// cannot come apart.
QList<BindGroupPositions> groupBindPositions(const QList<Bind> &binds);

// Maps a foreign modifier name onto the canonical spelling. Empty string when
// it is not a known modifier, which means it is the key.
QString normalizeModifier(const QString &raw);

// Sorts by modifierOrder() and drops duplicates.
QStringList orderModifiers(const QStringList &mods);

// Unifies key spelling: "t" -> "T", "slash" -> "Slash", "Volume Down" stays.
QString normalizeKey(const QString &raw);

// Display text of the shortcut, for example "SUPER+CTRL+T".
QString shortcutText(const Bind &bind);

// Whether a shortcut belongs on screen while `held` is down, and what it still
// needs before it fires.
//
// A shortcut matches when every held modifier is part of it. Holding SUPER
// therefore also brings up SUPER+CTRL+T, which is the whole point: the panel
// answers "what can I still reach from here" rather than only "what fires this
// instant", and pressing a further modifier narrows that list instead of
// replacing it.
//
// Compared as a set, never in order: CTRL then SUPER reaches the same shortcut
// as SUPER then CTRL, and a panel that disagreed would be wrong about the
// keyboard.
//
// *missing receives what the shortcut needs and the hand does not hold yet, in
// display order. Empty means the combination is exactly the held one, so the
// next key fires it.
//
// Nothing matches while nothing is held: an unmodified shortcut is not the
// answer to a question nobody asked.
bool bindMatchesHeld(const QStringList &modifiers, const QStringList &held,
                     QStringList *missing = nullptr);

// The modifiers held right now, in the order they were pressed.
//
// The order is what the panel writes in its heading: pressing CTRL and then
// SUPER reads as "CTRL+SUPER", because that is what the hand did. It never
// decides which shortcuts are shown; that is a question of the set, and
// bindMatchesHeld answers it.
class HeldModifiers {
public:
    // Both return true when the list changed, which is when there is anything
    // to tell anyone about.
    bool press(const QString &name);
    bool release(const QString &name);

    // Takes the set the devices actually report and rebuilds the list from it.
    //
    // Everything still down keeps the place it was pressed in. A modifier that
    // turns up here without ever having been seen going down, which is what a
    // key event lost under load looks like, has no such place and is appended
    // in display order.
    bool reconcile(const QSet<QString> &actual);

    const QStringList &names() const;
    bool isEmpty() const;

private:
    QStringList m_names;
};

// Splits the modifier part of a shortcut ("Meta+Alt") into the canonical,
// ordered list. Tokens that are not modifiers are appended to *unknown when
// that pointer is given.
QStringList splitModifiers(const QString &raw, QChar separator,
                           QStringList *unknown = nullptr);

class Source {
public:
    virtual ~Source();

    // Display name of the environment, for example "mango".
    virtual QString name() const = 0;

    // Reads the source afresh (on every call, never cached: only that way does
    // the display stay right after a reload_config). Never throws, never
    // guesses.
    //
    // *error is set whenever there is something to report. Empty list plus a
    // message means the source was unreadable. Non-empty list plus a message
    // means single entries were skipped and the rest is valid. Both belong on
    // screen, so that nothing goes missing silently.
    virtual QList<Bind> read(QString *error) const = 0;
};

} // namespace bindpeek
