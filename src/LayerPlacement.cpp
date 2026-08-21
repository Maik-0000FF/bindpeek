// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LayerPlacement.h"

#include <LayerShellQt/Window>

#include <QMargins>

namespace bindpeek {

void applyPlacement(LayerShellQt::Window *window, const Settings &settings) {
    if (window == nullptr) {
        return;
    }

    // An anchor on one edge lets the compositor centre the surface along the
    // other, so a single anchor is all a side needs. Without any anchor it
    // floats in the middle, which is the centre position.
    using Anchor = LayerShellQt::Window::Anchor;
    LayerShellQt::Window::Anchors anchors;
    QMargins margins;

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
        anchors = Anchor::AnchorLeft;
        margins = QMargins(gap, inset, 0, inset);
        break;
    case Settings::Position::Right:
        anchors = Anchor::AnchorRight;
        margins = QMargins(0, inset, gap, inset);
        break;
    case Settings::Position::Top:
        anchors = Anchor::AnchorTop;
        margins = QMargins(inset, gap, inset, 0);
        break;
    case Settings::Position::Bottom:
        anchors = Anchor::AnchorBottom;
        margins = QMargins(inset, 0, inset, gap);
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
        anchors.setFlag(Anchor::AnchorLeft);
        anchors.setFlag(Anchor::AnchorRight);
    }
    if (spansVertically(settings.position())) {
        anchors.setFlag(Anchor::AnchorTop);
        anchors.setFlag(Anchor::AnchorBottom);
    }

    window->setAnchors(anchors);
    window->setMargins(margins);
    // Follow the focus rather than QWindow::screen(), which would put the panel
    // wherever Qt happened to create the window.
    window->setWantsToBeOnActiveScreen(true);
    // Reserve no space in the layout; the panel floats above the windows.
    window->setExclusiveZone(0);
}

} // namespace bindpeek
