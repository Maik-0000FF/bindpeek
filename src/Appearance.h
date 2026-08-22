// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Settings.h"
#include "SystemScheme.h"

#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

namespace bindpeek {

// The largest box that fits inside every one of the given screens.
//
// Free of any screen: it takes sizes, so the rule can be
// measured without a display. Sizes that report nothing are skipped rather
// than compared: an output that has appeared but not yet applied its mode says
// it is zero by zero for a moment, and a zero taken as a measurement makes
// every later screen the new minimum. The panel would then be sized for the
// largest screen attached, which is the overflow this measurement exists to
// prevent. The fallback is the answer when nothing measurable is left.
QSize smallestScreenBox(const QList<QSize> &screens, QSize fallback);

// How much of each axis a placement keeps clear of the panel, both ends
// counted. The width is the horizontal reserve, the height the vertical one.
//
// Along the edge a surface spans, the inset is kept at each of its two ends
// and counts twice; across that edge the margin sits on the anchored side
// alone and counts once. A centred surface is anchored to nothing, so it
// keeps nothing clear on either axis.
//
// A free function rather than a step inside apply(): it is the rule that
// decides how large the panel may grow, and a rule that can be measured on
// its own is one that cannot quietly drift from the placement it describes.
QSize reservedPixels(Settings::Position position, int marginPx,
                     int edgeInsetPx);

// What the panel should look like right now, handed to QML as one object.
//
// It exists so the QML never has to know where a value came from: the file,
// the desktop's light/dark setting, or a default. When the desktop switches
// scheme the theme property changes and the panel follows without a restart.
class Appearance : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString theme READ theme NOTIFY themeChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY changed)
    Q_PROPERTY(int fontSizePt READ fontSizePt NOTIFY changed)
    Q_PROPERTY(int cornerRadius READ cornerRadius NOTIFY changed)
    Q_PROPERTY(int borderWidth READ borderWidth NOTIFY changed)
    Q_PROPERTY(double opacity READ opacity NOTIFY changed)
    // What becomes of the shortcuts that need a further modifier: whether
    // they are listed at all, and if they are, how. All three false is the
    // panel that lists only what the held keys fire; the four words that set
    // them are in Settings::knownDisclosures().
    Q_PROPERTY(bool showsDeeper READ showsDeeper NOTIFY changed)
    Q_PROPERTY(bool deeperInSections READ deeperInSections NOTIFY changed)
    Q_PROPERTY(bool showContinuations READ showContinuations NOTIFY changed)
    // Whether a group is headed by the combination its shortcuts want rather
    // than by the heading the session gave them. The panel then names the
    // combination itself, so the key caps that would say the same thing above
    // each run are left off and every row carries its key alone.
    Q_PROPERTY(bool arrangesByModifier READ arrangesByModifier NOTIFY changed)
    // The order the modifiers are named in, which is the order the panel
    // writes them in and the order the line at the foot counts them. Handed
    // over so the preview can sort its made-up list the way the controller
    // sorts the real one, rather than keeping a second opinion about which
    // modifier comes first.
    Q_PROPERTY(QStringList modifierOrder READ modifierOrder CONSTANT)
    // Where the content sits along the axis the compositor stretches. Both
    // false is the middle, which is where a panel that spans nothing also
    // ends up: there is no room to move it in.
    Q_PROPERTY(bool alignsAtStart READ alignsAtStart NOTIFY changed)
    Q_PROPERTY(bool alignsAtEnd READ alignsAtEnd NOTIFY changed)
    // Which axis the compositor sizes, so the QML knows not to set that one
    // itself. Both false means the panel sizes itself in both directions,
    // which is the centre position.
    //
    // They say a second thing as well, because it is the same thing: a panel
    // that takes the whole width is a row of groups, one that takes the whole
    // height is a column of them. The layout follows from the shape.
    Q_PROPERTY(bool spanHorizontal READ spanHorizontal NOTIFY changed)
    Q_PROPERTY(bool spanVertical READ spanVertical NOTIFY changed)
    // How much of each axis is kept clear altogether, ends included; see
    // horizontalReservedPx().
    Q_PROPERTY(
        int horizontalReservedPx READ horizontalReservedPx NOTIFY changed)
    Q_PROPERTY(int verticalReservedPx READ verticalReservedPx NOTIFY changed)
    // How much room the panel may take at most. See screenWidth().
    Q_PROPERTY(int screenWidth READ screenWidth NOTIFY screensChanged)
    Q_PROPERTY(int screenHeight READ screenHeight NOTIFY screensChanged)

