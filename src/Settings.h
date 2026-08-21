// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

namespace bindpeek {

// The three ways of showing the shortcuts that need a further modifier.
//
// Named here rather than compared as literals: the file writes one of these
// words, and whoever acts on it, which is Appearance, has to mean the same
// word. knownDisclosures() lists them in the order the editor offers them and
// says what each does.
namespace disclosure {
inline constexpr char kExact[] = "exact";
inline constexpr char kInline[] = "inline";
inline constexpr char kFooter[] = "footer";
inline constexpr char kSections[] = "sections";
} // namespace disclosure

// Where the panel's content sits along the edge it spans. One of these words;
// knownAlignments() lists them in the order the editor offers them.
//
// Deliberately named for the ends of an axis rather than for left and right:
// which axis it applies to is decided by the position. A panel along the top
// or bottom spans the width, so the words mean left, centre and right; one
// along a side spans the height, so they mean top, middle and bottom. One
// setting either way, because a surface only ever spans one axis and a second
// setting for the other would be a word that never applies.
namespace alignment {
inline constexpr char kStart[] = "start";
inline constexpr char kCenter[] = "center";
inline constexpr char kEnd[] = "end";
} // namespace alignment

// User settings, read from ~/.config/bindpeek/bindpeek.conf.
//
// One file, read here and written by the editor, so a setting never exists
// twice. A missing file is not an error: every value falls back to its default
// and the program works without any configuration at all. Anything unusable is
// reported through warnings() instead of being corrected in silence, because a
// setting that quietly does nothing is worse than one that complains.
class Settings {
public:
    // Where the panel sits on screen.
    enum class Position {
        Center, // floating in the middle
        Left,
        Right,
        Top,
        Bottom, // flush against that edge of the screen
    };

    explicit Settings(QString path = QString());

    // --- Behaviour --------------------------------------------------------

    // How long a modifier has to be held before the panel appears.
    int showDelayMs() const;

    // How the shortcuts that need a further modifier are shown. One of
    // knownDisclosures(); see the words there for what each does.
    QString disclosure() const;

    // The three questions the word above answers, asked as they are meant
    // rather than compared as text at every place that cares. One word, one
    // place that reads it.
    //
    // Whether the panel shows what a further modifier would reach at all;
    // whether those get a segment of their own; and whether a line at the
    // foot counts what each further key is worth.
    bool showsDeeper() const;
    bool deeperInSections() const;
    bool showsContinuations() const;

    // Whether a Shift held on its own is ignored.
    //
    // Shift alone is how capitals are typed, not how a shortcut is looked up,
    // and a panel that comes up in the middle of a sentence is in the way.
    // Shift together with another modifier is a combination like any other
    // and is answered as usual.
    bool ignoreLoneShift() const;

    // Whether the panel is wanted at all.
    //
    // The overlay is a process of its own and the tray brings it up; this is
    // where that decision is kept, so switching it off stays switched off
    // after a logout. The overlay itself never reads it: by the time it runs,
    // the answer was yes.
    bool overlayEnabled() const;

    // --- Placement --------------------------------------------------------

    Position position() const;

    // Distance to the screen edge the panel is anchored to. Ignored in the
    // centre position, which touches no edge.
    int marginPx() const;

    // How far a panel that takes a whole edge stays from the two ends of it.
    // Zero runs it from corner to corner. Ignored in the centre position,
    // which takes no edge.
    //
    // Kept apart from marginPx because the two are different distances: one
    // is how far the panel sits from the edge it is anchored to, the other
    // how far it stops short along that edge.
    int edgeInsetPx() const;

    // Whether the panel is anchored to an edge at all. False only in the
    // centre, which is the one position with no edge to keep a distance from
    // and none to span. Asked here rather than compared against a position
    // name wherever it matters.
    bool anchoredToEdge() const;

    // Where the content sits along the axis the panel spans. One of
    // knownAlignments().
    QString alignment() const;

    // The same answer as the panel needs it: which of the two ends the
    // content is held against, if either. Both false is the middle.
    //
    // Asked this way rather than by comparing the word, so the words live in
    // one place and everything else asks a question.
    //
    // It has no visible effect where the panel is not stretched: a surface
    // that is only as wide as what it holds has nowhere to move it to.
    bool alignsAtStart() const;
    bool alignsAtEnd() const;

    // --- Appearance -------------------------------------------------------

    // Palette to use when the system reports no colour scheme, or when
    // followSystemScheme() is off.
    QString theme() const;

    // Follow the desktop's light/dark setting.
    bool followSystemScheme() const;

