// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Server.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <systemd/sd-daemon.h>
#include <systemd/sd-login.h>

namespace bindpeek::watch {
namespace {

// How many connections one person may hold at once. A panel is one, and a
// second is the moment during a restart when the old one has not let go yet.
// Counted per user rather than over everybody, so that one account cannot use
// up the room another one needs.
constexpr std::size_t kMaxClientsPerUser = 4;

// Whether a user is at a seat right now, with a session in the foreground.
//
// A positive test rather than a list of states to refuse. sd_uid_get_state
// would answer "lingering" for an account that is not logged in at all but has
// services running, and "closing" for one that has logged out, and both of
// those are exactly who this is meant to keep out; a check written as "not
// offline" lets both in. Measured against sd_uid_get_state(3), which spells
// out both of those states.
//
// The seats are enumerated rather than left to the library. Asked with no seat
// at all, sd_uid_is_on_seat falls back to the seat of the caller's own
// session, and this service has none: it runs under an account the service
// manager makes for it. Read in sd-login.c, file_of_seat.
//
// It reads /run/systemd/seats, not /proc, so ProtectProc in the unit does not
// blind it.
//
// What this does not say: which keyboard the records came from. One service
// reads every keyboard on the machine, so on a machine with two seats the
// person at one of them learns when the person at the other pressed a
// modifier. Everybody served here is at a screen of this machine, which is the
// line that can be drawn from here.
bool atAnActiveSeat(uid_t uid) {
    char **seats = nullptr;
    const int count = sd_get_seats(&seats);
    if (count < 0) {
        return false;
    }

    bool found = false;
    for (int at = 0; at < count; ++at) {
        if (!found && sd_uid_is_on_seat(uid, 1, seats[at]) > 0) {
            found = true;
        }
        std::free(seats[at]);
    }
    // The array itself, after its strings. Cast because it is a pointer to
    // pointers and free takes one level; the check that says so is right that
    // the conversion is worth writing out.
    std::free(static_cast<void *>(seats));
    return found;
}

// Who is on the other end. Not to find out who they are: to turn away everyone
// who is not at this machine. The records carry the moment of every keystroke,
// which is worth little on its own and more than nothing to somebody
// collecting it.
bool peerUid(int fd, uid_t *uid) {
    ucred peer{};
    socklen_t size = sizeof peer;
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) < 0) {
        return false;
    }
    *uid = peer.uid;
    return true;
}

} // namespace

Server::~Server() {
    for (const Client &client : m_clients) {
        ::close(client.fd);
    }
    // The listening socket is not closed: it belongs to the service manager,
    // which keeps listening while this process is away and starts it again on
    // the next connection.
}

bool Server::start() {
    const int passed = sd_listen_fds(0);
    if (passed != 1) {
        std::fprintf(stderr,
                     "bindpeek-watch: expected one socket from the service "
                     "manager, got %d. This service is started by "
                     "bindpeek-watch.socket, not by hand.\n",
                     passed);
        return false;
    }

    m_listen = SD_LISTEN_FDS_START;
    if (sd_is_socket(m_listen, AF_UNIX, SOCK_SEQPACKET, 1) <= 0) {
        std::fprintf(stderr, "bindpeek-watch: the socket handed over is not a "
                             "listening AF_UNIX SOCK_SEQPACKET socket\n");
        return false;
    }

    // The socket arrives blocking, and it has to be made non-blocking here
    // rather than asked for in the unit: the loop below accepts until there is
    // nothing left, and on a blocking socket that last attempt never returns.
    // Measured, and it is a quiet failure: the service sits in accept holding
    // the keyboards, answering nobody and never noticing that its one client
    // has gone.
    //
    // SOCK_NONBLOCK in accept4 does not do this. That flag is put on the
    // connection that comes out, not on the socket being accepted from.
    if (::fcntl(m_listen, F_SETFL, O_NONBLOCK) < 0) {
        std::fprintf(stderr,
                     "bindpeek-watch: cannot set the socket non-blocking: %s\n",
                     std::strerror(errno));
        return false;
    }
    return true;
}

std::size_t Server::clients() const { return m_clients.size(); }

// The listening socket first, then one for each client, and dispatch reads the
// answers back at exactly those places.
void Server::appendPollFds(std::vector<pollfd> &out) const {
    out.push_back(pollfd{m_listen, POLLIN, 0});
    for (const Client &client : m_clients) {
        // No POLLIN: nothing a client writes is ever read. Only the hang-up is
        // of interest, and that arrives without being asked for. Watched at
        // all so that a panel which has gone is noticed there and then, rather
        // than at the next keystroke.
        out.push_back(pollfd{client.fd, 0, 0});
    }
}

void Server::drop(std::size_t at) {
    ::close(m_clients[at].fd);
    m_clients.erase(m_clients.begin() + static_cast<std::ptrdiff_t>(at));
}

bool Server::sendTo(int fd, const Report &report) {
    // MSG_NOSIGNAL so that a client which has gone cannot end the process that
    // holds the keyboards, and MSG_DONTWAIT so that one which has stopped
    // reading cannot hold it up. A full buffer means eight bytes could not be
    // placed in a queue that takes thousands of them, which is not a slow
    // reader but one that is not reading at all.
    const ssize_t wrote =
        ::send(fd, &report, sizeof report, MSG_NOSIGNAL | MSG_DONTWAIT);
    return wrote == static_cast<ssize_t>(sizeof report);
}

void Server::broadcast(const Report &report) {
    for (std::size_t at = m_clients.size(); at > 0; --at) {
        const std::size_t index = at - 1;
        if (!sendTo(m_clients[index].fd, report)) {
            drop(index);
        }
    }
}

void Server::dropStrangers() {
    for (std::size_t at = m_clients.size(); at > 0; --at) {
        const std::size_t index = at - 1;
        if (!atAnActiveSeat(m_clients[index].uid)) {
            drop(index);
        }
    }
}

void Server::dispatch(const std::vector<pollfd> &ready, std::size_t offset,
                      const Report &current) {
    // The clients first and from the back, so that dropping one does not move
    // the ones still to be looked at, and so that what is accepted below is
    // not immediately walked over again.
    for (std::size_t at = m_clients.size(); at > 0; --at) {
        const std::size_t index = at - 1;
        if ((ready[offset + index + 1].revents & (POLLHUP | POLLERR)) != 0) {
            drop(index);
        }
    }

    if ((ready[offset].revents & POLLIN) == 0) {
        return;
    }

    while (true) {
        const int fd =
            ::accept4(m_listen, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            break;
        }

        uid_t uid = 0;
        if (!peerUid(fd, &uid) || !atAnActiveSeat(uid)) {
            ::close(fd);
            continue;
        }

        std::size_t held = 0;
        for (const Client &client : m_clients) {
            if (client.uid == uid) {
                ++held;
            }
        }
        if (held >= kMaxClientsPerUser) {
            ::close(fd);
            continue;
        }

        // Sent at once rather than at the next change: this client connected
        // because the panel has just started, and by then a modifier may well
        // already be down.
        if (!sendTo(fd, current)) {
            ::close(fd);
            continue;
        }
        m_clients.push_back(Client{fd, uid});
    }
}

} // namespace bindpeek::watch
