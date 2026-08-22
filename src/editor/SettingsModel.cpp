// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SettingsModel.h"

#include <QFontDatabase>

namespace bindpeek {
namespace {

// How long the model waits before writing. Long enough that dragging a slider
// produces one write instead of hundreds, short enough that letting go feels
// like it took effect at once.
constexpr int kSaveDelayMs = 400;

} // namespace

SettingsModel::SettingsModel(QObject *parent) : QObject(parent) {
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this,
            [this]() { m_settings.save(); });

    // The file may not exist yet on a machine where the overlay never ran.
    Settings::writeTemplateIfMissing();
    load();
}

void SettingsModel::scheduleSave() { m_saveTimer.start(); }

void SettingsModel::load() {
    m_settings = Settings();
    emit changed();
}

void SettingsModel::resetToDefaults() {
    // Reading a path that cannot exist yields a Settings full of defaults,
    // which is exactly what "reset" means.
    m_settings = Settings(QStringLiteral("/nonexistent/bindpeek.conf"));
    scheduleSave();
    emit changed();
}

int SettingsModel::showDelayMs() const { return m_settings.showDelayMs(); }
QString SettingsModel::position() const {
    return Settings::positionName(m_settings.position());
}
int SettingsModel::marginPx() const { return m_settings.marginPx(); }
int SettingsModel::edgeInsetPx() const { return m_settings.edgeInsetPx(); }
bool SettingsModel::overlayEnabled() const {
    return m_settings.overlayEnabled();
}
QString SettingsModel::disclosure() const { return m_settings.disclosure(); }

QString SettingsModel::arrangement() const { return m_settings.arrangement(); }
QString SettingsModel::alignment() const { return m_settings.alignment(); }
bool SettingsModel::ignoreLoneShift() const {
    return m_settings.ignoreLoneShift();
}
QString SettingsModel::theme() const { return m_settings.theme(); }
bool SettingsModel::followSystemScheme() const {
    return m_settings.followSystemScheme();
}
QString SettingsModel::themeLight() const { return m_settings.themeLight(); }
QString SettingsModel::themeDark() const { return m_settings.themeDark(); }
QString SettingsModel::fontFamily() const { return m_settings.fontFamily(); }
int SettingsModel::fontSizePt() const { return m_settings.fontSizePt(); }
int SettingsModel::cornerRadiusPx() const {
    return m_settings.cornerRadiusPx();
}
int SettingsModel::borderWidthPx() const { return m_settings.borderWidthPx(); }
double SettingsModel::opacity() const { return m_settings.opacity(); }

// Each setter guards against a no-op so a binding cannot loop, then announces
// the change once. QML rebinds the preview from there.
#define BINDPEEK_SETTER(name, type, getter, setter)                            \
    void SettingsModel::name(type value) {                                     \
        if (m_settings.getter() == value) {                                    \
            return;                                                            \
        }                                                                      \
        m_settings.setter(value);                                              \
        scheduleSave();                                                        \
        emit changed();                                                        \
    }

BINDPEEK_SETTER(setShowDelayMs, int, showDelayMs, setShowDelayMs)
BINDPEEK_SETTER(setMarginPx, int, marginPx, setMarginPx)
BINDPEEK_SETTER(setEdgeInsetPx, int, edgeInsetPx, setEdgeInsetPx)
BINDPEEK_SETTER(setOverlayEnabled, bool, overlayEnabled, setOverlayEnabled)
BINDPEEK_SETTER(setDisclosure, const QString &, disclosure, setDisclosure)
BINDPEEK_SETTER(setArrangement, const QString &, arrangement, setArrangement)
BINDPEEK_SETTER(setAlignment, const QString &, alignment, setAlignment)
BINDPEEK_SETTER(setIgnoreLoneShift, bool, ignoreLoneShift, setIgnoreLoneShift)
BINDPEEK_SETTER(setTheme, const QString &, theme, setTheme)
BINDPEEK_SETTER(setFollowSystemScheme, bool, followSystemScheme,
                setFollowSystemScheme)
BINDPEEK_SETTER(setThemeLight, const QString &, themeLight, setThemeLight)
BINDPEEK_SETTER(setThemeDark, const QString &, themeDark, setThemeDark)
BINDPEEK_SETTER(setFontFamily, const QString &, fontFamily, setFontFamily)
BINDPEEK_SETTER(setFontSizePt, int, fontSizePt, setFontSizePt)
BINDPEEK_SETTER(setCornerRadiusPx, int, cornerRadiusPx, setCornerRadiusPx)
BINDPEEK_SETTER(setBorderWidthPx, int, borderWidthPx, setBorderWidthPx)

#undef BINDPEEK_SETTER

void SettingsModel::setPosition(const QString &value) {
    // Settings owns the mapping; repeating it here is exactly the kind of
    // second copy that drifts.
    const Settings::Position parsed = Settings::positionFromName(value);
    if (m_settings.position() == parsed) {
        return;
    }
    m_settings.setPosition(parsed);
    scheduleSave();
    emit changed();
}

void SettingsModel::setOpacity(double value) {
    if (qFuzzyCompare(m_settings.opacity(), value)) {
        return;
    }
    m_settings.setOpacity(value);
    scheduleSave();
    emit changed();
}

const Settings &SettingsModel::current() const { return m_settings; }

QStringList SettingsModel::themes() const { return Settings::knownThemes(); }
QStringList SettingsModel::positions() const {
    return Settings::knownPositions();
}
QStringList SettingsModel::disclosures() const {
    return Settings::knownDisclosures();
}

QStringList SettingsModel::arrangements() const {
    return Settings::knownArrangements();
}
QString SettingsModel::alignmentStart() const {
    return QLatin1String(alignment::kStart);
}
QString SettingsModel::alignmentCenter() const {
    return QLatin1String(alignment::kCenter);
}
QString SettingsModel::alignmentEnd() const {
    return QLatin1String(alignment::kEnd);
}

// An empty first entry stands for "pick one for me", which is what an empty
// fontFamily means in the file.
QStringList SettingsModel::fontFamilies() const {
    QStringList families;
    families.append(QString());
    families.append(QFontDatabase::families());
    return families;
}

int SettingsModel::showDelayMin() const {
    return Settings::showDelayRange().low;
}
int SettingsModel::showDelayMax() const {
    return Settings::showDelayRange().high;
}
int SettingsModel::showDelayStep() const { return Settings::showDelayStepMs(); }
int SettingsModel::marginMin() const { return Settings::marginRange().low; }
int SettingsModel::marginMax() const { return Settings::marginRange().high; }
int SettingsModel::fontSizeMin() const { return Settings::fontSizeRange().low; }
int SettingsModel::fontSizeMax() const {
    return Settings::fontSizeRange().high;
}
int SettingsModel::radiusMin() const {
    return Settings::cornerRadiusRange().low;
}
int SettingsModel::radiusMax() const {
    return Settings::cornerRadiusRange().high;
}
int SettingsModel::borderMin() const {
    return Settings::borderWidthRange().low;
}
int SettingsModel::borderMax() const {
    return Settings::borderWidthRange().high;
}
double SettingsModel::opacityMin() const { return Settings::opacityLow(); }
double SettingsModel::opacityMax() const { return Settings::opacityHigh(); }
double SettingsModel::opacityStep() const { return Settings::opacityStep(); }

bool SettingsModel::anchoredToEdge() const {
    return m_settings.anchoredToEdge();
}

} // namespace bindpeek
