// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Source.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <memory>

namespace bindpeek {

class KeyboardWatch;

// State behind the overlay. It owns the shortcut list, follows the modifiers
// the keyboard watch reports and decides when the panel is on screen.
//
// The behaviour it implements, in the spirit of which-key:
//
//   hold SUPER          -> after a short delay the SUPER shortcuts appear
//   add SHIFT           -> the view switches to SUPER+SHIFT at once
//   press a normal key  -> the panel goes away; the shortcut was taken
//   change the modifiers-> a new question, so the panel comes back
//   release the modifier-> the panel goes away
//
// The delay matters. Without it every deliberate shortcut would make the panel
// flash for the fraction of a second between pressing the modifier and the
// key, which is noise rather than help.
class OverlayController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY viewChanged)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY viewChanged)
    // What each further modifier would still reach, for the footer.
    Q_PROPERTY(QVariantList continuations READ continuations NOTIFY viewChanged)
    Q_PROPERTY(QString message READ message NOTIFY viewChanged)
    Q_PROPERTY(bool panelVisible READ isPanelVisible NOTIFY panelVisibleChanged)
    // Whether a panel is being got ready: a combination is held and the wait
    // before showing it is running.
    //
    // The surface is created and left to the compositor during that wait,
    // drawing nothing. Which output it lands on is what decides how large the
    // panel may be, and Qt names the wrong one until the compositor has
    // answered. Drawing at that size and correcting it a moment later is
    // exactly the flicker this is here to prevent, and the wait is time that
    // is being spent anyway.
    Q_PROPERTY(bool panelPending READ isPanelPending NOTIFY panelPendingChanged)

public:
    // showDelayMs comes from the settings; see Settings::showDelayMs().
    OverlayController(std::unique_ptr<Source> source, KeyboardWatch *watch,
                      int showDelayMs, QObject *parent = nullptr);
    ~OverlayController() override;

    // Reads the source afresh. Called once at startup and again whenever the
    // panel is about to be shown, so an edited configuration is picked up
    // without restarting anything.
    bool reload();

    // Takes changed settings while running, so the editor live saving reaches
    // the panel without a restart.
    void setShowDelayMs(int value);

    // Whether the panel answers with everything the held keys can still reach
    // or only with what fires this instant. See Settings::showsDeeper().
    void setShowsDeeper(bool value);

    // See Settings::ignoreLoneShift().
    void setIgnoreLoneShift(bool value);

    // Whether the groups are headed by the combination their shortcuts want
    // rather than by the heading the session gave them. See
    // Settings::arrangesByModifier().
    void setArrangesByModifier(bool value);

    QString subtitle() const;
    QVariantList groups() const;
    QVariantList continuations() const;
    QString message() const;
    bool isPanelVisible() const;
    bool isPanelPending() const;

signals:
    void viewChanged();
    void panelVisibleChanged();
    void panelPendingChanged();

private slots:
    void onHeldChanged(const QStringList &held);
    void onShortcutTaken();
    void onDelayElapsed();

private:
    // Whether the held combination is one the panel answers at all.
    bool answersHeld() const;

    void setPanelVisible(bool visible);

    // The wait before showing is started and stopped in four places and read
    // by the window, so neither is done to the timer directly: a stop that
    // forgets to announce itself leaves a surface standing with nothing on it.
    void startDelay();
    void stopDelay();
    void rebuild();
    void rebuildContinuations(const QList<Bind> &visible);

    std::unique_ptr<Source> m_source;
    QList<Bind> m_binds;
    QString m_message;

    QStringList m_held;
    QVariantList m_groups;
    QVariantList m_continuations;

    // Runs between the modifier going down and the panel appearing.
    QTimer m_delay;

    // Set once a normal key was pressed while modifiers were held. Cleared
    // when the last modifier is released, and also when the combination
    // changes: keeping it would swallow the answer to a question the user has
    // just asked. It only keeps the panel from popping back up between two
    // shortcuts typed under one long press of the same combination.
    bool m_suppressed = false;

    // Both from the settings; see the setters above.
    bool m_showsDeeper = true;
    bool m_ignoreLoneShift = true;
    bool m_arrangesByModifier = false;

    bool m_panelVisible = false;
};

} // namespace bindpeek