    // The two halves used while following the system.
    QString themeLight() const;
    QString themeDark() const;

    // Empty means: pick the first installed family from a built-in list.
    QString fontFamily() const;
    int fontSizePt() const;

    int cornerRadiusPx() const;
    int borderWidthPx() const;
    // Panel background opacity, 0.0 to 1.0. At 0 the plate disappears and only
    // text and border remain.
    double opacity() const;

    // --- Diagnostics ------------------------------------------------------

    // One line per value that had to be ignored. Empty when the file was
    // fully usable.
    QStringList warnings() const;

    // Every palette name the overlay knows.
    static QStringList knownThemes();

    // The accepted range of each value, so the editor can bound its controls
    // with the same numbers the validation uses. One source, no drift.
    struct Range {
        int low;
        int high;
    };
    static Range showDelayRange();
    // How far one step of a dragged control moves the delay. A value in
    // milliseconds is set with a slider, and single milliseconds are noise:
    // nobody can tell 240 from 245 and nobody wants to drag through them.
    static int showDelayStepMs();
    static Range marginRange();
    static Range fontSizeRange();
    static Range cornerRadiusRange();
    static Range borderWidthRange();
    static double opacityLow();
    static double opacityHigh();
    // Same for the opacity, whose range is a single unit wide and therefore
    // needs a step smaller than one.
    static double opacityStep();

    // ~/.config/bindpeek/bindpeek.conf
    static QString defaultPath();

    // Writes the given values back, keeping the file readable: comments,
    // blank lines and the order of the keys survive, because the file is
    // updated line by line rather than rewritten. QSettings would drop every
    // comment, and the comments are what make the file usable by hand.
    //
    // A key that is not in the file yet is appended. Returns false only when
    // the file could not be written.
    bool save(const QString &path = QString()) const;

    // --- Setters, used by the editor --------------------------------------

    void setShowDelayMs(int value);
    void setPosition(Position value);
    void setMarginPx(int value);
    void setEdgeInsetPx(int value);
    void setOverlayEnabled(bool value);
    void setDisclosure(const QString &value);
    void setAlignment(const QString &value);
    void setIgnoreLoneShift(bool value);
    void setTheme(const QString &value);
    void setFollowSystemScheme(bool value);
    void setThemeLight(const QString &value);
    void setThemeDark(const QString &value);
    void setFontFamily(const QString &value);
    void setFontSizePt(int value);
    void setCornerRadiusPx(int value);
    void setBorderWidthPx(int value);
    void setOpacity(double value);

    // Writes a commented template when no file exists yet, so the settings are
    // discoverable without documentation. Never touches an existing file.
    static bool writeTemplateIfMissing(const QString &path = QString());

    // Turns a position back into the word used in the file, for the editor.
    static QString positionName(Position position);
    // The one place a position word becomes a Position. Used when reading the
    // file and by the editor, so the two can never disagree about what "left"
    // means.
    static Position positionFromName(const QString &name);
    static QStringList knownPositions();

    // The three ways of showing what a further modifier would reach:
    //
    //   inline    every shortcut in one list, the ones that need more held
    //             dimmed and with those modifiers written in front of the key
    //   footer    the same, plus a line saying which modifier leads to how
    //             many more
    //   sections  the ones that fire now first, then a block per further
    //             combination under a heading of its own
    static QStringList knownDisclosures();

    // The three places the content can sit along the spanned axis, in the
    // order the editor offers them: against the beginning of that axis, in
    // the middle, against its end.
    static QStringList knownAlignments();

private:
    int m_showDelayMs;
    bool m_overlayEnabled;
    QString m_disclosure;
    QString m_alignment;
    bool m_ignoreLoneShift;
    Position m_position;
    int m_marginPx;
    int m_edgeInsetPx;
    QString m_theme;
    bool m_followSystemScheme;
    QString m_themeLight;
    QString m_themeDark;
    QString m_fontFamily;
    int m_fontSizePt;
    int m_cornerRadiusPx;
    int m_borderWidthPx;
    double m_opacity;
    QStringList m_warnings;
};

// Which axis the compositor stretches a surface along, given where it sits.
//
// Asking for an edge is asking for the whole edge: top and bottom take the
// full width, left and right the full height, and the centre is stretched in
// neither direction. Written once and read by everything that depends on it,
// because a second spelling of the same rule is a second place to change.
//
// Declared beside the positions they are about rather than with the panel's
// look: the placement needs them and has no business reaching for the class
// that feeds the QML to get at them.
bool spansHorizontally(Settings::Position position);
bool spansVertically(Settings::Position position);

} // namespace bindpeek
