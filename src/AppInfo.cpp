// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AppInfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace bindpeek {
namespace {

constexpr char kName[] = "bindpeek";
constexpr char kRepository[] = "https://github.com/Maik-0000FF/bindpeek";
constexpr char kIssues[] = "https://github.com/Maik-0000FF/bindpeek/issues";

// Matches the SPDX identifier in the file headers and the LICENSE file.
constexpr char kLicenseName[] = "GPL-3.0-or-later";
constexpr char kLicenseUrl[] = "https://www.gnu.org/licenses/gpl-3.0.html";

constexpr char kDeveloper[] = "Maik-0000FF";

// The single-instance marks, both in the runtime directory the session clears
// on logout, so a crash cannot leave one behind that keeps the program from
// ever starting again.
//
// Two of them, because the two programs are single in different ways. The
// panel takes a lock and a second copy of it steps aside; the settings window
// listens on a socket, and a second start hands the request over and lets the
// window that is already there answer it.
constexpr char kLockFileName[] = "bindpeek.lock";
constexpr char kEditorSocketName[] = "bindpeek-editor.sock";

// The mark rather than the full logo: in a dialog the wordmark would repeat
// the name printed next to it. Two inks, because a light palette needs the
// dark one.
constexpr char kIconOnDark[] = "qrc:/icons/logo-mark-white.svg";
constexpr char kIconOnLight[] = "qrc:/icons/logo-mark-black.svg";

// The runtime directory, or the temporary one when the session names none.
QString runtimeDir() {
    QString directory =
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (directory.isEmpty()) {
        directory = QDir::tempPath();
    }
    return directory;
}

} // namespace

QString AppInfo::editorSocketPath() {
    return runtimeDir() + QLatin1Char('/') + QLatin1String(kEditorSocketName);
}

QString AppInfo::lockPath() {
    return runtimeDir() + QLatin1Char('/') + QLatin1String(kLockFileName);
}

AppInfo::AppInfo(QObject *parent) : QObject(parent) {}

QString AppInfo::name() const { return QLatin1String(kName); }
QString AppInfo::version() const { return QLatin1String(BINDPEEK_VERSION); }

QString applicationDescription() {
    return QCoreApplication::translate(
        "AppInfo", "Shows the shortcuts assigned in the running session.");
}

QString AppInfo::description() const { return applicationDescription(); }

QString AppInfo::repositoryUrl() const { return QLatin1String(kRepository); }
QString AppInfo::issuesUrl() const { return QLatin1String(kIssues); }
QString AppInfo::licenseName() const { return QLatin1String(kLicenseName); }
QString AppInfo::licenseUrl() const { return QLatin1String(kLicenseUrl); }
QString AppInfo::developer() const { return QLatin1String(kDeveloper); }
QString AppInfo::iconSource() const {
    return QLatin1String(m_darkSurface ? kIconOnDark : kIconOnLight);
}

bool AppInfo::isDarkSurface() const { return m_darkSurface; }

void AppInfo::setDarkSurface(bool dark) {
    if (m_darkSurface == dark) {
        return;
    }
    m_darkSurface = dark;
    emit iconSourceChanged();
}

} // namespace bindpeek
