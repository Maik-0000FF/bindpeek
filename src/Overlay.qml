// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// The window the cheat sheet lives in. Display only, and it takes no input at
// all: the keyboard is watched below the compositor, so the keys stay where
// they belong. Nothing here can run a command.
//
// What is drawn is PanelBody, the same item the settings window shows in its
// preview. This file only says where it sits and when.
Window {
    id: win

    // Frameless and transparent, the rounded panel below paints the whole
    // visible surface.
    //
    // WindowTransparentForInput gives the surface an empty input region: the
    // pointer falls straight through to whatever is underneath, so a click can
    // never land on the panel and the focus can never move to it. Together
    // with KeyboardInteractivityNone that makes the panel a pure bystander,
    // which is the whole point: the shortcut it shows has to keep working
    // while it is on screen.
    flags: Qt.FramelessWindowHint | Qt.WindowTransparentForInput
    color: "transparent"

    // The surface goes up as soon as a combination is held, and the panel is
    // drawn on it once it has settled. Nothing is visible in between: the
    // window is transparent and the panel below is kept at nothing until it
    // is on its output and its size is worked out.
    //
    // The two are separate because the wait before showing is the only time
    // there is to learn which output the compositor picked, and that decides
    // how large the panel may be.
    visible: OverlayController.panelPending || OverlayController.panelVisible

    // Whether the surface is on an output and Qt has been told which.
    //
    // Measured: a window that has never been shown already names a screen,
    // and it names the primary one rather than the one the panel lands on. A
    // panel built against that is too large for a smaller output, and the
    // excess is drawn onto the monitor next to it. Until the surface has been
    // mapped, the smallest screen attached stands in, which fits wherever it
    // ends up.
    property bool onItsOutput: false
    onScreenChanged: win.onItsOutput = true
    // The surface has been presented, so the compositor has placed it and
    // what Qt names now is what it is. Needed as well as the signal above,
    // which never comes when the panel lands on the screen Qt guessed.
    onFrameSwapped: win.onItsOutput = true
    onVisibleChanged: if (!win.visible) {
        win.onItsOutput = false;
    }

    // Only the axes the compositor does not size are set here. With the panel
    // spanning an edge the compositor decides that extent, and a width of our
    // own would fight it.
    Binding on width {
        when: !Appearance.spanHorizontal
        value: panel.implicitWidth
    }
    Binding on height {
        when: !Appearance.spanVertical
        value: panel.implicitHeight
    }

    // The panel is measured against the output it is actually on. Under a
    // layer surface the compositor decides which that is, and Qt learns it
    // once the surface has been shown: the first panel after a change of
    // monitor can therefore be measured against the one before it and settle a
    // frame later. The alternative, measuring against the smallest screen
    // attached, never settles wrongly and never uses a wide screen either.
    Theme {
        id: theme
        screenWidth: win.onItsOutput && win.screen ? win.screen.width : Appearance.screenWidth
        screenHeight: win.onItsOutput && win.screen ? win.screen.height : Appearance.screenHeight
    }

    PanelBody {
        id: panel
        anchors.fill: parent
        theme: theme
        // Drawn only when there is something worth looking at: the panel is
        // wanted, the surface is where it will stay, and the type has been
        // settled. Everything before that would be a stutter.
        opacity: OverlayController.panelVisible && win.onItsOutput && panel.fitted ? 1 : 0
        // Told whether it is being read, which is what decides between
        // searching for a size out of sight and stepping towards one in
        // plain view.
        showing: panel.opacity > 0
        // The bound here is a display, so a size that does not fit it is
        // lowered until everything is on screen.
        fitsToBounds: true
        // Which way the groups run, measured against the output this window
        // landed on; see Theme.groupsAcross.
        groupsAcross: theme.groupsAcross
        alignsAtStart: Appearance.alignsAtStart
        alignsAtEnd: Appearance.alignsAtEnd
        deeperInSections: Appearance.deeperInSections
        arrangesByModifier: Appearance.arrangesByModifier
        showContinuations: Appearance.showContinuations
        continuations: OverlayController.continuations

        // Along an edge the panel spans, the extent is not a guess: the
        // compositor has set it and the window carries it. Only the axis the
        // panel sizes itself is measured against the screen, and that
        // measurement is deliberately the smallest one attached, which on a
        // wide screen would leave a band along the bottom edge wrapping into
        // rows at a fraction of the room it actually has.
        maxWidth: Appearance.spanHorizontal ? win.width : theme.maxPanelWidth
        maxHeight: Appearance.spanVertical ? win.height : theme.maxPanelHeight
        heldText: OverlayController.subtitle
        message: OverlayController.message
        groups: OverlayController.groups
    }
}
