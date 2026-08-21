// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>

namespace bindpeek {

// What the program says about itself, in one place.
//
// The about dialog, the window title and the command line all want the same
// name, version and links. Version comes from CMake through a compile
// definition, so a release means changing one number in one file; the rest
// lives here rather than being typed into each screen that shows it.
class AppInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString description READ description CONSTANT)
    Q_PROPERTY(QString repositoryUrl READ repositoryUrl CONSTANT)
    Q_PROPERTY(QString issuesUrl READ issuesUrl CONSTANT)
    Q_PROPERTY(QString licenseName READ licenseName CONSTANT)
    Q_PROPERTY(QString licenseUrl READ licenseUrl CONSTANT)
    Q_PROPERTY(QString developer READ developer CONSTANT)
    // Not CONSTANT: which mark is readable depends on the palette in use.
    Q_PROPERTY(QString iconSource READ iconSource NOTIFY iconSourceChanged)
    // Set from QML, which is where the palette colours actually live. Deciding
    // it here would mean a second list of which themes are light, and that
    // list would drift from the palettes themselves.
    Q_PROPERTY(bool darkSurface READ isDarkSurface WRITE setDarkSurface NOTIFY
                   iconSourceChanged)

public:
    // Where the panel keeps its single-instance lock.
    //
    // Here rather than in either program because both need the same one: the
    // panel takes it to keep a second copy of itself off the screen, and the
    // settings window reads it to find out whether a panel is running and
    // which process it is. Named after the program, which is what this file
    // is for.
    //
    // The lock is the only reliable way to ask that question. A process is
    // not found by its name: a package that installs the program as a wrapper
    // around the real binary leaves that binary name in /proc, and the kernel
    // keeps only the first fifteen characters of it.
    static QString lockPath();

    // Where the settings window listens for a second start of itself.
    //
    // A socket rather than a lock, because the answer is not "step aside" but
    // "show the window that is already there": someone reaching for the
    // launcher a second time wants the settings, and a start that only
    // refuses itself looks like a program that does not open.
    static QString editorSocketPath();

    explicit AppInfo(QObject *parent = nullptr);

    QString name() const;
    QString version() const;
    // Translated: it is a sentence, not an identifier.
    QString description() const;
    QString repositoryUrl() const;
    QString issuesUrl() const;
    QString licenseName() const;
    QString licenseUrl() const;
    QString developer() const;
    QString iconSource() const;

    // Told by QML which way round the surface is, so the mark stays visible:
    // a white mark on a light panel is no mark at all.
    bool isDarkSurface() const;
    void setDarkSurface(bool dark);

signals:
    void iconSourceChanged();

private:
    bool m_darkSurface = true;
};

// The one sentence the program uses to describe itself, as a free function so
// the overlay can reach it without the QML object above. It goes to --help and
// to the about dialog, and the desktop entry carries a copy it cannot avoid,
// because the desktop database reads a static file. scripts/check.sh compares
// that copy against this one.
QString applicationDescription();

} // namespace bindpeek
