// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Binds every id from an enclosing component at creation time, as the panel
// does: what this card shows is handed to it, never reached for through the
// scope it happens to be created in.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// One group of shortcuts on the panel: its heading, and the rows under it
// wrapped into as many columns as the room allows.
ColumnLayout {
    id: card

    // Every colour and size comes from here; see Theme.qml.
    required property var theme

    // One group as OverlayController hands it out: { name, entries }.
    required property var group

    // Whether the shortcuts that want a further modifier are shown under
    // headings of their own.
    required property bool deeperInSections

    // The height the card has to stay within, which is what decides where the
    // rows wrap. Zero while the panel does not know it yet.
    required property int roomForCard

    // Stands in for "no bound at all", where an extent has to be given and
    // there is nothing yet to bound it by. Past any display, so a wrap
    // measured against it never triggers.
    readonly property int unbounded: 1 << 20

    spacing: card.theme.spacingRow

    // The shortcut column is as wide as the widest shortcut in this group and
    // no wider.
    //
    // A fixed width is wrong in both directions at once: a group of single
    // letters leaves two thirds of it empty and pushes every description away
    // from its key, while a "CTRL+SHIFT+Delete" is cut off in the same panel.
    // Each group answers for itself, which is where the eye reads down a
    // column anyway.
    //
    // The longest string is the widest one because this column is set in a
    // monospaced face; measured rather than counted all the same, so the
    // number is in pixels and not in guessed character widths.
    readonly property string widestShortcut: {
        var widest = "";
        var entries = card.group.entries;
        for (var i = 0; i < entries.length; ++i) {
            var cell = card.deeperInSections ? entries[i].key : entries[i].shortcut;
            if (cell.length > widest.length)
                widest = cell;
        }
        return widest;
    }

    TextMetrics {
        id: shortcutMetrics
        font.family: card.theme.fontFamilyMono
        font.pixelSize: card.theme.fontSizeBody
        text: card.widestShortcut
    }

    Text {
        id: groupName

        text: card.group.name
        color: card.theme.accent
        font.family: card.theme.fontFamily
        font.pixelSize: card.theme.fontSizeGroup
        font.weight: Font.DemiBold
        elide: Text.ElideRight
        Layout.maximumWidth: card.theme.columnShortcut + card.theme.columnDescription
    }

    // The wrap inside one group, which is what a list that arrives as a single
    // group needs: a compositor that hands its binds out ungrouped answers
    // with one card, and the wrap between cards has nothing to wrap. The
    // column would then stand as tall as the whole list, which along the top
    // or bottom edge is a panel the height of the screen.
    //
    // The same two boxes the panel uses for its own wrap, and for the same
    // reason: the Flow wraps against a height taken from the panel's bound,
    // and this item takes up as much as the wrap actually used. Letting it
    // wrap against the room the layout gives it is the loop described there.
    //
    // The width is one gutter short of what the wrap measured: every row
    // carries a gutter on its right, including the rows of the last column,
    // and that one is not part of the card.
    //
    // Not capped at what the plate allows, and that is the whole point. The
    // panel reads a card that is wider than the plate as an overflow, takes
    // the type down a point and lets the wrap try again, and says at the foot
    // when even the smallest type could not fit it. A card that reported the
    // plate's width instead would report that everything fits while the last
    // columns are behind the edge, which is the one thing this panel promises
    // never to do.
    Item {
        id: entryBox

        // What is left for the rows once the heading has had its share. Zero
        // while nothing is known about the room, which is what the bound below
        // reads as "do not wrap at all".
        readonly property int roomForEntries: Math.max(0, card.roomForCard - groupName.height - card.spacing)

        // The height a column may reach before the next row starts a new one.
        //
        // Where the room is not known yet the wrap is off, and deliberately
        // so: a bound of nothing would give every row a column of its own and
        // ask the compositor for a surface as wide as the whole list laid end
        // to end. One tall column is the panel as it was before the wrap,
        // which is the safe answer while the room is still being worked out.
        readonly property int columnHeight: entryBox.roomForEntries > 0 ? entryBox.roomForEntries : card.unbounded

        Layout.preferredWidth: Math.max(0, entryFlow.implicitWidth - card.theme.gutterWrap)
        Layout.preferredHeight: Math.min(entryFlow.implicitHeight, entryBox.columnHeight)

        Flow {
            id: entryFlow

            // Down the column and into the next one, in both shapes of panel:
            // the height is the scarce extent either way, because the wrap
            // between cards has already taken the width it needed.
            //
            // Only the height is set. Running top to bottom, that is the
            // extent the wrap is measured against; the width is whatever the
            // columns came to, which is what the box above takes up.
            flow: Flow.TopToBottom
            spacing: card.theme.spacingRow
            height: entryBox.columnHeight

            Repeater {
                model: card.group.entries

                delegate: EntryBlock {
                    id: entryDelegate
                    required property var modelData
                    required property int index

                    theme: card.theme
                    entry: entryDelegate.modelData
                    deeperInSections: card.deeperInSections
                    shortcutWidth: Math.min(shortcutMetrics.width, card.theme.columnShortcut)
                    // The first row of a run of shortcuts belonging to the
                    // same combination. The entries arrive sorted by exactly
                    // that, so a run is found by comparing with the row above
                    // rather than by regrouping. The first row of a group
                    // opens one as well: the combination that fires now is a
                    // segment like any other.
                    opensSection: entryDelegate.index === 0 || card.group.entries[entryDelegate.index - 1].section !== entryDelegate.modelData.section
                }
            }
        }
    }
}
