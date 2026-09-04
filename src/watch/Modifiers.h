// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Protocol.h"

#include <cstdint>
#include <vector>

namespace bindpeek::watch {

// Which modifiers are held, across every keyboard at once.
//
// Nothing here opens a device or touches a descriptor: it is handed key codes
// and hands back a record. That is what makes it measurable without a
// keyboard, and it is the only part of the service with a state worth
// measuring.
class Modifiers {
public:
    // The modifier a kernel key code stands for, or kNoModifier. Both sides of
    // the keyboard give the same answer: nobody thinks of left and right SUPER
    // as two different modifiers.
    static std::uint8_t idOf(int code);

    // Every code idOf answers to. A caller that has to ask a device about each
    // of them reads the list from here rather than keeping a second copy.
    static const std::vector<int> &codes();

    // A key went down or came up on one device. Devices are told apart by a
    // number of the caller's choosing that stays the same while the device
    // lives. The same physical key held on two keyboards is two entries here,
    // and the modifier only lifts when the last of them is up.
    //
    // Both return true when the held modifiers changed, which is when there is
    // anything to send.
    bool press(int device, int code);
    bool release(int device, int code);

    // A device is gone. Everything it held goes with it, because a key on a
    // keyboard that has been unplugged can never be released.
    bool forget(int device);

    // What one device really reports right now, replacing what it was thought
    // to hold. This is the correction after a key-up that never arrived.
    bool reconcile(int device, const std::vector<int> &down);

    // The modifiers that are down, in the order they went down, each named
    // once however many keys are producing it.
    std::vector<std::uint8_t> held() const;

    // The record as it stands. keyTaken is a fact about the moment rather than
    // about the state, so it is passed in rather than kept.
    Report report(bool keyTaken) const;

private:
    struct Key {
        int device;
        int code;
    };

    // What the keys that are down produce, in the order they were pressed.
    std::vector<std::uint8_t> produced() const;

    // Brings the held list into line with that. Returns true when it changed,
    // which is the only thing any of the callers report.
    bool refresh();

    // Every modifier key that is physically down. What is held follows from
    // it, and the two-keyboard case needs no special handling anywhere.
    std::vector<Key> m_down;

    // The modifiers that are held, in the order they first went down, kept
    // rather than worked out afresh each time.
    //
    // Kept, because working it out from the keys would move a modifier that
    // never lifted. Hold SUPER on one keyboard, then SHIFT, then SUPER on a
    // second: letting the first SUPER go leaves SUPER held by the second, but
    // the earliest key producing it is now the later one, and the panel would
    // re-sort under somebody's fingers for a release that changed nothing.
    std::vector<std::uint8_t> m_held;
};

} // namespace bindpeek::watch
