// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WatchClient.h"

#include "Protocol.h"
#include "Source.h"

#include <QSocketNotifier>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace bindpeek {
namespace {

using watch::Report;

// How long before knocking again after the connection went. Short enough that
// a restarted service is picked up before the next time the user reaches for a
// modifier, long enough that a service which cannot start is not asked ten
// times a second.
constexpr int kRetryMs = 2000;

// The name each modifier goes by once it is out of the service. The service
// sends numbers, because names are an interface matter and it has no
// interface; this is the one place the two are joined.
QString nameOf(std::uint8_t id) {
    switch (id) {
    case watch::kSuper:
        return QString::fromLatin1(modifier::kSuper);
    case watch::kCtrl:
        return QString::fromLatin1(modifier::kCtrl);
    case watch::kAlt:
        return QString::fromLatin1(modifier::kAlt);
    case watch::kShift:
        return QString::fromLatin1(modifier::kShift);
    default:
        return {};
    }
}

} // namespace

Heard hear(const void *record, std::size_t bytes) {
    Heard out;
    if (bytes != sizeof(Report)) {
        return out;
    }

    Report copy{};
    std::memcpy(&copy, record, sizeof copy);

    if (copy.version != watch::kProtocolVersion) {
        out.wrongVersion = true;
        return out;
    }
    if (copy.count > watch::kMaxHeld) {
        return out;
    }

    for (std::size_t at = 0; at < copy.count; ++at) {
        const QString name = nameOf(copy.held[at]);
        if (name.isEmpty()) {
            // A number this panel has no name for, inside a record whose
            // version says it should. Refused whole rather than shown with a
            // gap in it.
            return Heard{};
        }
        out.held.append(name);
    }

    out.understood = true;
    out.keyTaken = (copy.flags & watch::kFlagKeyTaken) != 0;
    return out;
}

WatchClient::WatchClient(QObject *parent) : QObject(parent) {
    m_retry.setInterval(kRetryMs);
    connect(&m_retry, &QTimer::timeout, this, [this]() {
        if (openConnection()) {
            m_retry.stop();
        }
    });
}

WatchClient::~WatchClient() { closeConnection(); }

QString WatchClient::socketPath() {
    return QString::fromLatin1(watch::kSocketPath);
}

QString WatchClient::socketUnit() {
    return QString::fromLatin1(BINDPEEK_WATCH_SOCKET_UNIT);
}

bool WatchClient::openConnection() {
    // SOCK_SEQPACKET because that is what the service listens on: the kernel
    // keeps each record whole, so there is no length to read and nothing to
    // reassemble.
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_un at{};
    at.sun_family = AF_UNIX;
    const QByteArray path = socketPath().toLocal8Bit();
    if (static_cast<std::size_t>(path.size()) >= sizeof at.sun_path) {
        ::close(fd);
        return false;
    }
    std::memcpy(at.sun_path, path.constData(),
                static_cast<std::size_t>(path.size()));

    if (::connect(fd, reinterpret_cast<sockaddr *>(&at), sizeof at) < 0) {
        ::close(fd);
        return false;
    }

    // Non-blocking only after the connection stands. A connection to a socket
    // in the file system is made or refused there and then, so nothing is won
    // by handling it in two steps, and the read below must never wait.
    if (::fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        ::close(fd);
        return false;
    }

    m_fd = fd;
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this,
            &WatchClient::onReadable);
    return true;
}

void WatchClient::closeConnection() {
    if (m_notifier != nullptr) {
        // Taken off the descriptor before it is closed. A notifier left on a
        // number the kernel may hand to the next open would fire on somebody
        // else's file.
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

bool WatchClient::start() { return openConnection(); }

void WatchClient::onReadable() {
    while (true) {
        Report record{};
        const ssize_t got = ::recv(m_fd, &record, sizeof record, 0);
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }
            break;
        }
        if (got == 0) {
            // The service has gone. It ends itself when the last panel leaves,
            // and it is replaced when a new one is installed; either way the
            // socket is still there to knock at.
            break;
        }

        const Heard heard = hear(&record, static_cast<std::size_t>(got));
        if (!heard.understood) {
            if (heard.wrongVersion && !m_saidWrongVersion) {
                // Said once, and once is the point: without it a panel left
                // running across an upgrade of the package goes quiet and
                // knocks every two seconds for the rest of the session with
                // nothing to show for it.
                m_saidWrongVersion = true;
                std::fprintf(stderr,
                             "bindpeek: the keyboard service speaks a version "
                             "this panel does not know. Restart the panel.\n");
            }
            break;
        }

        // The order matters: whoever hears that the shortcut was taken asks
        // what is held while doing so, and must be told the new answer first.
        if (heard.held != m_held) {
            m_held = heard.held;
            emit heldChanged(m_held);
        }
        if (heard.keyTaken) {
            emit shortcutTaken();
        }
    }

    closeConnection();

    // Nothing is held any more as far as this panel can tell, and saying so is
    // what takes it off the screen. Standing there with a modifier that
    // nothing will ever release is the one outcome to avoid.
    if (!m_held.isEmpty()) {
        m_held.clear();
        emit heldChanged(m_held);
    }
    m_retry.start();
}

} // namespace bindpeek
