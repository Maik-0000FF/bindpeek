// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LayerPlacement.h"

#include <LayerShellQt/Window>

#include <QMargins>

namespace bindpeek {

// The positions this is asked with, and what each one is worth. Every word
// Settings::knownPositions() offers is a case here, and the test measures the
// two lists against each other so a position added there cannot arrive here
// unanswered.
//
//   position  anchors                       margins (l, t, r, b)
//   left      left, and top+bottom to span  gap,   inset, 0,     inset
//   right     right, and top+bottom         0,     inset, gap,   inset
//   top       top, and left+right           inset, gap,   inset, 0
//   bottom    bottom, and left+right        inset, 0,     inset, gap
//   center    none                          0,     0,     0,     0
//
// Both distances are ignored in the centre, which touches no edge: a margin
// against an edge the surface is not anchored to moves nothing, and writing
// one there would say the panel keeps a distance it does not keep.
Placement placementFor(const Settings &settings) {
    // An anchor on one edge lets the compositor centre the surface along the
    // other, so a single anchor is all a side needs. Without any anchor it
    // floats in the middle, which is the centre position.
    using Anchor = LayerShellQt::Window::Anchor;
    Placement placement;

    // The two distances, and they are not the same one: the gap is how far the
    // panel sits from the edge it is anchored to, the inset how far it stops
    // short of that edge's two ends.
    const int gap = settings.marginPx();
    const int inset = settings.edgeInsetPx();

    // The edge the surface is held against, and the two distances that go
    // with it. Which axis it is stretched along is not decided here: that is
    // one rule for the whole program, and it is asked below.
    switch (settings.position()) {
    case Settings::Position::Left:
        placement.anchors = Anchor::AnchorLeft;
        placement.margins = QMargins(gap, inset, 0, inset);
        break;
    case Settings::Position::Right:
        placement.anchors = Anchor::AnchorRight;
        placement.margins = QMargins(0, inset, gap, inset);
        break;
    case Settings::Position::Top:
        placement.anchors = Anchor::AnchorTop;
        placement.margins = QMargins(inset, gap, inset, 0);
        break;
    case Settings::Position::Bottom:
        placement.anchors = Anchor::AnchorBottom;
        placement.margins = QMargins(inset, 0, inset, gap);
        break;
    case Settings::Position::Center:
        // No edge, so neither distance applies: the surface floats and the
        // compositor puts it in the middle.
        break;
    }

    // Anchoring the two opposite edges as well is what makes a surface span:
    // the compositor stretches it along that axis, and the panel must not
    // fight it by setting an extent of its own.
    //
    // Asked of the one place that answers it rather than written out per
    // position. A position added later would otherwise be stretched here and
    // measured as unstretched there, which leaves the panel and the
    // compositor sizing one surface against each other.
    //
    // One flag at a time: the enum carries no operator for combining its
    // values, so writing them with a bar produces an int the flags refuse.
    if (spansHorizontally(settings.position())) {
        placement.anchors.setFlag(Anchor::AnchorLeft);
        placement.anchors.setFlag(Anchor::AnchorRight);
    }
    if (spansVertically(settings.position())) {
        placement.anchors.setFlag(Anchor::AnchorTop);
        placement.anchors.setFlag(Anchor::AnchorBottom);
    }

    return placement;
}

void applyPlacement(LayerShellQt::Window *window, const Settings &settings) {
    if (window == nullptr) {
        return;
    }

    const Placement placement = placementFor(settings);
    window->setAnchors(placement.anchors);
    window->setMargins(placement.margins);
    // Follow the focus rather than QWindow::screen(), which would put the panel
    // wherever Qt happened to create the window.
    window->setWantsToBeOnActiveScreen(true);
    // Reserve no space in the layout; the panel floats above the windows.
    window->setExclusiveZone(0);
}

} // namespace bindpeek
