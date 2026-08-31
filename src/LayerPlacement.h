// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Settings.h"

#include <LayerShellQt/Window>

#include <QMargins>

namespace bindpeek {

// Where a surface hangs and how far it keeps away.
//
// The two answers a compositor needs, together: which edges the surface is
// held against, and the distances it keeps on each of its four sides. Handed
// back as a value so the rule that works them out can be asked without a
// surface to write them into.
struct Placement {
    LayerShellQt::Window::Anchors anchors;
    QMargins margins;
};

// Works the placement out from the settings alone.
//
// Split from the call below because the two need different things: deciding
// where the panel goes needs nothing but the settings, and only the writing
// needs a layer-shell surface, which means a running compositor.
Placement placementFor(const Settings &settings);

// Puts a layer-shell surface where the settings say.
//
// The panel's own, and only the panel's: it is the one surface here that a
// compositor places. What the settings window shows is an item inside its own
// window, laid out by the bindings there, and the two are held together by
// Appearance, which answers the same questions for both.
void applyPlacement(LayerShellQt::Window *window, const Settings &settings);

} // namespace bindpeek
