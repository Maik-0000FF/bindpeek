// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "KeyboardWatch.h"

#include "Source.h"

#include <QDir>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSocketNotifier>

#include <libevdev/libevdev.h>

#include <fcntl.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

#include <cerrno>

namespace bindpeek {
namespace {

// Where the kernel exposes its input devices, and the prefix of the event
// nodes below it.
constexpr char kInputDir[] = "/dev/input";
constexpr char kEventPrefix[] = "event";

// The three states the kernel reports for a key. Auto-repeat is not a state
// change and is ignored: a held SUPER must not look like a new press.
constexpr int kKeyRelease = 0;
constexpr int kKeyPress = 1;

// How often the real state of the modifier keys is fetched from the devices.
// Rare enough to cost nothing, often enough that a stuck panel corrects itself
// before it becomes annoying.
constexpr int kResyncIntervalMs = 1500;

// Maps a kernel key code onto a canonical modifier name. Both sides of the
// keyboard map to the same name: nobody thinks of left and right SUPER as two
// different modifiers.
//
// KEY_RIGHTALT is AltGr on many layouts. It is counted as ALT because that is
// what a compositor binds it as; a layout that uses it for characters emits
// those characters through a different code anyway.
const QHash<int, QString> &modifierCodes() {
    static const QHash<int, QString> table = {
        {KEY_LEFTMETA, QString::fromLatin1(modifier::kSuper)},
        {KEY_RIGHTMETA, QString::fromLatin1(modifier::kSuper)},
        {KEY_LEFTCTRL, QString::fromLatin1(modifier::kCtrl)},
        {KEY_RIGHTCTRL, QString::fromLatin1(modifier::kCtrl)},
        {KEY_LEFTALT, QString::fromLatin1(modifier::kAlt)},
        {KEY_RIGHTALT, QString::fromLatin1(modifier::kAlt)},
        {KEY_LEFTSHIFT, QString::fromLatin1(modifier::kShift)},
        {KEY_RIGHTSHIFT, QString::fromLatin1(modifier::kShift)},
    };
    return table;
}

// A device counts as a keyboard when it can report the letter range and
// space. Mice, touchpads and volume rockers also carry EV_KEY, so the event
// type alone is not enough to tell them apart.
bool looksLikeKeyboard(libevdev *dev) {
    if (libevdev_has_event_type(dev, EV_KEY) == 0) {
        return false;
    }
    return libevdev_has_event_code(dev, EV_KEY, KEY_A) != 0 &&
           libevdev_has_event_code(dev, EV_KEY, KEY_Z) != 0 &&
           libevdev_has_event_code(dev, EV_KEY, KEY_SPACE) != 0;
}

} // namespace

KeyboardWatch::KeyboardWatch(QObject *parent) : QObject(parent) {
    m_resync.setInterval(kResyncIntervalMs);
    connect(&m_resync, &QTimer::timeout, this, &KeyboardWatch::resync);
}

KeyboardWatch::~KeyboardWatch() {
    m_shuttingDown = true;
    // Through retireDevice rather than closing the descriptors here: it takes
    // the notifier off the descriptor first. Closing while a notifier still
    // watches it leaves that notifier on a number the kernel may hand to the
    // next open, and the notifiers are only destroyed afterwards as children
    // of this object.
    for (Device &device : m_devices) {
        retireDevice(device);
    }
}

QStringList KeyboardWatch::held() const { return m_held.names(); }

bool KeyboardWatch::openDevice(const QString &path) {
    for (const Device &device : m_devices) {
        if (device.path == path && !device.dead) {
            return false;
        }
    }

    // Non-blocking: the notifier decides when there is something to read, and
    // libevdev_next_event then drains what is there.
    const int fd = ::open(path.toLocal8Bit().constData(),
                          O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    libevdev *dev = nullptr;
    if (libevdev_new_from_fd(fd, &dev) < 0) {
        ::close(fd);
        return false;
    }
    if (!looksLikeKeyboard(dev)) {
        libevdev_free(dev);
        ::close(fd);
        return false;
    }

    m_devices.push_back(Device{path, fd, dev, nullptr, false});

    // Capture the index, never a reference or pointer into the vector: a later
    // push_back may move the elements, and the lambda would then read freed
    // memory. Dead devices are marked rather than erased for the same reason.
    const std::size_t index = m_devices.size() - 1;

    auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this,
            [this, index]() { readFrom(index); });
    m_devices[index].notifier = notifier;
    return true;
}

int KeyboardWatch::start() {
    const QDir directory{QLatin1String(kInputDir)};
    const QStringList nodes =
        directory.entryList({QLatin1String(kEventPrefix) + QStringLiteral("*")},
                            QDir::System | QDir::Files, QDir::Name);

    for (const QString &node : nodes) {
        openDevice(directory.filePath(node));
    }

    // Watching the directory is what makes a keyboard plugged in later show
    // up. Without it a Bluetooth keyboard connecting after start would stay
    // invisible for the rest of the session.
    m_dirWatcher = new QFileSystemWatcher(this);
    if (m_dirWatcher->addPath(QLatin1String(kInputDir))) {
        connect(m_dirWatcher, &QFileSystemWatcher::directoryChanged, this,
                &KeyboardWatch::onInputDirChanged);
    }

    if (!m_devices.empty()) {
        m_resync.start();
    }
    return static_cast<int>(m_devices.size());
}

void KeyboardWatch::onInputDirChanged() {
    const QDir directory{QLatin1String(kInputDir)};
    const QStringList nodes =
        directory.entryList({QLatin1String(kEventPrefix) + QStringLiteral("*")},
                            QDir::System | QDir::Files, QDir::Name);

    bool added = false;
    for (const QString &node : nodes) {
        added = openDevice(directory.filePath(node)) || added;
    }
    if (added && !m_resync.isActive()) {
        m_resync.start();
    }
}

// Stops listening to a device that has gone away.
//
// Without this the notifier keeps firing on a hung-up descriptor while
// libevdev returns the same error forever, which is a busy loop at full CPU
// rather than a quiet failure.
void KeyboardWatch::retireDevice(Device &device) {
    if (device.dead) {
        return;
    }
    device.dead = true;
    if (device.notifier != nullptr) {
        device.notifier->setEnabled(false);
        device.notifier->deleteLater();
        device.notifier = nullptr;
    }
    if (device.dev != nullptr) {
        libevdev_free(device.dev);
        device.dev = nullptr;
    }
    if (device.fd >= 0) {
        ::close(device.fd);
        device.fd = -1;
    }

    // A key held on the device that just vanished can never be released, so
    // the state is rebuilt from what is left. Skipped while shutting down:
    // there is nobody left to tell.
    if (!m_shuttingDown) {
        resync();
    }
}

void KeyboardWatch::readFrom(std::size_t index) {
    Device &device = m_devices[index];
    if (device.dead || device.dev == nullptr) {
        return;
    }

    input_event event{};
    int rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_NORMAL, &event);
    while (rc == LIBEVDEV_READ_STATUS_SUCCESS ||
           rc == LIBEVDEV_READ_STATUS_SYNC) {
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // The device dropped events and libevdev is replaying the current
            // state. Follow it to the end, otherwise a modifier released
            // during the gap would stay stuck as held.
            //
            // Only modifiers are taken from the replay: an ordinary key that
            // happens to be down is reported here as if it had just been
            // pressed, and treating that as "the shortcut was taken" would
            // hide the panel for a keystroke that never happened.
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                if (event.type == EV_KEY &&
                    !modifierCodes().value(event.code).isEmpty()) {
                    handleKey(device, event.code, event.value);
                }
                rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_SYNC,
                                         &event);
            }
            continue;
        }
        if (event.type == EV_KEY) {
            handleKey(device, event.code, event.value);
        }
        rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_NORMAL, &event);
    }

    // Anything but "no more events" means the device is gone: unplugged, or a
    // Bluetooth keyboard that disconnected.
    if (rc != -EAGAIN) {
        retireDevice(device);
    }
}

