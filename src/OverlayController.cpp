// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OverlayController.h"

#include "WatchClient.h"

#include <QCoreApplication>
#include <QPair>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace bindpeek {
namespace {

// Keys of the maps handed to QML. Spelled once here, read once in Overlay.qml.
constexpr char kRoleName[] = "name";
constexpr char kRoleEntries[] = "entries";
constexpr char kRoleShortcut[] = "shortcut";
constexpr char kRoleDescription[] = "description";
constexpr char kRoleDeeper[] = "deeper";
constexpr char kRoleSection[] = "section";
constexpr char kRoleCaps[] = "caps";
constexpr char kRoleKey[] = "key";
constexpr char kRoleModifier[] = "modifier";
constexpr char kRoleCount[] = "count";

// One row of the panel while it is being built: what the shortcut is still
// missing, and the shortcut itself.
using Row = QPair<QStringList, Bind>;

// Where a modifier stands in the order the panel names them everywhere else.
int placeOf(const QString &modifier) {
    const qsizetype at = modifierOrder().indexOf(modifier);
    return at < 0 ? static_cast<int>(modifierOrder().size())
                  : static_cast<int>(at);
}

// What fires on the next key first, then what is one modifier away, and so on.
// Counted in modifiers, never in characters of their names: the two agree only
// by accident of the four names in use, and a longer name would put a single
// modifier behind a pair of them.
//
// Then by where those modifiers stand in the display order, so everything
// wanting the same further keys ends up next to each other. Both arrangements
// need that much: one heads a run wherever the combination changes, the other
// cuts a whole group there, and neither can do it unless they are adjacent.
//
// By that order rather than by the alphabet, although the alphabet would sort
// them just as adjacently. Under the arrangement that heads a group with its
// combination, this is what the headings are read in, and the line at the foot
// counts the same modifiers in the display order: two lists of the same four
// words in two different orders, one under the other.
bool nearerFirst(const Row &left, const Row &right) {
    if (left.first.size() != right.first.size()) {
        return left.first.size() < right.first.size();
    }
    for (qsizetype at = 0; at < left.first.size(); ++at) {
        const int here = placeOf(left.first.at(at));
        const int there = placeOf(right.first.at(at));
        if (here != there) {
            return here < there;
        }
    }
    return false;
}

} // namespace

OverlayController::OverlayController(std::unique_ptr<Source> source,
                                     WatchClient *watch, int showDelayMs,
                                     QObject *parent)
    : QObject(parent), m_source(std::move(source)) {
    m_delay.setSingleShot(true);
    m_delay.setInterval(showDelayMs);
    connect(&m_delay, &QTimer::timeout, this,
            &OverlayController::onDelayElapsed);

    connect(watch, &WatchClient::heldChanged, this,
            &OverlayController::onHeldChanged);
    connect(watch, &WatchClient::shortcutTaken, this,
            &OverlayController::onShortcutTaken);
}

OverlayController::~OverlayController() = default;

bool OverlayController::reload() {
    QString error;
    m_binds = m_source->read(&error);
    m_message = error;
    rebuild();
    return !m_binds.isEmpty();
}

void OverlayController::setShowDelayMs(int value) {
    m_delay.setInterval(value);
}

void OverlayController::setShowsDeeper(bool value) {
    if (m_showsDeeper == value) {
        return;
    }
    m_showsDeeper = value;
    rebuild();
}

void OverlayController::setIgnoreLoneShift(bool value) {
    m_ignoreLoneShift = value;
}

void OverlayController::setArrangesByModifier(bool value) {
    if (m_arrangesByModifier == value) {
        return;
    }
    m_arrangesByModifier = value;
    rebuild();
}

// Shift on its own is how capitals are typed, and answering that would put a
// panel on screen several times a sentence. Shift together with anything else
// is a combination like any other.
bool OverlayController::answersHeld() const {
    if (m_held.isEmpty()) {
        return false;
    }
    return !(m_ignoreLoneShift && m_held.size() == 1 &&
             m_held.first() == QLatin1String(modifier::kShift));
}

QString OverlayController::subtitle() const {
    return m_held.join(QString::fromLatin1(kShortcutSeparator));
}

QVariantList OverlayController::groups() const { return m_groups; }

QVariantList OverlayController::continuations() const {
    return m_continuations;
}

QString OverlayController::message() const { return m_message; }

bool OverlayController::isPanelVisible() const { return m_panelVisible; }

bool OverlayController::isPanelPending() const { return m_delay.isActive(); }

void OverlayController::startDelay() {
    const bool was = m_delay.isActive();
    m_delay.start();
    if (!was) {
        emit panelPendingChanged();
    }
}

