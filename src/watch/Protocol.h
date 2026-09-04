// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>

namespace bindpeek::watch {

// What the service says and what the panel hears. In one file because the
// value crosses a process boundary: one side writes the record and the other
// reads it, and a shape written down twice is a shape that drifts.
//
// The path is a compile definition rather than a literal, because the socket
// unit names it as well. CMake substitutes both from one setting, so there is
// no way for the two to disagree.
inline constexpr char kSocketPath[] = BINDPEEK_WATCH_SOCKET;

// Raised whenever the record changes shape. A reader that does not know the
// number refuses the record rather than reading old fields out of a new
// layout.
inline constexpr std::uint8_t kProtocolVersion = 1;

// The modifiers, one byte each. Numbers rather than names: the names are an
// interface matter and belong where the interface is, and this is the half of
// the program that is meant to know as little as possible.
enum Modifier : std::uint8_t {
    kNoModifier = 0,
    kSuper = 1,
    kCtrl = 2,
    kAlt = 3,
    kShift = 4,
};

// Four, because there are four modifiers and none can be held twice.
inline constexpr std::size_t kMaxHeld = 4;

// A key that is not a modifier went down. Which key is not in here and is not
// asked for: the panel only has to know that the user has taken the shortcut
// they were looking at, and that is the whole of what leaves this service
// about any other key.
inline constexpr std::uint8_t kFlagKeyTaken = 1u << 0;

// One record, one datagram. The socket is SOCK_SEQPACKET, so the kernel draws
// the boundary; neither side needs a length, and that is what keeps a parser
// out of the half that holds the keyboards.
//
// count says how many entries of held are meant. Deliberately a count and not
// a terminator: with four modifiers down there would be no room left for the
// terminator, and a rule whose last case is an exception is a rule that gets
// read wrong.
struct Report {
    std::uint8_t version;
    std::uint8_t flags;
    std::uint8_t count;
    std::uint8_t held[kMaxHeld];
    std::uint8_t reserved;
};

static_assert(sizeof(Report) == 8, "the record goes on the wire as it stands");

} // namespace bindpeek::watch
