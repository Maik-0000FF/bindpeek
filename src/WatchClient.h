// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

class QSocketNotifier;

namespace bindpeek {

// The panel's end of the keyboard watch.
//
// The event devices are not opened here and cannot be: this program runs as
// the person using it, and giving that account the right to read every
// keystroke is the thing the service exists to avoid. What arrives over the
// socket is which modifiers are held and the bare fact that some other key
// went down, which is all the panel ever needed.
class WatchClient : public QObject {
    Q_OBJECT

public:
    explicit WatchClient(QObject *parent = nullptr);
    ~WatchClient() override;

    // Connects to the service. False means it could not be reached at all,
    // which is what a service that is not installed or not enabled looks like
    // from here; the caller says so and gives up, because a panel that cannot
    // see a modifier has nothing to show.
    bool start();

    // The modifiers held right now, canonical and in the order they went down.
    QStringList held() const;

    // Where the service is expected, for the message the caller writes when
    // start() says no.
    static QString socketPath();

    // The unit that starts it, for the same message.
    static QString socketUnit();

signals:
    // The held modifiers changed, in what is down or in what was pressed
    // first.
    void heldChanged(const QStringList &held);

    // A key that is not a modifier went down. The user has just pressed the
    // shortcut they were looking for, so the panel has done its job. Which key
    // it was is not known here and is not needed: the service does not send
    // it.
    void shortcutTaken();

private:
    bool openConnection();
    void closeConnection();
    void onReadable();

    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QStringList m_held;

    // The service ends itself once the last panel has gone, and the socket
    // starts it again on the next connection. So a lost connection is not the
    // end of anything, it is a service that was restarted or replaced, and the
    // way back is to knock again.
    QTimer m_retry;
};

} // namespace bindpeek
