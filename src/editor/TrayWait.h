// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

class QObject;

namespace bindpeek {

// Asks whether a tray is there, again and again, until one is or until the
// wait is up. `whenMissing` is called in the second case and only then.
//
// At the moment a program starts, a tray that is merely late and one that
// never comes look exactly alike, and the only thing that tells them apart is
// time. A session brings up the bar that carries the tray and the programs
// that want one at once, so the first answer is "no tray" on a session that
// has a perfectly good one.
//
// The question and what to do without an answer are handed in rather than
// asked for here. What is worth measuring is the rule, and handed in it can be
// measured without a tray, without a bus and without a window.
//
// The wait is held against the clock, not counted in polls. A program starting
// into a session that is still coming up is exactly where timers arrive late
// and Qt folds several of them into one, and a count would then give up
// whenever the ticks happened to add up rather than when the time was gone.
void waitForTray(QObject *owner, std::function<bool()> isPresent,
                 std::function<void()> whenMissing, int waitMs, int pollMs);

} // namespace bindpeek
