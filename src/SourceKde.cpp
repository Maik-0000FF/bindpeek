// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SourceKde.h"

#include <KConfig>
#include <KConfigGroup>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace bindpeek {
namespace {

// The file KDE keeps its global shortcuts in.
constexpr char kFileName[] = "kglobalshortcutsrc";

// Key under which KDE stores the translated name of a group ("[kwin]" ->
// "Window Management"). Not a shortcut but the heading.
constexpr char kKeyFriendlyName[] = "_k_friendly_name";

// Value with which KDE marks an unassigned shortcut.
constexpr char kUnassigned[] = "none";

// KDE joins several shortcuts of one action with a tab ("Ctrl+F9\tMeta+F9").
// Both are assigned and both belong on screen, otherwise one of them is
// missing from its modifier group.
constexpr QChar kShortcutSeparatorKde = QLatin1Char('\t');

// Field order in "Name=Active,Default,Description".
constexpr int kFieldActive = 0;
constexpr int kFieldDescription = 2;

// Splits a single KDE shortcut ("Meta+Alt+L") into modifiers and key. Returns
// false when nothing displayable is left.
bool splitShortcut(const QString &raw, Bind *bind) {
    const QStringList parts = raw.split(QLatin1Char('+'), Qt::KeepEmptyParts);

    // Collect the leading modifiers. Everything from the first non-modifier on
    // is the key, joined with "+" again: that way "Meta++" survives, where the
    // key itself is a plus sign.
    QStringList mods;
    int firstKeyPart = 0;
    while (firstKeyPart < parts.size()) {
        const QString canonical = normalizeModifier(parts.at(firstKeyPart));
        if (canonical.isEmpty()) {
            break;
        }
        mods.append(canonical);
        ++firstKeyPart;
    }

    QString key = parts.mid(firstKeyPart).join(QLatin1Char('+'));
    if (key.isEmpty()) {
        // A pure modifier shortcut, for example "Meta" for the launcher. The
        // last modifier is the key then.
        if (mods.isEmpty()) {
            return false;
        }
        key = mods.takeLast();
    }

    bind->modifiers = orderModifiers(mods);
    bind->key = normalizeKey(key);
    return !bind->key.isEmpty();
}

} // namespace

SourceKde::SourceKde(QString path)
    : m_path(path.isEmpty() ? defaultPath() : std::move(path)) {}

QString SourceKde::name() const { return QStringLiteral("KDE"); }

// Asked of the standard locations rather than built out of the home directory.
//
// The file belongs to the desktop, and the desktop puts its configuration
// where XDG_CONFIG_HOME says, which is not always below the home directory. A
// path assembled here would miss it there, and miss it silently: the reader
// would report a file that is not present while KDE is plainly using one.
//
// The writable location answers when nothing is found, so the message names
// the place a reader would go looking.
QString SourceKde::defaultPath() {
    // Not const: it is returned below, and const would force a copy.
    QString found = QStandardPaths::locate(
        QStandardPaths::GenericConfigLocation, QLatin1String(kFileName));
    if (!found.isEmpty()) {
        return found;
    }
    return QStandardPaths::writableLocation(
               QStandardPaths::GenericConfigLocation) +
           QLatin1Char('/') + QLatin1String(kFileName);
}

// Inputs read() is measured against (every accepted shape is taken from a real
// kglobalshortcutsrc):
//
//   line                                               | result
//   ---------------------------------------------------|----------------------
//   [kwin]                                             | new group
//   _k_friendly_name=Window Management                 | group name, no bind
//   Foo=Meta+Alt+L,Meta+Alt+L,Switch to last layout    | SUPER+ALT+L
//   Foo=none,none,Switch to activity                   | unassigned, dropped
//   Lock Session=Screensaver\tCtrl+Alt+L,,             | 2 shortcuts, no text
//                                                      |   -> fallback: the key
//   mic_mute=Microphone Mute\tMeta+Volume Mute,...     | 2 shortcuts, one with
//                                                      |   a blank in its name
//   Foo=Meta+Ctrl+A,...,Activate window demanding\\,.. | escaped comma stays
//                                                      |   inside the text
//   increase_volume=Volume Up,Volume Up,Raise volume   | key without modifier
//   ...=Shift+Volume Down,...                          | SHIFT + "Volume Down"
//   ...=Meta,...                                       | plain Meta, key=SUPER
//   field 1 empty (user cleared it)                    | unassigned, dropped
//   only 2 fields instead of 3                         | no description
//                                                      |   -> fallback: the key
//   file missing / unreadable                          | empty list + message
//   binary garbage                                     | empty list + message
//
// The double escaping (KConfig masks the list comma first, then the INI layer
// masks the backslash) is deliberately not reimplemented but left to KConfig:
// it is the very library KDE writes the file with.
QList<Bind> SourceKde::read(QString *error) const {
    QList<Bind> binds;

    if (!QFileInfo::exists(m_path)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate("SourceKde", "%1 not found")
                         .arg(m_path);
        }
        return binds;
    }

    // SimpleConfig: no cascade over system and global files, exactly this one
    // file is read.
    KConfig config(m_path, KConfig::SimpleConfig);

    int invalid = 0;

    // groupList() promises no fixed order. Sorting by section id keeps the
    // display identical on every open.
    QStringList groups = config.groupList();
    groups.sort();

    for (const QString &groupId : std::as_const(groups)) {
        const KConfigGroup group = config.group(groupId);
        const QString groupName = group.readEntry(kKeyFriendlyName, groupId);

        const QStringList keys = group.keyList();
        for (const QString &name : keys) {
            if (name == QLatin1String(kKeyFriendlyName)) {
                continue;
            }

            const QStringList fields = group.readEntry(name, QStringList());
            const QString active = fields.value(kFieldActive).trimmed();
            if (active.isEmpty() || active == QLatin1String(kUnassigned)) {
                continue;
            }

            QString description = fields.value(kFieldDescription).trimmed();
            if (description.isEmpty()) {
                // Without translated text the key itself is the best available
                // answer, for example "Lock Session".
                description = name;
            }

            const QStringList shortcuts =
                active.split(kShortcutSeparatorKde, Qt::SkipEmptyParts);
            for (const QString &single : shortcuts) {
                const QString clean = single.trimmed();
                if (clean.isEmpty() || clean == QLatin1String(kUnassigned)) {
                    continue;
                }
                Bind bind;
                if (!splitShortcut(clean, &bind)) {
                    ++invalid;
                    continue;
                }
                bind.description = description;
                bind.group = groupName;
                binds.append(bind);
            }
        }
    }

    if (error != nullptr) {
        if (binds.isEmpty()) {
            *error = QCoreApplication::translate(
                         "SourceKde", "%1 holds no assigned shortcut")
                         .arg(m_path);
        } else if (invalid > 0) {
            *error = QCoreApplication::translate(
                         "SourceKde", "%1: skipped %n unreadable shortcut(s)",
                         nullptr, invalid)
                         .arg(m_path);
        }
    }
    return binds;
}

} // namespace bindpeek