public:
    // The scheme reader is passed in rather than created here, so a process
    // with more than one user of it, the editor has a tray icon as well, asks
    // the portal once and cannot end up with two different answers.
    Appearance(const Settings &settings, SystemScheme *systemScheme,
               QObject *parent = nullptr);

    // Takes a new set of values. The editor calls this on every change so its
    // preview is driven by the same class the overlay uses, which is the only
    // way the two can be guaranteed to agree.
    void apply(const Settings &settings);

    // The palette actually in use, after the system scheme has had its say.
    QString theme() const;

    // Empty means QML picks the first installed family from its own list.
    QString fontFamily() const;
    int fontSizePt() const;
    int cornerRadius() const;
    int borderWidth() const;
    double opacity() const;
    bool showsDeeper() const;
    bool deeperInSections() const;
    bool arrangesByModifier() const;
    QStringList modifierOrder() const;
    bool showContinuations() const;
    bool alignsAtStart() const;
    bool alignsAtEnd() const;
    bool spanHorizontal() const;
    bool spanVertical() const;
    // How much of each axis is not available to the panel, counted the way the
    // surface is actually placed rather than by which value was set.
    //
    // Along the edge it spans, the inset is kept at both ends, so twice.
    // Across that edge only the anchored side has a margin, so once. In the
    // centre nothing at all: that surface is anchored to nothing and keeps no
    // distance from anything. LayerPlacement lays out the same answer as
    // margins for the compositor, one edge at a time.
    int horizontalReservedPx() const;
    int verticalReservedPx() const;

    // The size of the smallest screen attached, and with it the largest the
    // panel may become.
    //
    // Not the screen the window says it is on: a layer surface belongs to the
    // output the compositor puts it on, and Qt learns which one only once the
    // surface has been mapped, if the compositor says so at all. A panel
    // measured against the wrong screen is committed at that size, and a
    // surface wider than its output is not cut off by it: the excess is drawn
    // wherever the output next to it happens to be. That is a panel spilling
    // from a portrait screen onto the one beside it.
    //
    // The smallest screen is the one answer that holds whichever output the
    // compositor picks. It costs width on the larger screen, which is the
    // cheaper of the two mistakes.
    int screenWidth() const;
    int screenHeight() const;

signals:
    void themeChanged();
    void changed();
    void screensChanged();

private:
    void updateTheme();

    // Re-measures the screens and follows every one of them, so a monitor
    // plugged in or rotated while the panel runs is taken into account.
    void watchScreens();
    void updateScreenBounds();

    // Kept as plain values rather than a reference to Settings: the settings
    // object is read once at startup and does not outlive the call.
    QString m_manualTheme;
    bool m_followSystemScheme;
    QString m_themeLight;
    QString m_themeDark;
    QString m_theme;

    // Asked instead of QStyleHints, which reports nothing on a plain wlroots
    // session. Not owned: see the constructor.
    SystemScheme *m_systemScheme;

    QString m_fontFamily;
    int m_fontSizePt;
    int m_cornerRadius;
    int m_borderWidth;
    double m_opacity;
    bool m_showsDeeper = false;
    bool m_deeperInSections = false;
    bool m_arrangesByModifier = false;
    bool m_showContinuations = false;
    bool m_alignsAtStart = false;
    bool m_alignsAtEnd = false;
    bool m_spanHorizontal = false;
    bool m_spanVertical = false;
    int m_horizontalReservedPx = 0;
    int m_verticalReservedPx = 0;
    int m_screenWidth;
    int m_screenHeight;
};

} // namespace bindpeek
