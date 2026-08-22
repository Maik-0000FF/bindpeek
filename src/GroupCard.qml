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

    // Whether a run is headed by the key caps of its combination.
    required property bool showsSectionHeads

    // Whether the card carries its written heading. Off where the key caps
    // above the rows already name the same combination.
    required property bool showsGroupName

    // Whether the rows carry their key alone, which they do wherever
    // something above them already names the modifiers.
    required property bool showsKeyOnly

    // The height the card has to stay within. Zero while the panel does not
    // know it yet.
    required property int roomForCard

    // Whether the rows are spread sideways rather than filled downwards.
    //
    // True where the plate is a band along an edge, false where it is a column
    // at one. Against the side there is height to spend and hardly any width,
    // so a column is filled to the room and the next one begins where it runs
    // out. Along the top or bottom it is the other way round, and filling the
    // room there would answer a band with a panel the height of the screen for
    // a list two columns wide.
    required property bool spreadsRows

    // What this card may take of the plate's width before its rows are spread
    // into further columns. An equal share, worked out by the panel.
    required property int widthShare

    // Stands in for "no bound at all", where an extent has to be given and
    // there is nothing yet to bound it by. Past any display, so a wrap
    // measured against it never triggers.
    readonly property int unbounded: 1 << 20

    spacing: card.theme.spacingRow

    // The rows cut into the runs that belong to one combination. The entries
    // arrive sorted by exactly that, so a run is found by comparing with the
    // row before it rather than by regrouping.
    //
    // The runs are what the wrap moves about, not the rows. A run carries a
    // heading of its own where the deeper shortcuts are shown in sections, and
    // a column that opens under somebody else's heading says the wrong thing
    // about what fires. Only a run that is taller than a whole column is
    // broken up, and then inside itself.
    readonly property var runs: {
        var out = [];
        var entries = card.group.entries;
        for (var i = 0; i < entries.length; ++i) {
            if (i === 0 || entries[i - 1].section !== entries[i].section) {
                out.push({
                    caps: entries[i].caps,
                    entries: []
                });
            }
            out[out.length - 1].entries.push(entries[i]);
        }
        return out;
    }

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
            var cell = card.showsKeyOnly ? entries[i].key : entries[i].shortcut;
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

    // What each run came to, asked of the runs themselves: the height of the
    // heading, the height of every row under it, and how wide the widest of
    // them is.
    //
    // None of those three depends on where the wrap falls, which is what makes
    // it safe to work the wrap out from them. They are read as properties, so
    // a change of type or of list is followed without anything having to be
    // told to look again.
    readonly property var runShapes: {
        var out = [];
        var blocks = entryFlow.children;
        for (var i = 0; i < blocks.length; ++i) {
            var block = blocks[i];
            if (block.rowHeights === undefined || block.rowHeights.length === 0)
                continue;
            out.push({
                head: block.headingHeight,
                rows: block.rowHeights,
                width: block.naturalWidth
            });
        }
        return out;
    }

    // The height of one run, heading and rows together.
    function runHeight(run, gap) {
        var height = run.head;
        for (var i = 0; i < run.rows.length; ++i) {
            height += run.rows[i] + (i > 0 ? gap : 0);
        }
        return height;
    }

    // What a run alone comes to where it is taller than a whole column: how
    // many columns it is wrapped into, and how tall it then stands.
    //
    // The height is the tallest of those columns and not the bound, because
    // that is what the wrap below puts on the plate: the run is one block,
    // however many columns it took inside itself. Counting it as a full column
    // would push the run after it into a column of its own where there is
    // still room under it, and the search would settle on a taller panel than
    // the width asked for.
    function innerShape(run, bound, gap) {
        var room = Math.max(1, bound - run.head);
        var columns = 1;
        var used = 0;
        var tallest = 0;
        for (var i = 0; i < run.rows.length; ++i) {
            var row = run.rows[i];
            var lead = used > 0 ? gap : 0;
            if (used === 0 || used + lead + row <= room) {
                used += lead + row;
            } else {
                tallest = Math.max(tallest, used);
                ++columns;
                used = row;
            }
        }
        return {
            columns: columns,
            height: run.head + Math.max(tallest, used)
        };
    }

    // How many columns the runs come to at a given column height.
    //
    // The same rule the wrap below follows, run here on numbers first: fill a
    // column until the next run would pass the bound, then start another, and
    // where a single run is taller than the bound, wrap it inside itself and
    // begin the one after it in a column of its own.
    function columnsAt(runs, bound, gap) {
        var columns = 1;
        var used = 0;
        for (var i = 0; i < runs.length; ++i) {
            var height = card.runHeight(runs[i], gap);
            var lead = used > 0 ? gap : 0;
            if (used + lead + height <= bound) {
                used += lead + height;
                continue;
            }
            if (height <= bound) {
                ++columns;
                used = height;
                continue;
            }
            if (used > 0) {
                ++columns;
            }
            var shape = card.innerShape(runs[i], bound, gap);
            columns += shape.columns - 1;
            used = shape.height;
        }
        return columns;
    }

    // The shortest column that still leaves the card inside its share of the
    // width. Zero where the rows are filled downwards instead, and there is
    // nothing to work out.
    //
    // Halved rather than stepped: the answer is wanted in the frame it is
    // asked in, because it decides a wrap that is about to be drawn. Stepping
    // towards it would reshape the panel in front of the reader, one row per
    // frame, for as many rows as the list is long.
    readonly property int spread: {
        if (!card.spreadsRows)
            return 0;
        var runs = card.runShapes;
        if (runs.length === 0)
            return 0;

        var gap = card.theme.spacingRow;
        var widest = 0;
        var total = 0;
        // The shortest a column may be, which is a heading with one row under
        // it. A bound below that is one the wrap cannot meet: a run whose
        // heading alone fills the column has no room left to put a row in, and
        // what comes back is not a short column but a run that did not wrap at
        // all, in a box that was told it would be short. That is a plate the
        // size of nothing with the whole list drawn past its edge.
        var floor = 0;
        for (var i = 0; i < runs.length; ++i) {
            widest = Math.max(widest, runs[i].width);
            total += card.runHeight(runs[i], gap) + (i > 0 ? gap : 0);
            var tallestRow = 0;
            for (var j = 0; j < runs[i].rows.length; ++j) {
                tallestRow = Math.max(tallestRow, runs[i].rows[j]);
            }
            floor = Math.max(floor, runs[i].head + tallestRow);
        }
        if (widest <= 0 || total <= 0)
            return 0;
        if (floor >= total)
            return total;

        // How many columns of that width the share has room for. One at the
        // least: a share too narrow for a single column is answered by the
        // longest column rather than by no column at all, and the panel says
        // at its foot what did not fit.
        var allowed = Math.max(1, Math.floor((card.widthShare + gap) / (widest + gap)));
        if (allowed === 1)
            return total;

        var low = floor;
        var high = total;
        var best = total;
        while (low <= high) {
            var mid = Math.floor((low + high) / 2);
            if (card.columnsAt(runs, mid, gap) <= allowed) {
                best = mid;
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        }
        return best;
    }

    Text {
        id: groupName

        visible: card.showsGroupName
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
        // Counted from the heading only where there is one: a card whose
        // combination is named by the key caps below has no written heading,
        // and taking a line off for it would leave the rows a row short.
        readonly property int roomForEntries: Math.max(0, card.roomForCard - (groupName.visible ? groupName.height + card.spacing : 0))

        // The height a column may reach before the next run starts a new one.
        //
        // Along a band that is the shortest column the width has room for, so
        // the plate stays a band; against a side it is the room itself, which
        // fills the column and wraps at the bottom of it.
        //
        // Where the room is not known yet the wrap is off, and deliberately
        // so: a bound of nothing would give every row a column of its own and
        // ask the compositor for a surface as wide as the whole list laid end
        // to end. One tall column is the panel as it was before the wrap,
        // which is the safe answer while the room is still being worked out.
        readonly property int columnHeight: {
            if (card.spreadsRows && card.spread > 0) {
                return entryBox.roomForEntries > 0 ? Math.min(card.spread, entryBox.roomForEntries) : card.spread;
            }
            return entryBox.roomForEntries > 0 ? entryBox.roomForEntries : card.unbounded;
        }

        // What the wrap actually came to, in both directions, and never the
        // bound it was given.
        //
        // The height was once capped at that bound, and it was the same
        // mistake the width carries a paragraph about above: where the wrap
        // cannot meet the bound, the cap turns a card that is too tall into a
        // card that merely claims to be short, and the rows it holds are drawn
        // past the edge of a plate built around the claim. Reported honestly,
        // a card that came out too tall is an overflow like any other, and the
        // panel answers it by taking the type down and saying so at the foot.
        Layout.preferredWidth: Math.max(0, entryFlow.implicitWidth - card.theme.gutterWrap)
        Layout.preferredHeight: entryFlow.implicitHeight

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
                model: card.runs

                delegate: ColumnLayout {
                    id: runBlock
                    required property var modelData

                    // What the card measures the wrap from. Each of the three
                    // is taken from the items themselves and none of them
                    // depends on where the wrap falls.
                    readonly property int headingHeight: runHeading.visible ? runHeading.implicitHeight + runHeading.Layout.topMargin + runBlock.spacing : 0
                    readonly property var rowHeights: {
                        var out = [];
                        var rows = runFlow.children;
                        for (var i = 0; i < rows.length; ++i) {
                            if (rows[i].implicitHeight > 0)
                                out.push(rows[i].implicitHeight);
                        }
                        return out;
                    }
                    // The gutter is added to the heading and not to the rows,
                    // and the two are not measured alike: a row is a layout
                    // around its own margin and carries it in what it reports,
                    // while the heading's margin is read by the layout above
                    // it and left out of its own width. Compared as they come,
                    // a heading wider than every row would still measure
                    // narrower than one, and the wrap would be worked out from
                    // a width the card does not have.
                    readonly property int naturalWidth: {
                        var widest = runHeading.visible ? runHeading.implicitWidth + card.theme.gutterWrap : 0;
                        var rows = runFlow.children;
                        for (var i = 0; i < rows.length; ++i) {
                            widest = Math.max(widest, rows[i].implicitWidth);
                        }
                        return widest;
                    }

                    spacing: card.theme.spacingRow

                    // The combination this run belongs to, one key cap per
                    // modifier, drawn the way they sit on the keyboard rather
                    // than written out as a line of text. Every cap is marked:
                    // the ones already held and the one still to press are the
                    // same kind of thing, a key.
                    //
                    // The heading belongs to what follows it, not to the row
                    // above, hence the margin on top rather than below.
                    RowLayout {
                        id: runHeading

                        visible: card.showsSectionHeads
                        spacing: card.theme.spacingRow
                        Layout.topMargin: card.theme.spacingGroup
                        // The gap to a wrapped column, carried by every row
                        // that can be the widest one in its column; see
                        // Theme.gutterWrap.
                        Layout.rightMargin: card.theme.gutterWrap

                        Repeater {
                            model: runBlock.modelData.caps

                            delegate: Rectangle {
                                id: keyCap
                                required property string modelData

                                implicitWidth: capLabel.implicitWidth + card.theme.paddingPill * 2
                                implicitHeight: capLabel.implicitHeight + card.theme.paddingPill
                                radius: card.theme.radiusPill
                                color: card.theme.surfaceHover
                                border.color: card.theme.brand
                                border.width: card.theme.borderWidthKey

                                Text {
                                    id: capLabel
                                    anchors.centerIn: parent
                                    text: keyCap.modelData
                                    color: card.theme.brand
                                    font.family: card.theme.fontFamilyMono
                                    font.pixelSize: card.theme.fontSizeGroup
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }

                    // The rows of this run, and a wrap of their own that only
                    // ever comes into play where the run alone is taller than
                    // a column. Everywhere else the run stands as one block,
                    // which is what keeps a heading with the rows it names.
                    Item {
                        id: runBox

                        readonly property int roomForRows: Math.max(0, entryBox.columnHeight - runBlock.headingHeight)

                        // What its wrap came to, for the reason the card gives
                        // above: a box that reports the bound rather than the
                        // rows it holds hides them instead of fitting them.
                        Layout.preferredWidth: runFlow.implicitWidth
                        Layout.preferredHeight: runFlow.implicitHeight

                        Flow {
                            id: runFlow

                            flow: Flow.TopToBottom
                            spacing: card.theme.spacingRow
                            height: runBox.roomForRows > 0 ? runBox.roomForRows : card.unbounded

                            Repeater {
                                model: runBlock.modelData.entries

                                delegate: EntryBlock {
                                    id: entryDelegate
                                    required property var modelData

                                    theme: card.theme
                                    entry: entryDelegate.modelData
                                    showsKeyOnly: card.showsKeyOnly
                                    shortcutWidth: Math.min(shortcutMetrics.width, card.theme.columnShortcut)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
