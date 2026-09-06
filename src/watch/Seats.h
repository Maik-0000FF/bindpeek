// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Modifiers.h"
#include "Protocol.h"

#include <cstddef>
#include <string>
#include <vector>

namespace bindpeek::watch {

// The seat a device belongs to when it names none, which is nearly all of
// them: udev writes the property only on a device that has been moved with
// `loginctl attach`, and everything else belongs to the first seat.
inline constexpr char kDefaultSeat[] = "seat0";

// What is held, one answer per seat.
//
// A keyboard belongs to a seat and a person sits at one of them. Told what is
// held at the other, they would learn when somebody else pressed a modifier
// and when they pressed some other key: no key and no character, only that
// something was held or fell, and when. That is a small thing to know about
// somebody and it is not nothing, so the two are kept apart here.
//
// One entry on the ordinary machine, which has one seat, and nothing about
// this costs anything there.
//
// It also remembers the record each seat last settled on. That is how the
// caller finds out something happened: no answer is threaded back through the
// reading, the record as it stands is compared with the one that went out.
class Seats {
public:
    // A key went down or came up on one device of one seat. The device numbers
    // are the caller's and stay the same while the device lives, as
    // Modifiers describes them; a device belongs to one seat for as long as it
    // is open, so no number is ever seen at two of them.
    void press(const std::string &seat, int device, int code);
    void release(const std::string &seat, int device, int code);

    // A device of this seat is gone, with everything it held.
    void forget(const std::string &seat, int device);

    // What one device really reports right now, replacing what it was thought
    // to hold.
    void reconcile(const std::string &seat, int device,
                   const std::vector<int> &down);

    // A key that is not a modifier went down at this seat. A fact about the
    // round rather than about the state, so settle forgets it again.
    void takeKey(const std::string &seat);

    // The seats that have been named, so the caller can walk them. A seat
    // appears the first time a keyboard of it is opened and stays for as long
    // as this runs.
    std::size_t count() const;
    const std::string &nameAt(std::size_t at) const;

    // Whether this seat has anything to say: the held modifiers differ from
    // the ones that went out, or a key was taken at it this round.
    bool hasNews(std::size_t at) const;

    // What that comes to, the taken key included.
    Report report(std::size_t at) const;

    // The held modifiers of one seat and nothing about the round, for somebody
    // who has just been let in. A seat no keyboard was ever opened for holds
    // nothing, which is the truth about it and not a refusal.
    Report snapshot(const std::string &seat) const;

    // Takes the records as they stand to be the ones that have gone out, and
    // forgets the keys taken this round.
    void settle();

private:
    struct Seat {
        std::string name;
        Modifiers state;
        // The held list as it stood when this seat was last settled.
        std::vector<std::uint8_t> sent;
        bool keyTaken = false;
    };

    // The seat by name, made if it is new. Every way in here is a way a seat
    // becomes known, so there is one place that makes one.
    Seat &seatFor(const std::string &name);
    const Seat *known(const std::string &name) const;

    std::vector<Seat> m_seats;
};

} // namespace bindpeek::watch
