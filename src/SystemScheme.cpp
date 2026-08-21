// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SystemScheme.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QStyleHints>
#include <QVariant>

namespace bindpeek {
namespace {

// The portal's settings interface, and the one key that matters here.
constexpr char kService[] = "org.freedesktop.portal.Desktop";
constexpr char kPath[] = "/org/freedesktop/portal/desktop";
constexpr char kInterface[] = "org.freedesktop.portal.Settings";
constexpr char kMethod[] = "ReadOne";
constexpr char kSignal[] = "SettingChanged";
constexpr char kNamespace[] = "org.freedesktop.appearance";
constexpr char kKey[] = "color-scheme";

// The values the specification defines for color-scheme.
constexpr uint kNoPreference = 0;
constexpr uint kPreferDark = 1;
constexpr uint kPreferLight = 2;

SystemScheme::Scheme fromPortalValue(uint value) {
    switch (value) {
    case kPreferDark:
        return SystemScheme::Scheme::Dark;
    case kPreferLight:
        return SystemScheme::Scheme::Light;
    case kNoPreference:
    default:
        return SystemScheme::Scheme::Unknown;
    }
}

SystemScheme::Scheme fromQt() {
    const QStyleHints *hints = QGuiApplication::styleHints();
    if (hints == nullptr) {
        return SystemScheme::Scheme::Unknown;
    }
    switch (hints->colorScheme()) {
    case Qt::ColorScheme::Dark:
        return SystemScheme::Scheme::Dark;
    case Qt::ColorScheme::Light:
        return SystemScheme::Scheme::Light;
    default:
        return SystemScheme::Scheme::Unknown;
    }
}

} // namespace

SystemScheme::SystemScheme(QObject *parent) : QObject(parent) {
    readFromPortal();

    // The signal carries (namespace, key, value).
    QDBusConnection::sessionBus().connect(
        QLatin1String(kService), QLatin1String(kPath),
        QLatin1String(kInterface), QLatin1String(kSignal), this,
        SLOT(onSettingChanged(QString, QString, QDBusVariant)));

    // Qt can still learn a scheme later, on a desktop where a platform theme
    // does feed it. Following that costs nothing and helps where no portal is
    // running.
    if (QStyleHints *hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) {
                    if (m_scheme == Scheme::Unknown) {
                        applyScheme(fromQt());
                    }
                });
    }
}

void SystemScheme::onSettingChanged(const QString &nameSpace,
                                    const QString &key,
                                    const QDBusVariant &value) {
    if (nameSpace != QLatin1String(kNamespace) || key != QLatin1String(kKey)) {
        return;
    }
    bool ok = false;
    const uint number = value.variant().toUInt(&ok);
    if (!ok) {
        return;
    }
    const Scheme portal = fromPortalValue(number);
    applyScheme(portal == Scheme::Unknown ? fromQt() : portal);
}

void SystemScheme::readFromPortal() {
    QDBusMessage call = QDBusMessage::createMethodCall(
        QLatin1String(kService), QLatin1String(kPath),
        QLatin1String(kInterface), QLatin1String(kMethod));
    call << QLatin1String(kNamespace) << QLatin1String(kKey);

    const QDBusMessage reply = QDBusConnection::sessionBus().call(call);
    if (reply.type() != QDBusMessage::ReplyMessage ||
        reply.arguments().isEmpty()) {
        // No portal, or it does not know the key. Qt is the fallback.
        applyScheme(fromQt());
        return;
    }

    // The reply is a variant wrapping a variant wrapping the number.
    QVariant value = reply.arguments().constFirst();
    while (value.canConvert<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }

    bool ok = false;
    const uint number = value.toUInt(&ok);
    if (!ok) {
        applyScheme(fromQt());
        return;
    }

    const Scheme portal = fromPortalValue(number);
    applyScheme(portal == Scheme::Unknown ? fromQt() : portal);
}

void SystemScheme::applyScheme(Scheme scheme) {
    if (scheme == m_scheme) {
        return;
    }
    m_scheme = scheme;
    emit schemeChanged();
}

SystemScheme::Scheme SystemScheme::scheme() const { return m_scheme; }

} // namespace bindpeek
