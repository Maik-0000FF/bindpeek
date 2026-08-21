// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Appearance.h"

#include <QGuiApplication>
#include <QScreen>

namespace bindpeek {
namespace {

// Stands in when there is no screen to measure, which happens in a process
// that has none: nothing is drawn then, and the panel only needs a number it
// can do arithmetic with rather than a right one.
constexpr int kNoScreenWidth = 1920;
constexpr int kNoScreenHeight = 1080;

} // namespace

QSize smallestScreenBox(const QList<QSize> &screens, QSize fallback) {
    // Whether anything has been measured yet, kept apart from the numbers: a
    // zero cannot say both "nothing yet" and "this wide" at once.
    bool measured = false;
    QSize smallest;
    for (const QSize &size : screens) {
        if (size.width() <= 0 || size.height() <= 0) {
            continue;
        }
        smallest = measured ? QSize(qMin(smallest.width(), size.width()),
                                    qMin(smallest.height(), size.height()))
                            : size;
        measured = true;
    }
    return measured ? smallest : fallback;
}

QSize reservedPixels(Settings::Position position, int marginPx,
                     int edgeInsetPx) {
    // The centre is anchored to no edge, so neither distance applies to it.
    if (position == Settings::Position::Center) {
        return {0, 0};
    }
    return spansHorizontally(position) ? QSize(edgeInsetPx * 2, marginPx)
                                       : QSize(marginPx, edgeInsetPx * 2);
}

Appearance::Appearance(const Settings &settings, SystemScheme *systemScheme,
                       QObject *parent)
    : QObject(parent), m_systemScheme(systemScheme),
      m_screenWidth(kNoScreenWidth), m_screenHeight(kNoScreenHeight) {
    watchScreens();
    // Follow the desktop while it is running, not just at startup: switching
    // the system to dark should darken the panel too, without restarting it.
    if (m_systemScheme != nullptr) {
        connect(m_systemScheme, &SystemScheme::schemeChanged, this,
                [this]() { updateTheme(); });
    }
    apply(settings);
}

void Appearance::apply(const Settings &settings) {
    m_manualTheme = settings.theme();
    m_followSystemScheme = settings.followSystemScheme();
    m_themeLight = settings.themeLight();
    m_themeDark = settings.themeDark();
    m_fontFamily = settings.fontFamily();
    m_fontSizePt = settings.fontSizePt();
    m_cornerRadius = settings.cornerRadiusPx();
    m_borderWidth = settings.borderWidthPx();
    m_opacity = settings.opacity();

    // The panel asks what to draw, not what the setting is called; Settings
    // turns its one word into the answers.
    m_alignsAtStart = settings.alignsAtStart();
    m_alignsAtEnd = settings.alignsAtEnd();
    m_showsDeeper = settings.showsDeeper();
    m_deeperInSections = settings.deeperInSections();
    m_showContinuations = settings.showsContinuations();

    // A panel put against an edge takes that edge whole: top and bottom are
    // stretched sideways, left and right vertically. Asking for an edge is
    // asking for the edge, so nothing switches this on or off separately.
    // Only the centre is stretched in neither direction.
    const Settings::Position position = settings.position();
    m_spanHorizontal = spansHorizontally(position);
    m_spanVertical = spansVertically(position);

    // Nothing is kept clear in the centre: that surface is anchored to no
    // edge, so neither distance is applied to it. Along a spanned edge the
    // inset is kept at both ends; across it the margin sits on the anchored
    // side alone.
    const QSize reserved =
        reservedPixels(position, settings.marginPx(), settings.edgeInsetPx());
    m_horizontalReservedPx = reserved.width();
    m_verticalReservedPx = reserved.height();

    updateTheme();
    emit changed();
}

// Three steps: the half of the pair the desktop asks for, else the manual
// pick, else whatever the settings validated as the default.
//
// Falling back to the MANUAL pick rather than to one half of the pair matters
// on a desktop that publishes no colour scheme at all: switching the mode on
// there must not cost the user the palette they chose.
void Appearance::updateTheme() {
    QString picked = m_manualTheme;

    if (m_followSystemScheme) {
        const SystemScheme::Scheme scheme = (m_systemScheme != nullptr)
                                                ? m_systemScheme->scheme()
                                                : SystemScheme::Scheme::Unknown;
        if (scheme == SystemScheme::Scheme::Light) {
            picked = m_themeLight;
        } else if (scheme == SystemScheme::Scheme::Dark) {
            picked = m_themeDark;
        }
    }

    if (picked == m_theme) {
        return;
    }
    m_theme = picked;
    emit themeChanged();
}

// Follows the screens themselves, not only their coming and going: a monitor
// that is rotated keeps its identity and only changes its geometry, and a
// portrait screen is exactly the case this measurement exists for.
void Appearance::watchScreens() {
    const auto follow = [this](QScreen *screen) {
        connect(screen, &QScreen::geometryChanged, this,
                [this]() { updateScreenBounds(); });
    };

    const QList<QScreen *> screens = QGuiApplication::screens();
    for (QScreen *screen : screens) {
        follow(screen);
    }
    connect(qApp, &QGuiApplication::screenAdded, this,
            [this, follow](QScreen *screen) {
                follow(screen);
                updateScreenBounds();
            });
    connect(qApp, &QGuiApplication::screenRemoved, this,
            [this](QScreen *) { updateScreenBounds(); });

    updateScreenBounds();
}

void Appearance::updateScreenBounds() {
    QList<QSize> sizes;
    const QList<QScreen *> screens = QGuiApplication::screens();
    sizes.reserve(screens.size());
    for (const QScreen *screen : screens) {
        sizes.append(screen->geometry().size());
    }
    const QSize bounds =
        smallestScreenBox(sizes, QSize(kNoScreenWidth, kNoScreenHeight));

    if (bounds.width() == m_screenWidth && bounds.height() == m_screenHeight) {
        return;
    }
    m_screenWidth = bounds.width();
    m_screenHeight = bounds.height();
    emit screensChanged();
}

QString Appearance::theme() const { return m_theme; }
QString Appearance::fontFamily() const { return m_fontFamily; }
int Appearance::fontSizePt() const { return m_fontSizePt; }
int Appearance::cornerRadius() const { return m_cornerRadius; }
int Appearance::borderWidth() const { return m_borderWidth; }
double Appearance::opacity() const { return m_opacity; }
bool Appearance::alignsAtStart() const { return m_alignsAtStart; }

bool Appearance::alignsAtEnd() const { return m_alignsAtEnd; }

bool Appearance::showsDeeper() const { return m_showsDeeper; }

bool Appearance::deeperInSections() const { return m_deeperInSections; }
bool Appearance::showContinuations() const { return m_showContinuations; }
bool Appearance::spanHorizontal() const { return m_spanHorizontal; }
bool Appearance::spanVertical() const { return m_spanVertical; }
int Appearance::horizontalReservedPx() const { return m_horizontalReservedPx; }
int Appearance::verticalReservedPx() const { return m_verticalReservedPx; }
int Appearance::screenWidth() const { return m_screenWidth; }
int Appearance::screenHeight() const { return m_screenHeight; }

} // namespace bindpeek
