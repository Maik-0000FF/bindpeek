// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Settings.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace bindpeek {

// The settings file as something QML can bind to.
//
// Every property writes straight through to the in-memory Settings, so the
// preview updates as a control moves, and the file follows on its own shortly
// after. There is no save button: a setting is either what you want, in which
// case it should already be in effect, or you change it again.
//
// The write is delayed by a moment rather than done on the spot, because a
// slider emits a change per pixel and rewriting the file that often would be
// wasteful and would fight with anything watching it.
class SettingsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(
        int showDelayMs READ showDelayMs WRITE setShowDelayMs NOTIFY changed)
    Q_PROPERTY(QString position READ position WRITE setPosition NOTIFY changed)
    Q_PROPERTY(int marginPx READ marginPx WRITE setMarginPx NOTIFY changed)
    Q_PROPERTY(
        int edgeInsetPx READ edgeInsetPx WRITE setEdgeInsetPx NOTIFY changed)
    Q_PROPERTY(bool overlayEnabled READ overlayEnabled WRITE setOverlayEnabled
                   NOTIFY changed)
    Q_PROPERTY(
        QString disclosure READ disclosure WRITE setDisclosure NOTIFY changed)
    // Where the content sits along the edge the panel spans. One of the three
    // words below, each of which is named on its own.
    Q_PROPERTY(
        QString alignment READ alignment WRITE setAlignment NOTIFY changed)
    Q_PROPERTY(bool ignoreLoneShift READ ignoreLoneShift WRITE
                   setIgnoreLoneShift NOTIFY changed)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(bool followSystemScheme READ followSystemScheme WRITE
                   setFollowSystemScheme NOTIFY changed)
    Q_PROPERTY(
        QString themeLight READ themeLight WRITE setThemeLight NOTIFY changed)
    Q_PROPERTY(
        QString themeDark READ themeDark WRITE setThemeDark NOTIFY changed)
    Q_PROPERTY(
        QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY changed)
    Q_PROPERTY(
        int fontSizePt READ fontSizePt WRITE setFontSizePt NOTIFY changed)
    Q_PROPERTY(int cornerRadiusPx READ cornerRadiusPx WRITE setCornerRadiusPx
                   NOTIFY changed)
    Q_PROPERTY(int borderWidthPx READ borderWidthPx WRITE setBorderWidthPx
                   NOTIFY changed)
    Q_PROPERTY(double opacity READ opacity WRITE setOpacity NOTIFY changed)

    // Choices the editor offers, taken from Settings so the two cannot drift.
    Q_PROPERTY(QStringList themes READ themes CONSTANT)
    Q_PROPERTY(QStringList positions READ positions CONSTANT)
    Q_PROPERTY(QStringList disclosures READ disclosures CONSTANT)
    // The three words, each under its own name. A list would leave every
    // reader of it to work out from a position which end a word means, and
    // three readers would work it out three times.
    Q_PROPERTY(QString alignmentStart READ alignmentStart CONSTANT)
    Q_PROPERTY(QString alignmentCenter READ alignmentCenter CONSTANT)
    Q_PROPERTY(QString alignmentEnd READ alignmentEnd CONSTANT)
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)

    // Bounds and steps for the controls, again straight from Settings, so a
    // slider cannot offer a value the reader would refuse.
    Q_PROPERTY(int showDelayMin READ showDelayMin CONSTANT)
    Q_PROPERTY(int showDelayMax READ showDelayMax CONSTANT)
    Q_PROPERTY(int showDelayStep READ showDelayStep CONSTANT)
    Q_PROPERTY(int marginMin READ marginMin CONSTANT)
    Q_PROPERTY(int marginMax READ marginMax CONSTANT)
    Q_PROPERTY(int fontSizeMin READ fontSizeMin CONSTANT)
    Q_PROPERTY(int fontSizeMax READ fontSizeMax CONSTANT)
    Q_PROPERTY(int radiusMin READ radiusMin CONSTANT)
    Q_PROPERTY(int radiusMax READ radiusMax CONSTANT)
    Q_PROPERTY(int borderMin READ borderMin CONSTANT)
    Q_PROPERTY(int borderMax READ borderMax CONSTANT)
    Q_PROPERTY(double opacityMin READ opacityMin CONSTANT)
    Q_PROPERTY(double opacityMax READ opacityMax CONSTANT)
    Q_PROPERTY(double opacityStep READ opacityStep CONSTANT)

    // Whether the chosen position has an edge at all. The controls that only
    // mean something against one are disabled without it, and asking here
    // keeps the name of the centre position out of the interface.
    Q_PROPERTY(bool anchoredToEdge READ anchoredToEdge NOTIFY changed)

public:
    explicit SettingsModel(QObject *parent = nullptr);

    int showDelayMs() const;
    QString position() const;
    int marginPx() const;
    int edgeInsetPx() const;
    bool overlayEnabled() const;
    QString disclosure() const;
    QString alignment() const;
    bool ignoreLoneShift() const;
    QString theme() const;
    bool followSystemScheme() const;
    QString themeLight() const;
    QString themeDark() const;
    QString fontFamily() const;
    int fontSizePt() const;
    int cornerRadiusPx() const;
    int borderWidthPx() const;
    double opacity() const;

    void setShowDelayMs(int value);
    void setPosition(const QString &value);
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

    QStringList themes() const;
    QStringList positions() const;
    QStringList disclosures() const;
    QString alignmentStart() const;
    QString alignmentCenter() const;
    QString alignmentEnd() const;
    QStringList fontFamilies() const;

    int showDelayMin() const;
    int showDelayMax() const;
    int showDelayStep() const;
    int marginMin() const;
    int marginMax() const;
    int fontSizeMin() const;
    int fontSizeMax() const;
    int radiusMin() const;
    int radiusMax() const;
    int borderMin() const;
    int borderMax() const;
    double opacityMin() const;
    double opacityMax() const;
    double opacityStep() const;

    bool anchoredToEdge() const;

    // Back to the defaults. Saved like any other change.
    Q_INVOKABLE void resetToDefaults();

    // The values as they stand, for whoever needs a whole Settings rather than
    // single properties. Handing out the object the model already holds avoids
    // rebuilding one from the file on every keystroke of a slider.
    const Settings &current() const;

signals:
    void changed();

private:
    void load();
    void scheduleSave();

    Settings m_settings;

    // Collects the changes of a moving control into one write.
    QTimer m_saveTimer;
};

} // namespace bindpeek
