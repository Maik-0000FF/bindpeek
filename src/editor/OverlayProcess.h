// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Compositor.h"
#include "SettingsModel.h"

#include <QObject>
#include <QString>
#include <QTimer>

namespace bindpeek {

// Starts and stops the overlay from the tray.
//
// The overlay is a separate process that may also have been started by the
// session, so it cannot simply be a child of the editor: the tray has to find
// one that is already running. It is found through the lock file the panel
// takes to keep itself single, which names the process that holds it; /proc
// is read afterwards, and only to compare where that process was installed
// from. Neither needs a helper process or a channel of its own.
class OverlayProcess : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    // Whether this session can host the panel at all, and why not.
    //
    // The overlay checks the same thing and refuses to start, but it says so
    // on its own standard error, and nothing started from a tray icon has a
    // terminal to say it to. Without this the tick would simply spring back
    // after a moment and leave the user guessing.
    Q_PROPERTY(bool supported READ isSupported CONSTANT)
    // Whether the switch is worth offering at all: a session that cannot host
    // the panel can still stop one that is somehow up, and nothing else.
    Q_PROPERTY(bool usable READ isUsable NOTIFY runningChanged)
    Q_PROPERTY(QString unsupportedReason READ unsupportedReason CONSTANT)
    // What stopped the last start, when something did and the panel is still
    // somebody else's. Empty the rest of the time.
    //
    // A panel left over from an earlier build holds the lock and keeps this
    // one out. The tick then follows that panel and stays on, which is true
    // and useless: the settings written here reach a program that may not
    // know them. Without a line saying so, that reads as settings that do
    // nothing.
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)

public:
    explicit OverlayProcess(QObject *parent = nullptr);

    bool isRunning() const;
    bool isSupported() const;
    bool isUsable() const;
    QString unsupportedReason() const;
    QString notice() const;

    // Re-reads the state and announces it even when it has not changed.
    //
    // After a start or a stop the answer has to reach the interface either
    // way: a start that failed leaves the state exactly as it was, and a
    // checkbox that toggled itself on the click would stay wrong until
    // something else happened to change it.
    void refresh();

    // Q_INVOKABLE rather than slots: these are called from QML, and the
    // connections in main.cpp use function pointers, which need no slot.
    // It also lets the QML contract test see them.

    // Starts the overlay if it is not up yet. Returns false when the binary
    // could not be launched.
    Q_INVOKABLE bool start();

    // Turns the panel around and writes down that this is what was wanted.
    //
    // The wish first and the attempt after, in that order: switching the
    // panel off is meant to outlast the session, and has to hold even where
    // the start that follows fails. Both the tray and the settings window go
    // through this, so the order cannot be right in one place and wrong in
    // the other.
    Q_INVOKABLE void requestToggle(SettingsModel *model);

    // Asks every running overlay to quit, politely: SIGTERM, so it can release
    // its layer surface instead of leaving one behind.
    Q_INVOKABLE void stop();

    Q_INVOKABLE void toggle();

signals:
    void runningChanged();
    void noticeChanged();

private:
    void poll();

    // The body of start(). replaceStrangers is false on the second pass, once
    // a panel from another build has been asked to go: see the comment there.
    bool launch(bool replaceStrangers);

    // Announces only when the text actually changes, so a poll that keeps
    // finding the same thing does not keep the interface busy.
    void setNotice(const QString &text);

    // The overlay can also be stopped from outside, so the state is polled
    // rather than assumed from the last button press.
    QTimer m_poll;
    bool m_running = false;
    QString m_notice;
    CompositorSupport m_compositor;
};

} // namespace bindpeek
