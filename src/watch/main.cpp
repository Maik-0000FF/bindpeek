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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Devices.h"
#include "Seats.h"
#include "Server.h"

namespace {

using bindpeek::watch::Devices;
using bindpeek::watch::Seats;
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
        std::fprintf(stderr,
                     BINDPEEK_PROGRAM_NAME ": cannot make a timer: %s\n",
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

// What a start from a shell is answered with, before anything is opened.
//
// The socket unit starts this service with no argument at all, so an argument
// only ever comes from somebody typing the program's name, and what they are
// asking is which program and which build they have in front of them. The
// panel and the settings window answer the same two options through Qt's
// parser; this program carries no Qt and says the same two values from the
// build itself.
//
// Nothing here is translated. A catalogue is a Qt one, and what this program
// is made of is the argument for trusting it with the keyboards.
//
// The whole line is read before anything is answered, and in the order Qt's
// parser keeps for the other two programs: an argument that is no option is
// refused first, and only a line that holds nothing else is answered.
//
//   argv                   answer
//   (none)                 false, the service runs as it always has
//   --version, -v          the name and the version, and done
//   --help, -h             what starts this service, and done
//   --version --help       the version: it is asked first, as it is there
//   --foo --version        refused, and the version is not printed
//   --version --foo        refused as well, wherever the unknown one stands
//
// True when the command line was answered here and the service is not to run.
// What to leave with is written to status.
bool answerArguments(int argc, char **argv, int &status) {
    bool version = false;
    bool help = false;

    for (int i = 1; i < argc; ++i) {
        const char *const argument = argv[i];
        if (std::strcmp(argument, "--version") == 0 ||
            std::strcmp(argument, "-v") == 0) {
            version = true;
            continue;
        }
        if (std::strcmp(argument, "--help") == 0 ||
            std::strcmp(argument, "-h") == 0) {
            help = true;
            continue;
        }
        // No option of this program takes a value, so there is nothing here
        // that could be one: whatever stands there was meant as an option and
        // is not one.
        std::fprintf(stderr,
                     BINDPEEK_PROGRAM_NAME ": unknown option %s. Try --help.\n",
                     argument);
        status = 1;
        return true;
    }

    if (version) {
        std::printf("%s %s\n", BINDPEEK_PROGRAM_NAME, BINDPEEK_VERSION);
        status = 0;
        return true;
    }
    if (help) {
        std::printf("Usage: %s\n"
                    "Reads which modifier keys are held, for the bindpeek "
                    "overlay.\n"
                    "\n"
                    "Started by %s, not by hand.\n"
                    "\n"
                    "Options:\n"
                    "  -h, --help     Displays this help.\n"
                    "  -v, --version  Displays version information.\n",
                    BINDPEEK_PROGRAM_NAME, BINDPEEK_WATCH_SOCKET_UNIT);
        status = 0;
        return true;
    }
    return false;
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

int main(int argc, char **argv) {
    // First of all, so that a question about this program is answered by a
    // process that has opened nothing.
    int answer = 0;
    if (answerArguments(argc, argv, answer)) {
        return answer;
    }

    // Belt and braces beside MSG_NOSIGNAL: a client that goes away must not be
    // able to end the process that is holding the keyboards.
    ::signal(SIGPIPE, SIG_IGN);

    const int signals = makeSignalFd();
    const int resyncTimer = makeTimer();
    const int idleTimer = makeTimer();
    if (signals < 0 || resyncTimer < 0 || idleTimer < 0) {
        return 1;
    }

    Seats state;
    Server server;
    if (!server.start()) {
        return 1;
    }

    // Made, not started. The socket is open to everyone and the decision about
    // who is served is taken here, so anybody local can have this process
    // begun; opening the keyboards at that point would open them for somebody
    // who is about to be turned away. They are opened further down, once a
    // client has passed the check.
    Devices devices(state);

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
            std::fprintf(stderr, BINDPEEK_PROGRAM_NAME ": poll failed: %s\n",
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

        bool onTheBeat = false;

        if ((fds[1].revents & POLLIN) != 0) {
            drain(resyncTimer);
            devices.resync();
            onTheBeat = true;
        }

        devices.dispatch(fds, devicesAt);

        // The last reader of the poll answers, and therefore the last moment
        // at which the client list still has the length those answers were
        // counted from. Everything below shortens it, so everything below
        // comes after this line: broadcast drops whoever it cannot reach, and
        // dropStrangers drops whoever left their seat.
        server.dispatch(fds, serverAt);

        // The first client has passed the check, so now there is somebody to
        // read the keyboards for. Nothing above this line has opened one, and
        // a caller who is refused starts a process that opens nothing and goes
        // away when the idle timer fires.
        //
        // Whoever is waiting has not joined yet, so there is nobody in the
        // list to miss what this finds; the state it leaves behind is what
        // they are sent a few lines below.
        if (!devices.watching() && server.waiting() > 0 && !devices.start()) {
            return 1;
        }

        // Seat by seat, because what is held at one of them is nothing to do
        // with the person at the other. Which seats have anything to say is
        // the state's own answer: it keeps the record that last went out and
        // compares, so nothing has to be threaded back from the reading.
        for (std::size_t at = 0; at < state.count(); ++at) {
            if (state.hasNews(at)) {
                server.broadcast(state.nameAt(at), state.report(at));
            }
        }
        state.settle();

        // Only now do the ones accepted a moment ago join, each given the
        // state of their own seat as it stands on the way in, so that a panel
        // which connected during this very round is not told a key was taken
        // before it existed.
        server.admit(state);

        // On the correction beat rather than at every keystroke: somebody
        // whose session was switched away from is no longer at a screen of
        // this machine and must stop being told, and a second and a half is
        // soon enough for that while costing nothing in between.
        if (onTheBeat) {
            server.dropStrangers();
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
