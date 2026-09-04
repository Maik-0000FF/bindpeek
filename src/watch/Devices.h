// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <poll.h>

#include <cstddef>
#include <string>
#include <vector>

struct libevdev;

namespace bindpeek::watch {

class Modifiers;

// The keyboards under /dev/input: opened, masked, watched while they live and
// given up when they go away.
//
// Nothing is grabbed. EVIOCGRAB would make this an interceptor and break every
// other consumer of the keyboard; the whole point is to stay passive.
//
// The mask is set on every descriptor before anything is read from it. It
// leaves the keys and the report that ends a packet, and takes away every
// other kind of event a keyboard can send, the raw scancodes of EV_MSC above
// all: those name the key a second time, and this service has no use for a
// second name.
class Devices {
public:
    explicit Devices(Modifiers &state);
    ~Devices();

    Devices(const Devices &) = delete;
    Devices &operator=(const Devices &) = delete;

    // Opens what is there and starts watching for what appears later. No
    // keyboard yet is not a failure: one may be plugged in a second later, and
    // a Bluetooth keyboard usually is. A directory that cannot be watched is,
    // because such a keyboard would then stay invisible for as long as this
    // runs.
    bool start();

    // Adds what has to be waited on. Between this and dispatch the list does
    // not change, which is what lets the caller pass a plain offset back.
    void appendPollFds(std::vector<pollfd> &out) const;

    // Takes back what became ready, at the offset the caller put it. Sets
    // *keyTaken when a key that is not a modifier went down. Returns true when
    // the held modifiers changed.
    bool dispatch(const std::vector<pollfd> &ready, std::size_t offset,
                  bool *keyTaken);

    // Asks every device what is really pressed and corrects the state from the
    // answer.
    //
    // A key-up can go missing when a device is taken away, when the kernel
    // drops events under load, or when something grabs the keyboard mid-press.
    // The panel would then stand there with a modifier that is long since up,
    // and nothing in the event stream would ever correct it.
    bool resync();

private:
    struct Device {
        // Stable for as long as the device lives, and never reused. The state
        // is keyed on it, so a number that came round again would mix up two
        // keyboards.
        int id = 0;
        std::string path;
        int fd = -1;
        libevdev *dev = nullptr;
    };

    // Both report whether the held modifiers changed. A keyboard can be
    // plugged in with a modifier already down, and that is news the moment it
    // is opened rather than at the next correction a second and a half later.
    bool openDevice(const std::string &path);
    bool scan();
    void retire(std::size_t at);
    // Reads what one device has to say. Returns false when it has gone away.
    bool readFrom(Device &device, bool *changed, bool *keyTaken);

    Modifiers &m_state;
    std::vector<Device> m_devices;
    int m_inotify = -1;
    int m_nextId = 1;
};

} // namespace bindpeek::watch