void KeyboardWatch::handleKey(const Device &source, int code, int value) {
    const QString name = modifierCodes().value(code);

    if (name.isEmpty()) {
        // Not a modifier. A press means the user has just taken the shortcut
        // they were looking at; a release is the tail of that and carries no
        // new information.
        if (value == kKeyPress) {
            emit shortcutTaken();
        }
        return;
    }

    bool changed = false;
    if (value == kKeyPress) {
        changed = m_held.press(name);
    } else if (value == kKeyRelease) {
        // Only when no other key of the same role is still down. With two
        // keyboards attached, or with both shift keys, one release must not
        // clear a modifier the other is still holding.
        //
        // The key that produced this event is skipped on its own device only:
        // skipping it everywhere would ignore the very case this guards, the
        // same physical key held down on a second keyboard.
        bool stillDown = false;
        for (auto it = modifierCodes().cbegin();
             it != modifierCodes().cend() && !stillDown; ++it) {
            if (it.value() != name) {
                continue;
            }
            for (const Device &device : m_devices) {
                if (device.dead || device.dev == nullptr) {
                    continue;
                }
                if (&device == &source && it.key() == code) {
                    continue;
                }
                if (libevdev_get_event_value(device.dev, EV_KEY, it.key()) !=
                    0) {
                    stillDown = true;
                    break;
                }
            }
        }
        if (!stillDown) {
            changed = m_held.release(name);
        }
    }
    // Auto-repeat (value 2) changes nothing.

    if (changed) {
        emit heldChanged(held());
    }
}

// Reads the current state straight from the devices instead of trusting the
// event stream, and rebuilds the held set from it.
//
// FORCE_SYNC makes libevdev compare its own picture with the device and hand
// back the difference as events, which is exactly what is needed after a
// missed key-up: the correction arrives as the release that never came.
void KeyboardWatch::resync() {
    QSet<QString> actual;
    for (Device &device : m_devices) {
        if (device.dead || device.dev == nullptr) {
            continue;
        }
        input_event event{};
        int rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_FORCE_SYNC,
                                     &event);
        while (rc == LIBEVDEV_READ_STATUS_SYNC) {
            rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_SYNC,
                                     &event);
        }
        for (auto it = modifierCodes().cbegin(); it != modifierCodes().cend();
             ++it) {
            if (libevdev_get_event_value(device.dev, EV_KEY, it.key()) != 0) {
                actual.insert(it.value());
            }
        }
    }

    if (!m_held.reconcile(actual)) {
        return;
    }
    emit heldChanged(held());
}

} // namespace bindpeek
