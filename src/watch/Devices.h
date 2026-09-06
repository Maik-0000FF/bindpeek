// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <poll.h>

#include <cstddef>
#include <string>
#include <vector>

struct libevdev;

namespace bindpeek::watch {

class Seats;

// The keyboards under /dev/input: opened, masked, watched while they live and
// given up when they go away.
//
// Each one is asked which seat it belongs to when it is opened, and what it
// reports goes to that seat alone. One plugged in while this runs can be
// opened before udev has written what it knows about it; until that answer
// arrives the device is read and what it says is dropped, so that nothing of
// it can reach the wrong seat.
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
    explicit Devices(Seats &state);
    ~Devices();

    Devices(const Devices &) = delete;
    Devices &operator=(const Devices &) = delete;

    // Opens what is there and starts watching for what appears later. No
    // keyboard yet is not a failure: one may be plugged in a second later, and
    // a Bluetooth keyboard usually is. A directory that cannot be watched is,
    // because such a keyboard would then stay invisible for as long as this
    // runs.
    //
    // Called when the first client has passed the check at the socket, not at
    // the start of the process. Anybody can connect, so anybody could
    // otherwise have every keyboard on the machine opened and only then be
    // turned away.
    bool start();

    // Whether the keyboards are open. False from the start of the process
    // until the first client is let in, and in that state this holds no
    // descriptor and has nothing to be polled.
    bool watching() const;

    // Adds what has to be waited on. Between this and dispatch the list does
    // not change, which is what lets the caller pass a plain offset back.
    void appendPollFds(std::vector<pollfd> &out) const;

    // Takes back what became ready, at the offset the caller put it, and puts
    // what it reads into the seat each device belongs to. Nothing is handed
    // back: what changed is a question for the state, which answers it by
    // comparing the record with the one that went out.
    void dispatch(const std::vector<pollfd> &ready, std::size_t offset);

    // Asks every device what is really pressed and corrects the state from the
    // answer.
    //
    // A key-up can go missing when a device is taken away, when the kernel
    // drops events under load, or when something grabs the keyboard mid-press.
    // The panel would then stand there with a modifier that is long since up,
    // and nothing in the event stream would ever correct it.
    void resync();

private:
    struct Device {
        // Stable for as long as the device lives, and never reused. The state
        // is keyed on it, so a number that came round again would mix up two
        // keyboards.
        int id = 0;
        std::string path;
        // What this device reports goes to this seat and to no other. Read
        // when the device is opened and kept for as long as it is open, unless
        // udev had not spoken by then: see below.
        std::string seat;
        // Whether the seat above is an answer or a guess at the commonest
        // case. While it is a guess nothing this device reports is passed on,
        // and it is asked about again.
        bool seatSettled = false;
        // How many correction beats have asked udev about this device and been
        // told nothing. Counted so that a machine where udev has nothing to
        // say at all does not go on dropping every key for good.
        int seatBeats = 0;
        int fd = -1;
        libevdev *dev = nullptr;
    };

    // What a keyboard already holds is taken the moment it is opened rather
    // than at the next correction a second and a half later: one can be
    // plugged in with a modifier already down.
    void openDevice(const std::string &path);
    void scan();
    void retire(std::size_t at);
    // Reads what one device has to say. Returns false when it has gone away.
    bool readFrom(Device &device);

    // Asks udev again which seat a device belongs to and takes the answer.
    // Says whether there is one now.
    bool askSeat(Device &device);

    // Takes what one device is holding at this moment and puts it at its seat.
    // Both the moment a seat becomes known and every correction beat need it,
    // and a keyboard is asked directly rather than remembered.
    void takeWhatIsHeld(Device &device);

    Seats &m_state;
    std::vector<Device> m_devices;
    int m_inotify = -1;
    int m_nextId = 1;
};

} // namespace bindpeek::watch
