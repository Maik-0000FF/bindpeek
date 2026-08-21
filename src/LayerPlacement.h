// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "Settings.h"

namespace LayerShellQt {
class Window;
}

namespace bindpeek {

// Puts a layer-shell surface where the settings say.
//
// The panel's own, and only the panel's: it is the one surface here that a
// compositor places. What the settings window shows is an item inside its own
// window, laid out by the bindings there, and the two are held together by
// Appearance, which answers the same questions for both.
void applyPlacement(LayerShellQt::Window *window, const Settings &settings);

} // namespace bindpeek
