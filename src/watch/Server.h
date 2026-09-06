// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <poll.h>
#include <sys/types.h>

#include <cstddef>
#include <string>
#include <vector>

#include "Protocol.h"
#include "Seats.h"

namespace bindpeek::watch {

// The socket side: who is listening, and what they are told.
//
// Nothing is ever read from a client. There is no request, no command and no
// setting to send, so this half of the program holds no parser at all, and the
// half that holds the keyboards cannot be talked into anything.
//
// Everybody is served the seat they are sitting at and no other. The seat is
// read when they are let in, and read again while they stay: a session can be
// switched away from long after it connected.
class Server {
public:
    Server() = default;
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    // Takes the listening socket from the service manager. The service is
    // started by its socket unit and never by hand, so a missing socket is a
    // failure rather than a reason to make one: a socket made here would land
    // wherever this process happens to be allowed to write, with whatever
    // permissions its umask gives it, and that is exactly the decision that
    // belongs in the unit.
    bool start();

    void appendPollFds(std::vector<pollfd> &out) const;

    // Accepts what is new and drops what has gone. Nothing is sent from here:
    // whoever passes the check waits in the list below until admit, which is
    // after the keyboards have been opened for them and therefore the first
    // moment at which there is a true record to send.
    //
    // This is the only place the poll answers are read, and they are read at
    // the places appendPollFds put them. So it is also the last moment at
    // which the list of clients still has the length those places were counted
    // from: everything below that shortens it belongs after this call.
    void dispatch(const std::vector<pollfd> &ready, std::size_t offset);

    // Gives the record to everybody sitting at that seat, and drops whoever it
    // could not be given to.
    void broadcast(const std::string &seat, const Report &report);

    // Takes whoever was accepted this round into the list proper, each sent
    // the record of their own seat as it stands on the way in: they connected
    // because a panel has just started, and by then a modifier may well
    // already be down.
    //
    // Held back until here for two reasons. A panel which connected during
    // this very round must not be sent a report saying a key was taken before
    // it existed, which would take it off the screen for a keystroke that was
    // not its business. And the keyboards are opened between the accept and
    // this call, so before it there is nothing to tell anybody.
    void admit(const Seats &state);

    // How many have passed the check this round and are waiting to be let in.
    // Read by the caller as the signal to open the keyboards, which happens
    // for the first client and not before.
    std::size_t waiting() const;

    // Drops whoever is no longer at the seat they were let in on: they have
    // left it, or they are at another one now and the records of this one are
    // no longer theirs to have. Checked again rather than only at the door,
    // because a session can be switched away from long after it connected and
    // the records would otherwise keep going to a screen nobody is looking at.
    void dropStrangers();

    std::size_t clients() const;

private:
    struct Client {
        int fd;
        // Kept so the check can be made again later without asking the
        // descriptor a second time, and so the limit can be counted per
        // person rather than over everybody at once.
        uid_t uid;
        // The seat they were let in on, which is the one they are served and
        // the one they have to still be at when they are looked at again.
        std::string seat;
    };

    void drop(std::size_t at);
    // Returns false when the client is gone or unreachable.
    bool sendTo(int fd, const Report &report);

    int m_listen = -1;
    std::vector<Client> m_clients;

    // Accepted this round and not yet in the list above. Never longer than a
    // single pass through the loop.
    std::vector<Client> m_pending;
};

} // namespace bindpeek::watch
