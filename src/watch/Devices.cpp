// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Devices.h"

#include "Modifiers.h"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>

#include <linux/input.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <libevdev/libevdev.h>

namespace bindpeek::watch {
namespace {

// Where the kernel exposes its input devices, and the prefix of the event
// nodes below it.
constexpr char kInputDir[] = "/dev/input";
constexpr char kEventPrefix[] = "event";

// The two states of a key that are state changes. Auto-repeat is the third and
// is not one: a held SUPER must not look like a new press.
constexpr int kKeyRelease = 0;
constexpr int kKeyPress = 1;

constexpr std::size_t kBitsPerLong = 8 * sizeof(unsigned long);

void setBit(unsigned long *bits, int at) {
    bits[at / kBitsPerLong] |= 1UL << (at % kBitsPerLong);
}

// Tells the kernel which kinds of event this descriptor wants to be handed.
// Type zero addresses the mask over the types themselves rather than over the
// codes of one type.
//
// Measured against the kernel rather than assumed: libevdev opens such a
// descriptor without complaint, its picture of the device stays whole because
// it takes that from ioctls rather than from the stream, the events of the
// masked types do not arrive, and the state correction still works.
bool setEventMask(int fd) {
    unsigned long types[(EV_CNT + kBitsPerLong - 1) / kBitsPerLong] = {0};
    setBit(types, EV_SYN);
    setBit(types, EV_KEY);

    input_mask mask{};
    mask.type = EV_SYN;
    mask.codes_size = sizeof types;
    mask.codes_ptr =
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(types));
    return ioctl(fd, EVIOCSMASK, &mask) == 0;
}

// A device counts as a keyboard when it can report the letter range and space.
// Mice, touchpads and volume rockers also carry EV_KEY, so the event type
// alone is not enough to tell them apart.
bool looksLikeKeyboard(libevdev *dev) {
    if (libevdev_has_event_type(dev, EV_KEY) == 0) {
        return false;
    }
    return libevdev_has_event_code(dev, EV_KEY, KEY_A) != 0 &&
           libevdev_has_event_code(dev, EV_KEY, KEY_Z) != 0 &&
           libevdev_has_event_code(dev, EV_KEY, KEY_SPACE) != 0;
}

} // namespace

Devices::Devices(Modifiers &state) : m_state(state) {}

Devices::~Devices() {
    while (!m_devices.empty()) {
        retire(m_devices.size() - 1);
    }
    if (m_inotify >= 0) {
        ::close(m_inotify);
    }
}

bool Devices::openDevice(const std::string &path) {
    for (const Device &device : m_devices) {
        if (device.path == path) {
            return false;
        }
    }

    // Non-blocking: the poll decides when there is something to read, and
    // libevdev then drains what is there.
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }

    // Before anything is read, so that nothing of the masked kinds is ever in
    // this process at all.
    if (!setEventMask(fd)) {
        std::fprintf(stderr, "bindpeek-watch: cannot mask %s: %s\n",
                     path.c_str(), std::strerror(errno));
        ::close(fd);
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

    m_devices.push_back(Device{m_nextId++, path, fd, dev});

    // libevdev asked the device what is down while it was opening, so the
    // answer is already here. Taken now rather than at the first resync: the
    // panel connects when it starts, and by then SUPER may well be held.
    std::vector<int> down;
    for (const int code : Modifiers::codes()) {
        if (libevdev_get_event_value(dev, EV_KEY, code) != 0) {
            down.push_back(code);
        }
    }
    return m_state.reconcile(m_devices.back().id, down);
}

bool Devices::scan() {
    bool changed = false;
    DIR *dir = ::opendir(kInputDir);
    if (dir == nullptr) {
        return changed;
    }
    while (const dirent *entry = ::readdir(dir)) {
        if (std::strncmp(entry->d_name, kEventPrefix,
                         sizeof kEventPrefix - 1) != 0) {
            continue;
        }
        changed |= openDevice(std::string{kInputDir} + "/" + entry->d_name);
    }
    ::closedir(dir);
    return changed;
}

bool Devices::start() {
    // IN_ATTRIB as well as IN_CREATE: the kernel creates the node before it
    // grants the permissions, so the first attempt at a fresh keyboard can
    // fail and has to be made again when the permissions arrive.
    m_inotify = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotify < 0) {
        std::fprintf(stderr, "bindpeek-watch: cannot watch %s: %s\n", kInputDir,
                     std::strerror(errno));
        return false;
    }
    if (::inotify_add_watch(m_inotify, kInputDir,
                            IN_CREATE | IN_ATTRIB | IN_DELETE) < 0) {
        std::fprintf(stderr, "bindpeek-watch: cannot watch %s: %s\n", kInputDir,
                     std::strerror(errno));
        return false;
    }
    scan();
    return true;
}