void OverlayController::stopDelay() {
    if (!m_delay.isActive()) {
        return;
    }
    m_delay.stop();
    emit panelPendingChanged();
}

void OverlayController::onHeldChanged(const QStringList &held) {
    m_held = held;

    if (m_held.isEmpty()) {
        // Every modifier is up: the gesture is over in every case, so the
        // suppression from a taken shortcut ends here too.
        m_suppressed = false;
        stopDelay();
        setPanelVisible(false);
        rebuild();
        return;
    }

    if (!answersHeld()) {
        // Nothing to say about this one. The panel stays down and so does the
        // timer; adding another modifier asks again.
        //
        // Asked before the list is built, not after it. A lone Shift is every
        // capital letter typed, and building the answer costs a walk over
        // every bind, a grouping and a sort per group, twice a letter for a
        // panel that then stays down. What is left standing in the groups is
        // the last answer, and nothing is showing it.
        stopDelay();
        setPanelVisible(false);
        return;
    }

    rebuild();

    // A changed combination is a new question and lifts the suppression: after
    // SUPER+T, adding SHIFT means the user is looking for something else and
    // deserves an answer. Only holding the same combination stays quiet, so
    // typing two shortcuts under one long SUPER does not make the panel blink
    // in between.
    m_suppressed = false;

    if (m_panelVisible) {
        // Already on screen: a changed combination switches the view at once,
        // waiting again would feel like a stutter.
        return;
    }
    startDelay();
}

void OverlayController::onShortcutTaken() {
    if (m_held.isEmpty()) {
        // A key pressed on its own is nothing this panel has to say anything
        // about.
        return;
    }
    m_suppressed = true;
    stopDelay();
    setPanelVisible(false);
}

void OverlayController::onDelayElapsed() {
    // The timer is single shot, so the wait is over either way.
    if (!answersHeld() || m_suppressed) {
        emit panelPendingChanged();
        return;
    }
    // Read the source again right before showing it. An edited bind.conf is
    // then live without restarting anything, which is the whole reason the
    // backends never cache.
    reload();
    // Raised before the wait is announced as over, and the order is not a
    // matter of taste. The window is up while either of the two holds, and
    // each of them is read the moment it is announced. Announcing the end of
    // the wait first leaves an instant in which neither holds: the surface is
    // taken down and built again, and with it goes the one thing the wait was
    // spent on, which is knowing the output it belongs to.
    setPanelVisible(true);
    emit panelPendingChanged();
}

void OverlayController::setPanelVisible(bool visible) {
    if (m_panelVisible == visible) {
        return;
    }
    m_panelVisible = visible;
    emit panelVisibleChanged();
}

// How many shortcuts each single further modifier would keep in view.
//
// One line per modifier, not per combination: the question it answers is what
// the next key press buys, and pressing CTRL reaches the CTRL+SHIFT entries as
// well as the CTRL ones. That is also why the counts overlap and do not add up
// to the number of rows.
void OverlayController::rebuildContinuations(const QList<Bind> &visible) {
    m_continuations.clear();
    // Walked in display order so the footer reads the same way round as every
    // other combination on screen.
    for (const QString &modifier : modifierOrder()) {
        if (m_held.contains(modifier)) {
            continue;
        }
        int reachable = 0;
        for (const Bind &bind : visible) {
            if (bind.modifiers.contains(modifier)) {
                ++reachable;
            }
        }
        if (reachable == 0) {
            continue;
        }
        QVariantMap entry;
        entry.insert(QLatin1String(kRoleModifier), modifier);
        entry.insert(QLatin1String(kRoleCount), reachable);
        m_continuations.append(entry);
    }
}

