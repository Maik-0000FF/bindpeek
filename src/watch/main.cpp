// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// bindpeek-watch: holds the keyboards so that no account has to.
//
// The panel needs to know which modifiers are held while those modifiers still
// belong to the compositor, which can only be read below it, from the event
// devices. Read there by the panel itself, that ability has to be granted to
// the account the panel runs as, and then every other program of that account
// has it too, for as long as the account exists.
//
// So it is read here instead: one small program, started by its socket unit,
// running under an account of its own that lives only while it does. What
// leaves it is which modifiers are down and the bare fact that some other key
// went down. No key codes and no characters cross the socket, and nothing a
// client sends is ever read.

#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Devices.h"
#include "Modifiers.h"
#include "Protocol.h"
#include "Server.h"

namespace {

using bindpeek::watch::Devices;
using bindpeek::watch::Modifiers;
using bindpeek::watch::Report;
using bindpeek::watch::Server;

// How often the real state of the modifier keys is fetched from the devices.
// Rare enough to cost nothing, often enough that a stuck panel corrects itself
// before it becomes annoying.
constexpr int kResyncIntervalMs = 1500;

// How long this stays alive after the last panel has gone. Without a pause it
// would be started and stopped again each time somebody switches the overlay
// off and straight back on, and with a long one it would hold the keyboards
// for no reason at all. Seconds, because that is the scale of a person doing
// that twice.
constexpr int kIdleLingerMs = 5000;

// The two conversions a timer needs. Named rather than written into the
// arithmetic, so the widening below is stated once and the numbers say what
// they are.
constexpr long kMillisecondsPerSecond = 1000;
constexpr long kNanosecondsPerMillisecond = 1000L * 1000L;

int makeTimer() {
    const int fd =
        ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        std::fprintf(stderr, "bindpeek-watch: cannot make a timer: %s\n",
                     std::strerror(errno));
    }
    return fd;
}

void arm(int fd, int milliseconds, bool repeating) {
    itimerspec when{};
    when.it_value.tv_sec = milliseconds / kMillisecondsPerSecond;
    when.it_value.tv_nsec =
        (milliseconds % kMillisecondsPerSecond) * kNanosecondsPerMillisecond;
    if (repeating) {
        when.it_interval = when.it_value;
    }
    ::timerfd_settime(fd, 0, &when, nullptr);
}

void disarm(int fd) {
    const itimerspec never{};
    ::timerfd_settime(fd, 0, &never, nullptr);
}

void drain(int fd) {
    std::uint64_t ticks = 0;
    while (::read(fd, &ticks, sizeof ticks) > 0) {
    }
}

// The two signals a service manager uses to end a service, taken as a
// descriptor so that the wait below has one thing to wait on and no handler
// runs in the middle of anything.
int makeSignalFd() {
    sigset_t ending;
    sigemptyset(&ending);
    sigaddset(&ending, SIGTERM);
    sigaddset(&ending, SIGINT);
    if (sigprocmask(SIG_BLOCK, &ending, nullptr) < 0) {
        return -1;
    }
    return ::signalfd(-1, &ending, SFD_NONBLOCK | SFD_CLOEXEC);
}

} // namespace

int main() {
    // Belt and braces beside MSG_NOSIGNAL: a client that goes away must not be
    // able to end the process that is holding the keyboards.
    ::signal(SIGPIPE, SIG_IGN);

    const int signals = makeSignalFd();
    const int resyncTimer = makeTimer();
    const int idleTimer = makeTimer();
    if (signals < 0 || resyncTimer < 0 || idleTimer < 0) {
        return 1;
    }

    Modifiers state;
    Server server;
    if (!server.start()) {
        return 1;
    }

    Devices devices(state);
    if (!devices.start()) {
        return 1;
    }

    arm(resyncTimer, kResyncIntervalMs, true);
    // Armed from the start: the socket unit begins this process when somebody
    // connects, but nothing says they will finish doing so, and a panel that
    // dies between the two would otherwise leave this running for the session.
    arm(idleTimer, kIdleLingerMs, false);
    bool lingering = true;

    std::vector<pollfd> fds;
    while (true) {
        fds.clear();
        fds.push_back(pollfd{signals, POLLIN, 0});
        fds.push_back(pollfd{resyncTimer, POLLIN, 0});
        fds.push_back(pollfd{idleTimer, POLLIN, 0});
        const std::size_t serverAt = fds.size();
        server.appendPollFds(fds);
        const std::size_t devicesAt = fds.size();
        devices.appendPollFds(fds);

        if (::poll(fds.data(), fds.size(), -1) < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::fprintf(stderr, "bindpeek-watch: poll failed: %s\n",
                         std::strerror(errno));
            return 1;
        }

        if ((fds[0].revents & POLLIN) != 0) {
            return 0;
        }

        if ((fds[2].revents & POLLIN) != 0) {
            drain(idleTimer);
            if (server.clients() == 0) {
                // Nobody is listening any more. Going away closes the
                // keyboards with it, which is the point: nothing holds a
                // keyboard descriptor while the panel is not running.
                return 0;
            }
        }

        bool changed = false;
        bool keyTaken = false;

        if ((fds[1].revents & POLLIN) != 0) {
            drain(resyncTimer);
            changed = devices.resync();
        }

        changed |= devices.dispatch(fds, devicesAt, &keyTaken);
        server.dispatch(fds, serverAt, state.report(false));

        if (changed || keyTaken) {
            server.broadcast(state.report(keyTaken));
        }

        const bool idle = server.clients() == 0;
        if (idle && !lingering) {
            arm(idleTimer, kIdleLingerMs, false);
            lingering = true;
        } else if (!idle && lingering) {
            disarm(idleTimer);
            lingering = false;
        }
    }
}