void Devices::retire(std::size_t at) {
    Device &device = m_devices[at];
    if (device.dev != nullptr) {
        libevdev_free(device.dev);
    }
    if (device.fd >= 0) {
        ::close(device.fd);
    }
    m_devices.erase(m_devices.begin() + static_cast<std::ptrdiff_t>(at));
}

// The devices first and in their own order, the directory watch last. dispatch
// reads the answers back at exactly those places, so the order here is not a
// matter of taste.
void Devices::appendPollFds(std::vector<pollfd> &out) const {
    for (const Device &device : m_devices) {
        out.push_back(pollfd{device.fd, POLLIN, 0});
    }
    out.push_back(pollfd{m_inotify, POLLIN, 0});
}

bool Devices::readFrom(Device &device, bool *changed, bool *keyTaken) {
    input_event event{};
    int rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_NORMAL, &event);
    while (rc == LIBEVDEV_READ_STATUS_SUCCESS ||
           rc == LIBEVDEV_READ_STATUS_SYNC) {
        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // The device dropped events and libevdev is replaying the state it
            // found. Followed to the end, or a modifier released during the
            // gap would stay held for good.
            //
            // Only modifiers are taken from the replay. An ordinary key that
            // happens to be down is replayed as if it had just been pressed,
            // and reporting that as a taken shortcut would hide the panel for
            // a keystroke that never happened.
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                if (event.type == EV_KEY &&
                    Modifiers::idOf(event.code) != kNoModifier) {
                    if (event.value == kKeyPress) {
                        *changed |= m_state.press(device.id, event.code);
                    } else if (event.value == kKeyRelease) {
                        *changed |= m_state.release(device.id, event.code);
                    }
                }
                rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_SYNC,
                                         &event);
            }
            continue;
        }

        if (event.type == EV_KEY) {
            if (Modifiers::idOf(event.code) == kNoModifier) {
                // Not a modifier, and this is the whole of what is learned
                // about it: that one went down. A release is the tail of that
                // and carries nothing new.
                if (event.value == kKeyPress) {
                    *keyTaken = true;
                }
            } else if (event.value == kKeyPress) {
                *changed |= m_state.press(device.id, event.code);
            } else if (event.value == kKeyRelease) {
                *changed |= m_state.release(device.id, event.code);
            }
        }
        rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_NORMAL, &event);
    }

    // Anything but "no more events" means the device is gone: unplugged, or a
    // Bluetooth keyboard that disconnected. Reported rather than retried,
    // because libevdev would hand back the same error forever and the poll
    // would spin at full speed on a descriptor that has hung up.
    return rc == -EAGAIN;
}

bool Devices::dispatch(const std::vector<pollfd> &ready, std::size_t offset,
                       bool *keyTaken) {
    bool changed = false;

    // Taken before anything below can retire a device. The directory watch was
    // put after the devices when the array was filled, so its place in the
    // array follows the count as it was then, not as it ends up.
    const std::size_t asAppended = m_devices.size();

    // Walked from the back so that retiring one does not move the ones still
    // to be looked at.
    for (std::size_t at = m_devices.size(); at > 0; --at) {
        const std::size_t index = at - 1;
        const pollfd &entry = ready[offset + index];
        if (entry.revents == 0) {
            continue;
        }
        if (!readFrom(m_devices[index], &changed, keyTaken)) {
            // A key held on the device that just vanished can never be
            // released, so what it was holding is dropped here.
            changed |= m_state.forget(m_devices[index].id);
            retire(index);
        }
    }

    const pollfd &watch = ready[offset + asAppended];
    if (watch.revents != 0) {
        // Drained and thrown away: what changed in there does not matter, only
        // that something did, and the answer to that is always to look again.
        char buffer[4096];
        while (::read(m_inotify, buffer, sizeof buffer) > 0) {
        }
        changed |= scan();
    }

    return changed;
}

bool Devices::resync() {
    bool changed = false;
    for (std::size_t at = m_devices.size(); at > 0; --at) {
        Device &device = m_devices[at - 1];

        // FORCE_SYNC makes libevdev compare its own picture with the device
        // and hand back the difference as events, which is exactly what is
        // wanted after a missed key-up: the correction arrives as the release
        // that never came.
        input_event event{};
        int rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_FORCE_SYNC,
                                     &event);
        while (rc == LIBEVDEV_READ_STATUS_SYNC) {
            rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_SYNC,
                                     &event);
        }

        std::vector<int> down;
        for (const int code : Modifiers::codes()) {
            if (libevdev_get_event_value(device.dev, EV_KEY, code) != 0) {
                down.push_back(code);
            }
        }
        changed |= m_state.reconcile(device.id, down);
    }
    return changed;
}

} // namespace bindpeek::watch
