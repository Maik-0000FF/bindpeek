// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Modifiers.h"

#include <linux/input-event-codes.h>

#include <algorithm>

namespace bindpeek::watch {
namespace {

struct Entry {
    int code;
    std::uint8_t id;
};

// The one table. Both readers below are built from it, so a code added here is
// answered by idOf and asked about by codes() without a second edit.
//
// KEY_RIGHTALT is AltGr on many layouts. It counts as ALT because that is what
// a compositor binds it as; a layout that uses it for characters emits those
// through a different code anyway.
constexpr Entry kTable[] = {
    {KEY_LEFTMETA, kSuper},  {KEY_RIGHTMETA, kSuper},  {KEY_LEFTCTRL, kCtrl},
    {KEY_RIGHTCTRL, kCtrl},  {KEY_LEFTALT, kAlt},      {KEY_RIGHTALT, kAlt},
    {KEY_LEFTSHIFT, kShift}, {KEY_RIGHTSHIFT, kShift},
};

} // namespace

std::uint8_t Modifiers::idOf(int code) {
    for (const Entry &entry : kTable) {
        if (entry.code == code) {
            return entry.id;
        }
    }
    return kNoModifier;
}

const std::vector<int> &Modifiers::codes() {
    static const std::vector<int> all = [] {
        std::vector<int> out;
        out.reserve(std::size(kTable));
        for (const Entry &entry : kTable) {
            out.push_back(entry.code);
        }
        return out;
    }();
    return all;
}

std::vector<std::uint8_t> Modifiers::held() const {
    std::vector<std::uint8_t> out;
    for (const Key &key : m_down) {
        const std::uint8_t id = idOf(key.code);
        if (std::find(out.begin(), out.end(), id) == out.end()) {
            out.push_back(id);
        }
    }
    return out;
}

bool Modifiers::press(int device, int code) {
    if (idOf(code) == kNoModifier) {
        return false;
    }
    for (const Key &key : m_down) {
        if (key.device == device && key.code == code) {
            // Already down. Auto-repeat is not a new press, and neither is a
            // state replay that only repeats what is known.
            return false;
        }
    }
    const std::vector<std::uint8_t> before = held();
    m_down.push_back(Key{device, code});
    return held() != before;
}

bool Modifiers::release(int device, int code) {
    const std::vector<std::uint8_t> before = held();
    const auto gone =
        std::remove_if(m_down.begin(), m_down.end(), [&](const Key &key) {
            return key.device == device && key.code == code;
        });
    if (gone == m_down.end()) {
        return false;
    }
    m_down.erase(gone, m_down.end());
    return held() != before;
}

bool Modifiers::forget(int device) {
    const std::vector<std::uint8_t> before = held();
    const auto gone =
        std::remove_if(m_down.begin(), m_down.end(),
                       [&](const Key &key) { return key.device == device; });
    if (gone == m_down.end()) {
        return false;
    }
    m_down.erase(gone, m_down.end());
    return held() != before;
}

bool Modifiers::reconcile(int device, const std::vector<int> &down) {
    const std::vector<std::uint8_t> before = held();

    // What this device is no longer holding goes. The order of what stays is
    // left alone: a modifier that survives the correction was pressed when it
    // was pressed, and moving it would reorder the panel for nothing.
    const auto gone =
        std::remove_if(m_down.begin(), m_down.end(), [&](const Key &key) {
            return key.device == device &&
                   std::find(down.begin(), down.end(), key.code) == down.end();
        });
    m_down.erase(gone, m_down.end());

    // What it holds and was not known to be holding is appended. That is a
    // lost key-down, the mirror image of the case above, and it has no place
    // in the order because nobody ever saw it take one.
    for (const int code : down) {
        if (idOf(code) == kNoModifier) {
            continue;
        }
        const bool known =
            std::any_of(m_down.begin(), m_down.end(), [&](const Key &key) {
                return key.device == device && key.code == code;
            });
        if (!known) {
            m_down.push_back(Key{device, code});
        }
    }

    return held() != before;
}

Report Modifiers::report(bool keyTaken) const {
    const std::vector<std::uint8_t> ids = held();
    Report out{};
    out.version = kProtocolVersion;
    out.flags = keyTaken ? kFlagKeyTaken : static_cast<std::uint8_t>(0);
    out.count = static_cast<std::uint8_t>(std::min(ids.size(), kMaxHeld));
    for (std::size_t at = 0; at < out.count; ++at) {
        out.held[at] = ids[at];
    }
    return out;
}

} // namespace bindpeek::watch
