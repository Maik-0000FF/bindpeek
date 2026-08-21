// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls.Basic

// A tooltip drawn from the editor's own tokens rather than from the system
// palette, which under a plain compositor is not the palette anything else
// here is painted in.
//
// Two ways to raise it, and they are not the same thing:
//
//   hovered   the ordinary hint. Bound to the control's own hovered state, it
//             waits out the shared delay first, so a pointer crossing a row of
//             buttons does not leave a trail of boxes behind it.
//   visible   bound directly, for something that has to be said the moment it
//             becomes true rather than when somebody happens to point at it.
//
// Used as a child of the control it describes.
ToolTip {
    id: tip

    required property var ui

    // Bound to the control's hovered state by whoever uses this.
    property bool hovered: false

    font.family: ui.fontFamily
    font.pixelSize: ui.fontSizeSmall
    padding: ui.paddingControl

    // The box is capped and the text inside wraps to fill it. Capping the text
    // alone would not do it: a tooltip takes its width from what it holds, and
    // what it holds would still be one long line. Short text keeps its natural
    // width, so a two-word hint is not stretched to the cap.
    implicitWidth: Math.min(implicitContentWidth + leftPadding + rightPadding, ui.tooltipMaxWidth)

    // Driven here rather than through a binding on visible, which is left free
    // for the second use above.
    Timer {
        id: afterTheDelay
        interval: tip.ui.tooltipDelayMs
        onTriggered: tip.visible = true
    }

    onHoveredChanged: {
        if (tip.hovered) {
            afterTheDelay.restart();
            return;
        }
        // Gone at once. A hint that lingers after the pointer has left points
        // at nothing.
        afterTheDelay.stop();
        tip.visible = false;
    }

    contentItem: Text {
        text: tip.text
        color: tip.ui.text
        font: tip.font
        wrapMode: Text.WordWrap
        width: tip.availableWidth
    }

    background: Rectangle {
        color: tip.ui.surface
        border.color: tip.ui.line
        border.width: tip.ui.lineWidth
        radius: tip.ui.radius
    }
}
