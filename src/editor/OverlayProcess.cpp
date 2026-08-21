// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "OverlayProcess.h"

#include "AppInfo.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QList>
#include <QLockFile>
#include <QProcess>
#include <QTimer>

#include <csignal>

namespace bindpeek {
namespace {

// File name of the panel binary, as it is installed next to the editor.
constexpr char kOverlayName[] = "bindpeek";

// How often the tray re-checks. The overlay can be stopped from a terminal or
// die on its own, and the icon has to follow that within a moment without
// burning cycles.
constexpr int kPollIntervalMs = 2000;

// A start or a stop needs a moment to show up in /proc: startDetached returns
// before the process is up, and SIGTERM before it is down. Checking on the
// spot would read the old state and make the tray checkbox jump back before
// correcting itself at the next poll.
constexpr int kSettleMs = 250;

constexpr char kProcDir[] = "/proc";

// The process that holds the panel's single-instance lock, and 0 when none
// does.
//
// Asked of the lock rather than of the process names in /proc. A process is
// not reliably found by its name: a package that installs the program as a
// wrapper around the real binary leaves that binary's name behind, and the
// kernel keeps only the first fifteen characters of it, so "bindpeek" appears
// as ".bindpeek-wrapp". Matching that would be matching one packaging
// convention, and getting it wrong is not a small thing: the settings window
// then reports no panel while one is plainly on screen, starts a second that
// refuses itself against this very lock, and can neither stop nor replace the
// one that is running.
//
// The lock says exactly one thing, and it is the thing being asked: which
// process is the panel.
qint64 lockedOverlayPid() {
    QLockFile lock(AppInfo::lockPath());
    qint64 pid = 0;
    QString host;
    QString application;
    if (!lock.getLockInfo(&pid, &host, &application) || pid <= 0) {
        return 0;
    }
    // A lock file outlives a process that was killed outright, so the entry is
    // only worth anything if that process is still there. Signal zero asks
    // exactly that and does nothing else.
    if (::kill(static_cast<pid_t>(pid), 0) != 0) {
        return 0;
    }
    return pid;
}

// Whether the running panel is the one this editor would start.
//
// Compared by the directory its executable sits in, not by the file: the two
// programs are installed side by side, and a package may run either of them
// through a wrapper that carries a different file name. The directory is what
// they share and what changes when the package does.
bool isFromThisBuild(qint64 pid) {
    const QString theirs =
        QFileInfo(
            QStringLiteral("%1/%2/exe").arg(QLatin1String(kProcDir)).arg(pid))
            .canonicalPath();
    if (theirs.isEmpty()) {
        // Nothing could be read about it. That is not another user's process:
        // the lock lives in this user's runtime directory and the caller has
        // already signalled the process to prove it is there. What is left is
        // one that ended between those two questions.
        //
        // Unknown is answered as not ours, which is the answer that ends with
        // a panel on screen: a process that has gone takes no signal, and the
        // start behind it happens. Answering "ours" would tick the box and
        // start nothing.
        return false;
    }
    return theirs ==
           QFileInfo(QCoreApplication::applicationFilePath()).canonicalPath();
}

// The overlay sits next to the editor in the same directory, so it is found
// without depending on PATH.
QString overlayBinary() {
    // Not const: it is returned below, and const would force a copy.
    QString beside = QCoreApplication::applicationDirPath() + QLatin1Char('/') +
                     QLatin1String(kOverlayName);
    if (QFileInfo::exists(beside)) {
        return beside;
    }
    return QLatin1String(kOverlayName);
}

} // namespace

OverlayProcess::OverlayProcess(QObject *parent)
    : QObject(parent), m_compositor(detectCompositorSupport()) {
    m_poll.setInterval(kPollIntervalMs);
    connect(&m_poll, &QTimer::timeout, this, &OverlayProcess::poll);
    m_poll.start();
    poll();
}

bool OverlayProcess::isRunning() const { return m_running; }

bool OverlayProcess::isSupported() const { return m_compositor.supported; }

bool OverlayProcess::isUsable() const {
    return m_compositor.supported || m_running;
}

void OverlayProcess::requestToggle(SettingsModel *model) {
    if (model != nullptr) {
        model->setOverlayEnabled(!isRunning());
    }
    toggle();
}

QString OverlayProcess::unsupportedReason() const {
    return m_compositor.message();
}

QString OverlayProcess::notice() const { return m_notice; }

void OverlayProcess::setNotice(const QString &text) {
    if (text == m_notice) {
        return;
    }
    m_notice = text;
    emit noticeChanged();
}

void OverlayProcess::poll() {
    const qint64 pid = lockedOverlayPid();
    const bool running = pid != 0;
    // A refusal only holds while the panel it refers to does. The leftover
    // may have been ended from outside, or what is running now may be this
    // build's after all; either way the line has to go, because it is shown
    // in place of every other one and would otherwise report a panel that is
    // no longer there.
    if (!m_notice.isEmpty() && (!running || isFromThisBuild(pid))) {
        setNotice(QString());
    }
    if (running == m_running) {
        return;
    }
    m_running = running;
    emit runningChanged();
}

void OverlayProcess::refresh() {
    m_running = lockedOverlayPid() != 0;
    emit runningChanged();
}

bool OverlayProcess::start() { return launch(true); }

bool OverlayProcess::launch(bool replaceStrangers) {
    // Whatever stopped the last attempt is not what stopped this one.
    if (replaceStrangers) {
        setNotice(QString());
    }
    // What is running decides, not what was last reported: a stale answer here
    // is what turns a replacement into no panel at all.
    const qint64 running = lockedOverlayPid();
    if (running != 0) {
        if (isFromThisBuild(running)) {
            // Ours is up. Nothing to do, and nothing to report.
            setNotice(QString());
            return true;
        }
        // A panel from another build counts as no panel: everything the editor
        // writes would reach a program built before the setting existed, which
        // looks exactly like a setting that does nothing. It is asked to go
        // and this one takes its place.
        //
        // Waiting for it is not optional, the panel refuses to start while
        // another instance holds the lock, so the start comes after the old
        // process has actually ended.
        //
        // One attempt, not a loop: a panel that does not answer SIGTERM is not
        // this program to end, and signalling it every quarter second forever
        // would be worse than the leftover. The second pass therefore finds it
        // still there and says so instead of trying again.
        //
        // Says so through the notice: its answer is thrown away by the timer
        // below, and the first pass has long since returned. A refusal nobody
        // can see is the same as no refusal at all.
        if (!replaceStrangers) {
            setNotice(tr("A panel from an earlier version is still running "
                         "and did not make way. Settings may not reach it."));
            return false;
        }
        ::kill(static_cast<pid_t>(running), SIGTERM);
        QTimer::singleShot(kSettleMs, this, [this]() {
            refresh();
            launch(false);
        });
        return true;
    }

    // Scheduled before anything can return, including the refusals below. The
    // caller is a checkable action that has already ticked itself; only this
    // report puts the tick back, and a start that fails silently would leave
    // it claiming a panel that is not there.
    QTimer::singleShot(kSettleMs, this, &OverlayProcess::refresh);

    if (!m_compositor.supported) {
        // Starting it would only produce a process that refuses itself.
        return false;
    }
    // A start can also fail after the call: the binary launches and then gives
    // up because the input group is missing or another instance holds the
    // lock. Which is the other reason the report above is unconditional.
    return QProcess::startDetached(overlayBinary(), {});
}

void OverlayProcess::stop() {
    // Whatever the last start ran into is no longer the state of things: the
    // panel is being sent away, and a line about a failed replacement would
    // outlive what it describes.
    setNotice(QString());
    if (const qint64 pid = lockedOverlayPid(); pid != 0) {
        ::kill(static_cast<pid_t>(pid), SIGTERM);
    }
    QTimer::singleShot(kSettleMs, this, &OverlayProcess::refresh);
}

void OverlayProcess::toggle() {
    if (m_running) {
        stop();
    } else {
        start();
    }
}

} // namespace bindpeek
