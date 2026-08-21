// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Binds every id from an enclosing component at creation time, which is what
// lets the bars below reach the mark they belong to. Without it they would
// reach it through the dynamic scope Qt only resolves by accident, which is
// the same reason the panel carries this line.
pragma ComponentBehavior: Bound

import QtQuick

// The three alignment marks, drawn rather than set in a typeface.
//
// Unicode has no left, centre and right alignment symbols. What interfaces use
// for them comes out of icon fonts, and this program ships none: reaching for
// a character would mean hoping for a face that happens to be installed, which
// is the trouble the keycap symbols already caused once.
//
// Three bars of unequal length say it without a font. Which end they are held
// against is the setting; whether they lie across or run down follows the axis
// the panel is stretched along, so the mark shows what the button does on the
// position that is actually set rather than a general idea of alignment.
//
// Asked as two questions rather than given the word, so the words for the
// three places live in one place and everything else asks about the ends.
Item {
    id: mark

    required property var ui
    property bool atStart: false
    property bool atEnd: false
    // True where the panel spans the width, false where it spans the height.
    property bool across: true
    property color colour: ui.text

    implicitWidth: ui.iconMarkSize
    implicitHeight: ui.iconMarkSize

    // The bars, no two of them the same length, so the end they are held
    // against can be seen at a glance. Fractions of the mark rather than pixel
    // counts, so the whole thing scales with its box.
    readonly property var lengths: [1.0, 0.6, 0.85]

    Repeater {
        model: mark.lengths

        delegate: Rectangle {
            id: bar

            required property int index
            required property real modelData

            color: mark.colour

            // Along the bar: its share of the mark, held against the chosen
            // end. Across it: the drawn weight, evenly spread over the box.
            readonly property real span: mark.across ? mark.width : mark.height
            readonly property real reach: bar.span * bar.modelData
            readonly property real offset: mark.atStart ? 0 : mark.atEnd ? bar.span - bar.reach : (bar.span - bar.reach) / 2
            readonly property real lane: (bar.index + 0.5) * (mark.across ? mark.height : mark.width) / mark.lengths.length - mark.ui.iconMarkBar / 2

            x: mark.across ? bar.offset : bar.lane
            y: mark.across ? bar.lane : bar.offset
            width: mark.across ? bar.reach : mark.ui.iconMarkBar
            height: mark.across ? mark.ui.iconMarkBar : bar.reach
        }
    }
}