void OverlayController::rebuild() {
    // Two lists, because two questions are asked of them.
    //
    // Everything the held modifiers can still reach, which is what the footer
    // counts; and of those, the ones the panel lists. Holding SUPER reaches
    // the SUPER+CTRL entries as well, and pressing CTRL narrows that same set;
    // whether they are listed or only counted is what the setting decides.
    QList<Bind> reachable;
    QList<Bind> visible;
    // What each visible bind is still missing, worked out here and carried
    // from here on. The question is answered by the same call that decides
    // whether the bind belongs on the panel at all, so asking it a second
    // time below would be asking something already known.
    QList<QStringList> missingOf;
    for (const Bind &bind : m_binds) {
        QStringList missing;
        if (!bindMatchesHeld(bind.modifiers, m_held, &missing)) {
            continue;
        }
        reachable.append(bind);
        if (m_showsDeeper || missing.isEmpty()) {
            visible.append(bind);
            missingOf.append(std::move(missing));
        }
    }

    // The blocks the panel draws, each a heading and the rows under it. What
    // that heading is, is the one thing the two arrangements disagree about;
    // everything after this point is the same for both.
    QList<QPair<QString, QList<Row>>> blocks;

    if (m_arrangesByModifier) {
        // The headings the session gave these shortcuts are left aside, and
        // the combination each of them wants becomes the heading instead.
        // Sorted first and cut afterwards, which is the opposite of the branch
        // below and for the same reason: here the order of the headings is
        // exactly what is being asked for, nearest first, and the cuts follow
        // from it.
        QList<Row> rows;
        rows.reserve(visible.size());
        for (qsizetype at = 0; at < visible.size(); ++at) {
            rows.append({missingOf.at(at), visible.at(at)});
        }
        // Stable, so the order the source listed them in survives inside each
        // combination: a configuration means the order it was written in.
        std::stable_sort(rows.begin(), rows.end(), nearerFirst);

        for (const Row &row : std::as_const(rows)) {
            QStringList caps = m_held;
            caps.append(row.first);
            const QString name =
                caps.join(QString::fromLatin1(kShortcutSeparator));
            if (blocks.isEmpty() || blocks.last().first != name) {
                blocks.append({name, {}});
            }
            blocks.last().second.append(row);
        }
    } else {
        // Grouped by the same code the text output uses, so the panel and
        // --list cannot disagree about what belongs under which heading.
        //
        // Grouped first and sorted afterwards, never the other way round: the
        // headings follow the order in which they first occur, so sorting the
        // whole list beforehand decides that order too. A section written
        // after another would then jump in front of it merely because one of
        // its shortcuts fires sooner, and where a configuration puts its
        // sections is something it meant.
        // Grouped by position rather than by copy, which is what lets the
        // answers above be carried across the grouping.
        const QList<BindGroupPositions> groups = groupBindPositions(visible);
        blocks.reserve(groups.size());
        for (const BindGroupPositions &group : groups) {
            // Each shortcut of this heading with what it still wants, taken
            // from the pass above rather than worked out again.
            //
            // Carried rather than recomputed because it is wanted three times
            // over: to sort by, to write in front of the key, and to head a
            // section with. The sort alone would ask it twice per comparison,
            // which is a list built and joined for every step of an n log n
            // walk, on every change of a modifier.
            QList<Row> rows;
            rows.reserve(group.at.size());
            for (const qsizetype at : group.at) {
                rows.append({missingOf.at(at), visible.at(at)});
            }
            // Stable, for the reason given in the branch above.
            std::stable_sort(rows.begin(), rows.end(), nearerFirst);
            blocks.append({group.name, rows});
        }
    }

    m_groups.clear();
    m_groups.reserve(blocks.size());
    for (const auto &block : std::as_const(blocks)) {
        const QList<Row> &rows = block.second;
        QVariantList entries;
        entries.reserve(rows.size());
        for (const Row &row : rows) {
            const QStringList &missing = row.first;
            const Bind &bind = row.second;
            // What is still missing is written in front of the key, so the
            // cell reads as the rest of the way there: "CTRL+T" under a held
            // SUPER. Composed here rather than in QML, which then needs to
            // know neither the separator nor the order.
            QStringList text = missing;
            text.append(bind.key);
            QVariantMap entry;
            entry.insert(QLatin1String(kRoleShortcut),
                         text.join(QString::fromLatin1(kShortcutSeparator)));
            entry.insert(QLatin1String(kRoleDescription), bind.description);
            // The one thing the panel styles differently: a shortcut that
            // still wants a key held is not the one about to fire.
            entry.insert(QLatin1String(kRoleDeeper), !missing.isEmpty());
            // The whole combination this shortcut belongs to: what is held,
            // in the order it went down, and what it still wants after that.
            //
            // Drawn as one key cap each by the view that shows segments, and
            // joined into a single string beside it. That string is what tells
            // one segment from the next, and comparing text is the only way to
            // do it: two lists of the same words are not the same object.
            QStringList caps = m_held;
            caps.append(missing);
            entry.insert(QLatin1String(kRoleCaps), caps);
            entry.insert(QLatin1String(kRoleSection),
                         caps.join(QString::fromLatin1(kShortcutSeparator)));
            // The key on its own. Under a segment heading the modifiers are
            // already named above, so repeating them in every row would say
            // the same thing twice.
            entry.insert(QLatin1String(kRoleKey), bind.key);
            entries.append(entry);
        }
        QVariantMap map;
        map.insert(QLatin1String(kRoleName), block.first);
        map.insert(QLatin1String(kRoleEntries), entries);
        m_groups.append(map);
    }

    rebuildContinuations(reachable);

    emit viewChanged();
}

} // namespace bindpeek
