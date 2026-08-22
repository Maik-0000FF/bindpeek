// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Binds every id from an enclosing component at creation time, as the panel
// does: what this block shows is handed to it, never reached for through the
// scope it happens to be created in.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// One shortcut on the panel: the keys on the left, what they do on the right.
//
// A row that still wants another modifier is not the one the next key fires,
// and it says so twice: the keys it is missing stand in front of its own, and
// it is set in the plain text colour rather than the brand one. It stays in
// the list rather than being hidden: that it is there at all is the answer to
// "is there more under this hand".
//
// A layout around a single row, which is not a leftover. The gap a wrapped
// column keeps is carried by the row as a margin, and a margin is only read by
// the layout the item sits in; in the wrap itself there is none.
ColumnLayout {
    id: block

    // Every colour and size comes from here; see Theme.qml.
    required property var theme

    // One entry as OverlayController hands it out: { shortcut, key,
    // description, deeper, section, caps }.
    required property var entry

    // Whether the row carries its key alone. It does wherever something above
    // it already names the modifiers: the key caps that head a section, or a
    // group headed by the combination itself.
    required property bool showsKeyOnly

    // How wide the shortcut column of this group is. Measured and capped by
    // the card, so every row of one group lines up.
    required property int shortcutWidth

    spacing: block.theme.spacingRow

    RowLayout {
        id: entryRow

        spacing: block.theme.spacingColumn
        // See Theme.gutterWrap: the row carries the distance its column keeps
        // from the one wrapped next to it.
        Layout.rightMargin: block.theme.gutterWrap

        Text {
            // The width the card measured, which is capped there so one
            // absurdly long combination cannot push the descriptions off the
            // panel; it is elided instead.
            Layout.preferredWidth: block.shortcutWidth
            text: block.showsKeyOnly ? block.entry.key : block.entry.shortcut
            // Colour, not weight: the shortcut that fires on the next key
            // carries the brand colour, the ones further off are set plainly.
            //
            // Dimming was the obvious way and the wrong one. It can only lower
            // contrast, and measured against the palettes here it pushes this
            // column under 4.5:1 on six of them, while three carry a brand
            // colour that sits below that undimmed anyway. What is one key
            // further on would then be the least readable thing on the panel.
            color: block.entry.deeper ? block.theme.text : block.theme.brand
            font.family: block.theme.fontFamilyMono
            font.pixelSize: block.theme.fontSizeBody
            elide: Text.ElideRight
        }

        Text {
            // Only as wide as it needs, up to the cap: a short description
            // leaves the panel narrow, a long one is elided rather than
            // widening the whole table.
            Layout.maximumWidth: block.theme.columnDescription
            text: block.entry.description
            color: block.theme.text
            font.family: block.theme.fontFamily
            font.pixelSize: block.theme.fontSizeBody
            elide: Text.ElideRight
        }
    }
}
