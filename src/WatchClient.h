// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>

class QSocketNotifier;

namespace bindpeek {

// What one record from the service means, and whether it means anything at
// all.
//
// A record arrives from another process, which makes reading it the one piece
// of checking code on this side of the socket. Pulled out of the reading so it
// can be measured without one: it takes bytes and gives an answer, and the
// part that owns a descriptor holds no judgement of its own.
struct Heard {
    // False when the record is not one this panel knows how to read. Nothing
    // else in the struct means anything then.
    bool understood = false;
    // Set when the record announces a version this panel was not built
    // against, which is a service that has been replaced under a running panel
    // rather than anything malformed.
    bool wrongVersion = false;
    QStringList held;
    bool keyTaken = false;
};

// bytes is what recv returned, so a short or overlong datagram is refused here
// rather than read past.
Heard hear(const void *record, std::size_t bytes);

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

    // Said once and then not again: a service that speaks another version
    // will keep speaking it, and a line every two seconds is noise.
    bool m_saidWrongVersion = false;

    // The service ends itself once the last panel has gone, and the socket
    // starts it again on the next connection. So a lost connection is not the
    // end of anything, it is a service that was restarted or replaced, and the
    // way back is to knock again.
    QTimer m_retry;
};

} // namespace bindpeek
