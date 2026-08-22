// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Binds every id from an enclosing component at creation time, as the panel
// does: what this block shows is handed to it, never reached for through the
// scope it happens to be created in.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// One shortcut on the panel: the combination it belongs to where that carries
// a heading of its own, and the row of the shortcut with its description.
//
// A row that still wants another modifier is not the one the next key fires,
// and it says so twice: the keys it is missing stand in front of its own, and
// it is set in the plain text colour rather than the brand one. It stays in
// the list rather than being hidden: that it is there at all is the answer to
// "is there more under this hand".
ColumnLayout {
    id: block

    // Every colour and size comes from here; see Theme.qml.
    required property var theme

    // One entry as OverlayController hands it out: { shortcut, key,
    // description, deeper, section, caps }.
    required property var entry

    // Whether this row opens a run of shortcuts under the same combination,
    // which is what puts the key caps above it. Answered by the card: this is
    // a question about the row above, and a block cannot see it.
    required property bool opensSection

    // Whether the shortcuts that want a further modifier are shown under
    // headings of their own.
    required property bool deeperInSections

    // How wide the shortcut column of this group is. Measured and capped by
    // the card, so every row of one group lines up.
    required property int shortcutWidth

    spacing: block.theme.spacingRow

    // The combination this segment belongs to, one key cap per modifier, drawn
    // the way they sit on the keyboard rather than written out as a line of
    // text. Every cap is marked: the ones already held and the one still to
    // press are the same kind of thing, a key.
    //
    // The segment heading belongs to what follows it, not to the row above,
    // hence the margin on top rather than below.
    RowLayout {
        visible: block.deeperInSections && block.opensSection
        spacing: block.theme.spacingRow
        Layout.topMargin: block.theme.spacingGroup
        // The gap to a wrapped column, carried by every row that can be the
        // widest one in its column; see Theme.gutterWrap.
        Layout.rightMargin: block.theme.gutterWrap

        Repeater {
            model: block.entry.caps

            delegate: Rectangle {
                id: keyCap
                required property string modelData

                implicitWidth: capLabel.implicitWidth + block.theme.paddingPill * 2
                implicitHeight: capLabel.implicitHeight + block.theme.paddingPill
                radius: block.theme.radiusPill
                color: block.theme.surfaceHover
                border.color: block.theme.brand
                border.width: block.theme.borderWidthKey

                Text {
                    id: capLabel
                    anchors.centerIn: parent
                    text: keyCap.modelData
                    color: block.theme.brand
                    font.family: block.theme.fontFamilyMono
                    font.pixelSize: block.theme.fontSizeGroup
                    font.weight: Font.DemiBold
                }
            }
        }
    }

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
            // Under a segment heading the caps above already name the
            // modifiers, so the row is the key alone. Without one it carries
            // them itself.
            text: block.deeperInSections ? block.entry.key : block.entry.shortcut
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
