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

// The state sd-login reports for a user with no session at all. Every other
// answer means there is one, whether it is the one in front of the screen or
// one that has been switched away from.
constexpr char kNoSession[] = "offline";

// How many panels may be connected at once. One is the ordinary number and a
// second is a moment during a restart; past that it is not a panel. A ceiling
// at all because every connection costs a descriptor and a receive queue that
// is never drained, and the socket is reachable by anybody logged in.
constexpr std::size_t kMaxClients = 8;

// Whether the other end of a connection belongs to somebody logged in.
//
// Not to find out who they are: to turn away everyone who is nobody. The
// records carry the moment of every keystroke, which is worth little on its
// own and more than nothing to a system account quietly collecting it.
//
// The peer credentials give the user, and sd-login is then asked about that
// user rather than about the process. That way round on purpose: the call
// about a process reads the cgroup of the peer out of /proc, which the unit
// hides with ProtectProc, and every client would be refused. The call about a
// user reads /run/systemd/users, which stays readable. Measured in
// sd-login.c, sd_peer_get_owner_uid against sd_uid_get_state.
//
// Any session counts, not only an active one. A nested session, a second seat
// or a moment during a switch between them are all somebody who may look at
// their own keyboard; what is being kept out is the account that never logs
// in.
bool loggedIn(int fd) {
    ucred peer{};
    socklen_t size = sizeof peer;
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) < 0) {
        return false;
    }

    char *state = nullptr;
    if (sd_uid_get_state(peer.uid, &state) < 0) {
        return false;
    }
    const bool ok = std::strcmp(state, kNoSession) != 0;
    std::free(state);
    return ok;
}

} // namespace

Server::~Server() {
    for (const int fd : m_clients) {
        ::close(fd);
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
                     "bindpeek-watch: cannot set the socket non-blocking: "
                     "%s\n",
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
    for (const int fd : m_clients) {
        // No POLLIN: nothing a client writes is ever read. Only the hang-up is
        // of interest, and that arrives without being asked for. Watched at
        // all so that a panel which has gone is noticed there and then, rather
        // than at the next keystroke.
        out.push_back(pollfd{fd, 0, 0});
    }
}

void Server::drop(std::size_t at) {
    ::close(m_clients[at]);
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
        if (!sendTo(m_clients[index], report)) {
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
        if (m_clients.size() >= kMaxClients || !loggedIn(fd)) {
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
        m_clients.push_back(fd);
    }
}

} // namespace bindpeek::watch
