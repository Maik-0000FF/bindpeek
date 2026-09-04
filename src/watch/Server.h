// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <poll.h>

#include <cstddef>
#include <vector>

#include "Protocol.h"

namespace bindpeek::watch {

// The socket side: who is listening, and what they are told.
//
// Nothing is ever read from a client. There is no request, no command and no
// setting to send, so this half of the program holds no parser at all, and the
// half that holds the keyboards cannot be talked into anything.
class Server {
public:
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;
    Server() = default;

    // Takes the listening socket from the service manager. The service is
    // started by its socket unit and never by hand, so a missing socket is a
    // failure rather than a reason to make one: a socket made here would land
    // wherever this process happens to be allowed to write, with whatever
    // permissions its umask gives it, and that is exactly the decision that
    // belongs in the unit.
    bool start();

    void appendPollFds(std::vector<pollfd> &out) const;

    // Accepts what is new and drops what has gone. A client that has just
    // connected is sent the record at once: it connected because the panel was
    // started, and by then a modifier may well already be down.
    void dispatch(const std::vector<pollfd> &ready, std::size_t offset,
                  const Report &current);

    void broadcast(const Report &report);

    std::size_t clients() const;

private:
    void drop(std::size_t at);
    // Returns false when the client is gone or unreachable.
    bool sendTo(int fd, const Report &report);

    int m_listen = -1;
    std::vector<int> m_clients;
};

} // namespace bindpeek::watch
