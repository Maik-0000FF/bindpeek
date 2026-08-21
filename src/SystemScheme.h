// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDBusVariant>
#include <QObject>
#include <QString>

namespace bindpeek {

// Whether the desktop wants light or dark, asked of the desktop portal.
//
// QStyleHints::colorScheme() is the obvious source and the wrong one here: it
// only carries a value when a platform theme feeds it, which on a plain
// wlroots session is not the case. Measured on such a session the portal
// answers "dark" while Qt answers "unknown", which is why applications that
// only ask Qt come up light on a dark desktop.
//
// So the portal is asked directly, over D-Bus, and its change signal is
// followed. Qt stays as the fallback for desktops without a portal.
class SystemScheme : public QObject {
    Q_OBJECT

public:
    enum class Scheme {
        Unknown, // nobody has an opinion
        Light,
        Dark,
    };

    explicit SystemScheme(QObject *parent = nullptr);

    Scheme scheme() const;

signals:
    void schemeChanged();

private slots:
    // The portal announces every setting; only the appearance key matters.
    void onSettingChanged(const QString &nameSpace, const QString &key,
                          const QDBusVariant &value);

private:
    void readFromPortal();
    void applyScheme(Scheme scheme);

    Scheme m_scheme = Scheme::Unknown;
};

} // namespace bindpeek
