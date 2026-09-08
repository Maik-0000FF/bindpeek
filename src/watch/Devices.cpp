// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Devices.h"

#include "Modifiers.h"
#include "Seats.h"

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
#include <systemd/sd-device.h>

namespace bindpeek::watch {
namespace {

// Where the kernel exposes its input devices, and the prefix of the event
// nodes below it.
constexpr char kInputDir[] = "/dev/input";
constexpr char kEventPrefix[] = "event";

// The property udev writes on a device that has been moved to another seat.
constexpr char kSeatProperty[] = "ID_SEAT";

// How many correction beats may ask udev about one device and be told nothing
// before the first seat is taken as the answer.
//
// Two rather than one because the beat is a single timer for every device: a
// keyboard opened a moment before a beat would otherwise be asked again a
// moment later and fall back inside the very window this is here to sit out.
// Two beats are at least one whole interval, and udev's own window is measured
// in microseconds.
constexpr int kSeatBeatsBeforeFallback = 2;

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

// Which seat a device belongs to, and whether that answer is a final one.
//
// The property when it carries one, the first seat when it does not: udev
// writes it only on a device that has been moved with `loginctl attach`.
// Measured on a single-seat machine, where no node under /dev/input carries it
// at all and the answer is the fallback every time.
//
// It reads the udev database under /run, not /proc, so ProtectProc in the unit
// does not blind it.
//
// *settled says whether that database has been written for this device at all,
// which is what tells "udev says no seat" apart from "udev has not spoken
// yet". A keyboard plugged in while this runs is opened on the permissions
// udev grants it, and udev grants those before it writes the database. Asked
// in that window, a keyboard of the second seat would answer with the first,
// and the answer would stand for as long as the device is plugged in, which is
// the very leak this reading exists to close.
//
// That window is read from the order udev does its work in and has never been
// caught: in 21 openings of a freshly plugged keyboard, 8 of them under twice
// the load the machine has cores for, the database had always been written by
// the time it was asked. Small enough never to be met, not small enough to
// leave open.
//
// What is done about it is not to wait. Writing the database does not touch
// the node under /dev/input, so there is no second directory event to wait
// for, and a keyboard held shut until one arrived would stay shut. The device
// is opened and read, and nothing it reports goes anywhere until the seat has
// settled: asked again at the end of the scan that opened it, and after that
// on the correction beat.
//
// A no here is never left standing for good. On a machine with no udev
// database at all, which a container with /dev/input passed into it can be,
// this says no every time it is asked, and the service would drop every key in
// silence; the beats that ask give up after kSeatBeatsBeforeFallback and take
// the first seat instead. That crosses no line, because a second seat is made
// by writing ID_SEAT into this very database: where there is nothing to read,
// nobody has been attached to a second seat and there is none to be at.
//
// `loginctl attach` can still move a device after it has settled, and the
// directory watch sees a node appear or go rather than a property change, so a
// device moved at that point keeps the seat it settled on until it is opened
// again. Closing that needs a udev monitor, which is a larger thing than this.
std::string seatOf(const std::string &path, bool *settled) {
    *settled = false;

    sd_device *device = nullptr;
    if (sd_device_new_from_devname(&device, path.c_str()) < 0) {
        return kDefaultSeat;
    }

    std::string name = kDefaultSeat;
    *settled = sd_device_get_is_initialized(device) > 0;
    const char *seat = nullptr;
    if (sd_device_get_property_value(device, kSeatProperty, &seat) >= 0 &&
        seat != nullptr && *seat != '\0') {
        name = seat;
    }
    sd_device_unref(device);
    return name;
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

Devices::Devices(Seats &state) : m_state(state) {}

Devices::~Devices() {
    while (!m_devices.empty()) {
        retire(m_devices.size() - 1);
    }
    if (m_inotify >= 0) {
        ::close(m_inotify);
    }
}

void Devices::openDevice(const std::string &path) {
    for (const Device &device : m_devices) {
        if (device.path == path) {
            return;
        }
    }

    // Non-blocking: the poll decides when there is something to read, and
    // libevdev then drains what is there.
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return;
    }

    // Before anything is read, so that nothing of the masked kinds is ever in
    // this process at all.
    if (!setEventMask(fd)) {
        std::fprintf(stderr, BINDPEEK_PROGRAM_NAME ": cannot mask %s: %s\n",
                     path.c_str(), std::strerror(errno));
        ::close(fd);
        return;
    }

    libevdev *dev = nullptr;
    if (libevdev_new_from_fd(fd, &dev) < 0) {
        ::close(fd);
        return;
    }
    if (!looksLikeKeyboard(dev)) {
        libevdev_free(dev);
        ::close(fd);
        return;
    }

    bool settled = false;
    std::string seat = seatOf(path, &settled);
    m_devices.push_back(
        Device{m_nextId++, path, std::move(seat), settled, 0, fd, dev});

    // A device whose seat is still provisional says nothing to anybody. What
    // it holds is taken at the moment its seat becomes one, which the end of
    // this scan asks about again.
    if (!settled) {
        return;
    }

    // libevdev asked the device what is down while it was opening, so the
    // answer is already here. Taken now rather than at the first resync: the
    // panel connects when it starts, and by then SUPER may well be held.
    takeWhatIsHeld(m_devices.back());
}

bool Devices::askSeat(Device &device) {
    bool settled = false;
    device.seat = seatOf(device.path, &settled);
    device.seatSettled = settled;
    return settled;
}

void Devices::takeWhatIsHeld(Device &device) {
    std::vector<int> down;
    for (const int code : Modifiers::codes()) {
        if (libevdev_get_event_value(device.dev, EV_KEY, code) != 0) {
            down.push_back(code);
        }
    }
    m_state.reconcile(device.seat, device.id, down);
}

void Devices::scan() {
    DIR *dir = ::opendir(kInputDir);
    if (dir == nullptr) {
        return;
    }
    while (const dirent *entry = ::readdir(dir)) {
        if (std::strncmp(entry->d_name, kEventPrefix,
                         sizeof kEventPrefix - 1) != 0) {
            continue;
        }
        openDevice(std::string{kInputDir} + "/" + entry->d_name);
    }
    ::closedir(dir);

    // Everything still provisional is asked again, here rather than only at
    // the next correction beat. What that is worth depends on which scan this
    // is. The one at the start walks every node, so a device opened early is
    // asked again once the rest have been opened, which is a real second
    // chance. A scan that a hotplug set off opens the one node and reaches it
    // again microseconds later, which is the same width as the window itself:
    // it may catch the answer and it may not, and the beat is what settles it
    // when it does not. Either way this can only shorten the wait, and it
    // costs one question per device that has not answered yet.
    //
    // Worth shortening, because the wait is not a late modifier but a deaf
    // keyboard. The bare fact that some other key went down is what takes the
    // panel off the screen, and dropping it leaves the panel standing over a
    // shortcut that has fired: hold SUPER on the keyboard that was already
    // open, press a letter on the one just plugged in, and the panel would sit
    // there.
    for (Device &device : m_devices) {
        if (!device.seatSettled && askSeat(device)) {
            takeWhatIsHeld(device);
        }
    }
}

bool Devices::start() {
    // IN_ATTRIB as well as IN_CREATE: the kernel creates the node before it
    // grants the permissions, so the first attempt at a fresh keyboard can
    // fail and has to be made again when the permissions arrive.
    m_inotify = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotify < 0) {
        std::fprintf(stderr, BINDPEEK_PROGRAM_NAME ": cannot watch %s: %s\n",
                     kInputDir, std::strerror(errno));
        return false;
    }
    if (::inotify_add_watch(m_inotify, kInputDir,
                            IN_CREATE | IN_ATTRIB | IN_DELETE) < 0) {
        std::fprintf(stderr, BINDPEEK_PROGRAM_NAME ": cannot watch %s: %s\n",
                     kInputDir, std::strerror(errno));
        // Given back rather than left lying about, so that a failed start
        // leaves this exactly as it was before it and says so.
        ::close(m_inotify);
        m_inotify = -1;
        return false;
    }
    scan();
    return true;
}

// The directory watch stands for the whole: it is made first and lives as long
// as the process, so there is no moment at which it is gone and a keyboard is
// still open.
bool Devices::watching() const { return m_inotify >= 0; }

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
//
// Nothing at all before the keyboards are opened, and dispatch reads nothing
// back in that state either.
void Devices::appendPollFds(std::vector<pollfd> &out) const {
    if (!watching()) {
        return;
    }
    for (const Device &device : m_devices) {
        out.push_back(pollfd{device.fd, POLLIN, 0});
    }
    out.push_back(pollfd{m_inotify, POLLIN, 0});
}

bool Devices::readFrom(Device &device) {
    // Read and dropped while the seat is provisional, rather than left unread.
    // The descriptor has to be drained or the poll would return on it forever,
    // and a device that has gone away has to be noticed here or nowhere.
    //
    // Dropped rather than put at the seat it will probably turn out to be:
    // a modifier put at the wrong one could be moved when the answer arrives,
    // but the bare fact that some other key went down could not. That is a
    // fact about one round, it goes out and is forgotten in the same round,
    // and there is nothing left to take back.
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
                if (device.seatSettled && event.type == EV_KEY &&
                    Modifiers::idOf(event.code) != kNoModifier) {
                    if (event.value == kKeyPress) {
                        m_state.press(device.seat, device.id, event.code);
                    } else if (event.value == kKeyRelease) {
                        m_state.release(device.seat, device.id, event.code);
                    }
                }
                rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_SYNC,
                                         &event);
            }
            // Back to reading ordinarily. The replay ends by saying it has no
            // more to say, and taking that as the end of everything would
            // leave whatever arrived after the gap sitting in libevdev's own
            // buffer, where no poll can see it: it would surface at the next
            // keystroke or at the next correction, seconds later.
            rc = libevdev_next_event(device.dev, LIBEVDEV_READ_FLAG_NORMAL,
                                     &event);
            continue;
        }

        if (device.seatSettled && event.type == EV_KEY) {
            if (Modifiers::idOf(event.code) == kNoModifier) {
                // Not a modifier, and this is the whole of what is learned
                // about it: that one went down. A release is the tail of that
                // and carries nothing new.
                if (event.value == kKeyPress) {
                    m_state.takeKey(device.seat);
                }
            } else if (event.value == kKeyPress) {
                m_state.press(device.seat, device.id, event.code);
            } else if (event.value == kKeyRelease) {
                m_state.release(device.seat, device.id, event.code);
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

void Devices::dispatch(const std::vector<pollfd> &ready, std::size_t offset) {
    // Nothing was put into the array, so nothing is taken back out of it.
    if (!watching()) {
        return;
    }

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
        if (!readFrom(m_devices[index])) {
            // A key held on the device that just vanished can never be
            // released, so what it was holding is dropped here. A device that
            // never settled held nothing anybody was told about, and asking
            // for it would bring a seat into being that no keyboard was ever
            // opened for.
            if (m_devices[index].seatSettled) {
                m_state.forget(m_devices[index].seat, m_devices[index].id);
            }
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
        scan();
    }
}

void Devices::resync() {
    for (std::size_t at = m_devices.size(); at > 0; --at) {
        Device &device = m_devices[at - 1];

        // Still no answer from udev when this device was opened, and none at
        // the end of that scan either, so nothing it has reported has gone
        // anywhere. Asked once more here; the correction below then takes
        // everything it holds, at the seat it turns out to belong to.
        if (!device.seatSettled && !askSeat(device)) {
            ++device.seatBeats;
            if (device.seatBeats < kSeatBeatsBeforeFallback) {
                continue;
            }

            // Asked this often and told nothing this late, udev is not going
            // to answer at all: its own window is microseconds wide, and this
            // is whole beats past it. So the guess is taken as the answer,
            // which loses nothing that could have been kept: a second seat is
            // made by writing ID_SEAT into the database that is not there, so
            // a machine which has none of it has no second seat either. Going
            // on dropping every key in silence would be the worse answer.
            std::fprintf(stderr,
                         BINDPEEK_PROGRAM_NAME
                         ": udev says nothing about %s, reading it as %s\n",
                         device.path.c_str(), kDefaultSeat);
            device.seat = kDefaultSeat;
            device.seatSettled = true;
        }

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

        takeWhatIsHeld(device);
    }
}

} // namespace bindpeek::watch
