// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Binds every id from an enclosing component at creation time. Without it the
// delegates below would reach `panel` through the deprecated dynamic scope,
// which Qt only resolves by accident. The price is that every delegate has to
// declare the model data it uses, which is spelled out below.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

// The panel: its plate, the held combination as a heading, whatever the source
// had to say, and the table of shortcuts.
//
// One file, drawn twice. The overlay puts it on a layer surface; the settings
// window puts the same item into its preview box with a made-up list. That is
// what makes the preview worth looking at, it is not a drawing of the panel
// but the panel itself, and a change here cannot look different there.
//
// It knows nothing about where it is or who is holding a key. Everything it
// shows is handed in, so neither side has to be a window and neither has to be
// the real one.
Rectangle {
    id: panel

    // Every colour and size comes from here; see Theme.qml.
    required property var theme

    // Which way the groups run. True for a panel that takes the whole width,
    // where they stand next to one another; false everywhere else, where they
    // stand under one another and wrap into a further column.
    property bool groupsAcross: false

    // The largest the plate may become. What does not fit is wrapped, and what
    // still does not fit is said at the foot rather than swallowed.
    property int maxWidth: 0
    property int maxHeight: 0

    // The heading: the modifiers held right now, in the order they went down.
    property string heldText: ""

    // A word from the source, shown when there is one. Never an error dialog:
    // the panel is on screen for as long as a key is held.
    property string message: ""

    // [{ name, entries: [{ shortcut, key, description, deeper, section, caps
    // }] }], which is what OverlayController hands to QML. The names are its
    // role constants, and a test holds the two sides to them.
    property var groups: []

    // [{ modifier, count }]: what each further key would still reach. Only
    // read when the footer is asked for.
    property var continuations: []

    // The two ways of showing the shortcuts that want a further modifier,
    // beyond the missing keys standing in front of their own.
    //
    // Both false is the plain list. Sections gives every further combination a
    // heading of its own; the footer adds a line saying which single modifier
    // leads to how many. They are independent of each other, which is why they
    // are two switches and not one word: the file offers three of the four
    // pairings, and nothing here would break on the fourth.
    property bool deeperInSections: false
    property bool showContinuations: false

    // Whether a group is headed by the combination its shortcuts want rather
    // than by the heading the session gave them, which is the other question
    // the list can be ordered by; see Settings::knownArrangements().
    property bool arrangesByModifier: false

    // The two things the panel makes of those settings, asked once here
    // rather than at each of the three places that care.
    //
    // A group named after its combination says what the key caps above a run
    // would say, so only one of the two is drawn. Either of them names the
    // modifiers, so either of them leaves the rows carrying their key alone.
    readonly property bool showsSectionHeads: panel.deeperInSections && !panel.arrangesByModifier
    readonly property bool showsKeyOnly: panel.deeperInSections || panel.arrangesByModifier

    // Where the content sits on the plate. Both false is the middle.
    //
    // Only visible where the plate is larger than what it holds, which is at
    // an edge the compositor stretches it along: a panel that sizes itself
    // has nowhere to move its content to. The two axes are answered by the
    // same pair, because a surface only ever spans one of them and the other
    // has no room to spare either way.
    property bool alignsAtStart: false
    property bool alignsAtEnd: false

    // Whether the type may be lowered until everything fits.
    //
    // On where the bound is a display, off where it is a box inside a window:
    // the preview in the settings window is a fraction of a screen, and a
    // preview that shrank to fit it would answer every size with the same
    // picture and hide the setting being made.
    property bool fitsToBounds: false

    // Whether this is on screen right now, which decides how a change of size
    // is made. Set by whoever draws it.
    property bool showing: false

    // Whether there is a size worth drawing.
    //
    // False only while the search runs, and the search only runs while
    // nothing is being shown. A panel already on screen is never taken away
    // to be measured: it is stepped towards its new size instead, because a
    // panel that blinks at every modifier is worse than one that is a point
    // too large for a moment.
    readonly property bool fitted: !fitsToBounds || (fit.phase !== fit.searching && fit.phase !== fit.confirming)

    // One layout round.
    //
    // The flow answers with the size it had before a change until the frame
    // that change was made in has been laid out, so every try costs one. A
    // frame signal would be the exact answer, but frames are only produced
    // when something changes, and a try that changes nothing would leave the
    // search standing; a timer always comes back. Where the round falls short
    // of a frame the confirmation below catches it, so this does not have to
    // match the refresh rate.
    readonly property int fitRoundMs: 16

    // The two ways to a size that fits.
    //
    // Out of sight, the range from the configured size down to the floor is
    // halved: about five tries instead of twenty, and nobody sees them. The
    // last try is a confirmation rather than a conclusion, so a round that
    // was too short on this display cannot leave a size that overflows.
    //
    // In plain view, one point per round and downwards only. The change then
    // passes for a redraw, where a halving search would jump through sizes in
    // front of the reader.
    //
    // Downwards only, although a narrower list would leave room to grow back
    // into. Measured, growing back costs seven size changes in plain view for
    // one key press, and it makes the size depend on the order a combination
    // was reached in. Within one gesture the size therefore only settles
    // downwards, and the next panel is searched for from the size that was
    // asked for again.
    QtObject {
        id: fit

        readonly property string searching: "searching"
        readonly property string confirming: "confirming"
        readonly property string adjusting: "adjusting"
        readonly property string idle: "idle"

        property string phase: idle
        // The largest size known to fit, and the largest not yet ruled out.
        property int low: 0
        property int high: 0
        property int best: 0
        property int trying: 0
    }

    Timer {
        id: fitRound

        interval: panel.fitRoundMs
        onTriggered: panel.stepFit()
    }

    // Starts over, from the size that was asked for. Called for everything
    // that changes how much has to fit or how much room there is.
    function refit() {
        if (!panel.fitsToBounds) {
            return;
        }
        if (panel.showing) {
            fit.phase = fit.adjusting;
            fitRound.restart();
            return;
        }
        fit.low = panel.theme.minFontSizePt;
        fit.high = panel.theme.configuredFontSizePt;
        fit.best = panel.theme.minFontSizePt;
        fit.trying = panel.theme.configuredFontSizePt;
        fit.phase = fit.searching;
        panel.theme.fontSizePt = fit.trying;
        fitRound.restart();
    }

    // Takes up a size that no longer fits while the panel is being read.
    // Ignored while anything else is under way, which would otherwise cut
    // that off halfway.
    function startAdjusting() {
        if (!panel.fitsToBounds || fit.phase !== fit.idle) {
            return;
        }
        fit.phase = fit.adjusting;
        fitRound.restart();
    }

    function stepFit() {
        if (fit.phase === fit.adjusting) {
            panel.stepAdjustment();
            return;
        }
        if (fit.phase === fit.confirming) {
            // Still too much at the size that measured as fitting: the round
            // it was measured in had not been laid out yet. One point down
            // and asked again, which ends at the floor at the latest.
            if (shortcutFlow.overflows && panel.theme.fontSizePt > panel.theme.minFontSizePt) {
                panel.theme.fontSizePt = panel.theme.fontSizePt - 1;
                fitRound.restart();
                return;
            }
            fit.phase = fit.idle;
            return;
        }

        if (shortcutFlow.overflows) {
            fit.high = fit.trying - 1;
        } else {
            fit.best = fit.trying;
            fit.low = fit.trying + 1;
        }

        if (fit.low <= fit.high) {
            fit.trying = Math.floor((fit.low + fit.high) / 2);
            panel.theme.fontSizePt = fit.trying;
            fitRound.restart();
            return;
        }

        fit.phase = fit.confirming;
        panel.theme.fontSizePt = fit.best;
        fitRound.restart();
    }

    function stepAdjustment() {
        // Nothing to take down, or nothing left to take: either way this is
        // over. Where it is the floor that ends it, the line at the foot of
        // the panel says what was left out.
        if (!shortcutFlow.overflows || panel.theme.fontSizePt <= panel.theme.minFontSizePt) {
            fit.phase = fit.idle;
            return;
        }
        panel.theme.fontSizePt = panel.theme.fontSizePt - 1;
        fitRound.restart();
    }

    // What the answer depends on. Everything here changes either how much has
    // to fit or how much room there is, and each of them starts the search
    // over rather than adjusting from where it left off.
    //
    // Not a complete list of what can leave the rows without room: a larger
    // corner radius eats into the plate, another font family measures
    // differently. Those are caught where they show themselves, at the
    // overflow itself, which asks for the size to be taken down. Enumerating
    // them here as well would be a second list to keep in step with the
    // first.
    onGroupsChanged: panel.refit()
    onMaxWidthChanged: panel.refit()
    onMaxHeightChanged: panel.refit()
    onGroupsAcrossChanged: panel.refit()
    onDeeperInSectionsChanged: panel.refit()
    onArrangesByModifierChanged: panel.refit()
    onShowContinuationsChanged: panel.refit()
    onFitsToBoundsChanged: panel.refit()

    // A size that was just asked for is taken at once, in view or not.
    //
    // Everything else only ever lowers the type while the panel is being
    // read, because nobody asked for it and type that walks about is a
    // distraction. This is the one change that was asked for, and asked for
    // by somebody watching the panel to see it happen: a setting held back
    // until the next gesture is a setting that appears to do nothing. Where
    // the new size does not fit, the step below takes it down again.
    function takeTheSizeAsked() {
        if (!panel.fitsToBounds) {
            return;
        }
        if (!panel.showing) {
            panel.refit();
            return;
        }
        panel.theme.fontSizePt = panel.theme.configuredFontSizePt;
        fit.phase = fit.adjusting;
        fitRound.restart();
    }

    Connections {
        target: panel.theme
        function onConfiguredFontSizePtChanged() {
            panel.takeTheSizeAsked();
        }
    }

    radius: theme.radiusPanel
    color: Qt.rgba(theme.surface.r, theme.surface.g, theme.surface.b, theme.panelOpacity)
    border.color: theme.border
    border.width: theme.borderWidth

    // A panel that clips is better than one whose text runs out over the
    // desktop, and it is not the rare case it once was: holding a single
    // modifier answers with everything that modifier can reach, while the
    // plate is bounded by the smallest screen attached. The line at the foot
    // says when something was left out, so a cut list is never mistaken for a
    // short one.
    clip: true

    // The inner distance is never smaller than the corner radius: at a large
    // radius the rounding eats into the box, and text set flush to the padding
    // would sit in the curve or outside it.
    readonly property int inset: Math.max(theme.paddingPanel, theme.radiusPanel)

    // What the content may occupy at most. The plate does not shrink below its
    // content; the content is bounded instead, which is what makes it wrap
    // rather than overflow.
    readonly property int maxContentWidth: panel.maxWidth - panel.inset * 2
    readonly property int maxContentHeight: panel.maxHeight - panel.inset * 2

    // What one group may take of the plate's width before its own rows are
    // spread into further columns.
    //
    // An equal share of what is left once the gaps between the cards are off,
    // which is a rough model of an uneven thing: one group can be ten times
    // another. It errs the safe way. A card that needs less than its share
    // simply leaves the room to the ones beside it, because the cards are
    // placed by the wrap and not by the share, while a card that took the
    // whole plate would push every other group into a second row.
    readonly property int widthShare: panel.groups.length > 1 ? Math.floor((panel.maxContentWidth - (panel.groups.length - 1) * panel.theme.spacingGroup) / panel.groups.length) : panel.maxContentWidth

    implicitWidth: content.width + panel.inset * 2
    implicitHeight: content.height + panel.inset * 2

    ColumnLayout {
        id: content

        // Placed rather than anchored, because where it sits is a setting.
        // The inset is the distance the plate keeps from its own edge, so the
        // two ends are that far in and the middle is what is left over.
        x: panel.alignsAtStart ? panel.inset : panel.alignsAtEnd ? panel.width - panel.inset - width : (panel.width - width) / 2
        y: panel.alignsAtStart ? panel.inset : panel.alignsAtEnd ? panel.height - panel.inset - height : (panel.height - height) / 2
        spacing: panel.theme.spacingGroup

        // Bounded on both axes, so the plate never grows past what it is
        // allowed to be.
        width: Math.min(implicitWidth, panel.maxContentWidth)
        height: Math.min(implicitHeight, panel.maxContentHeight)

        // --- Header -------------------------------------------------------

        // The held combination is the whole heading. Which compositor the list
        // came from is not news to whoever is sitting in front of it.
        Text {
            id: heading

            text: panel.heldText
            color: panel.theme.brand
            font.family: panel.theme.fontFamilyMono
            font.pixelSize: panel.theme.fontSizeTitle
            font.weight: Font.DemiBold
        }

        // --- Message from the source --------------------------------------

        Text {
            id: messageText

            Layout.fillWidth: true
            visible: panel.message !== ""
            text: panel.message
            color: panel.theme.warning
            font.family: panel.theme.fontFamily
            font.pixelSize: panel.theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }

        // --- The shortcuts of the held combination -------------------------

        // Which way the groups run follows the shape of the panel.
        //
        // Against the left or right edge it is a tall column, so the groups
        // stand under one another, which is how a list is read. Against the
        // top or bottom it is a wide band, so they stand next to one another;
        // stacking them there would push everything but the first group off
        // the panel. Floating in the middle there is no edge to take the shape
        // from, so the display decides: on a monitor standing on end the
        // groups run downwards and wrap into a new column, on a wide one they
        // run sideways and wrap into a new row.
        //
        // The wrap is the fallback in every case: nothing is dropped, it moves
        // to the next column or row instead.
        //
        // Two boxes, and they are not the same one.
        //
        // The Flow wraps inside the panel's own bound, which is a fixed number
        // for a given panel. This item then takes up as much as the wrap
        // actually used. Letting the Flow wrap against the room the layout
        // gives it is the obvious way and a loop: the layout sizes it from its
        // implicit size, and its implicit size comes out of the wrap. The
        // engine settles that by alternating between two answers, which is a
        // panel that flickers and stands on end while a slider is dragged.
        Item {
            id: shortcutBox

            // The room left for the table, which is the panel's own bound less
            // everything else in this column and the gaps between them.
            //
            // Counted from the siblings rather than taken from the layout: the
            // layout's answer would come out of this item's size, which comes
            // out of the wrap, which is what the room decides. None of the
            // siblings depends on the table, so counting them is free of that.
            //
            // The line at the foot is the one exception and is reserved for
            // whether it shows or not: it appears exactly when the wrap ran
            // out of room, so asking whether it is visible would be asking the
            // outcome of the very thing being measured. Reserving it costs one
            // row of a panel that had room to spare, which is the cheaper
            // mistake by far.
            readonly property int roomForFlow: Math.max(0, panel.maxContentHeight - heading.height - warningText.implicitHeight - content.spacing * 2 - (messageText.visible ? messageText.height + content.spacing : 0) - (continuationRow.visible ? continuationRow.height + content.spacing : 0))

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: Math.min(shortcutFlow.implicitWidth, panel.maxContentWidth)
            Layout.preferredHeight: Math.min(shortcutFlow.implicitHeight, shortcutBox.roomForFlow)

            Flow {
                id: shortcutFlow

                // Whether the wrap ran out of room as well, which is when
                // entries are behind the panel's edge rather than on it.
                readonly property bool overflows: implicitWidth > width || implicitHeight > height

                // Anything that leaves the rows without room ends up here,
                // including what no list of triggers names: a larger corner
                // radius eating into the plate, a font family that measures
                // wider. Rather than enumerate those, the overflow itself
                // asks for the size to be taken down.
                onOverflowsChanged: if (overflows) {
                    panel.startAdjusting();
                }

                width: panel.maxContentWidth
                height: shortcutBox.roomForFlow

                flow: panel.groupsAcross ? Flow.LeftToRight : Flow.TopToBottom
                spacing: panel.theme.spacingGroup

                Repeater {
                    model: panel.groups

                    delegate: GroupCard {
                        id: groupCard
                        required property var modelData

                        theme: panel.theme
                        group: groupCard.modelData
                        showsSectionHeads: panel.showsSectionHeads
                        showsKeyOnly: panel.showsKeyOnly
                        roomForCard: shortcutBox.roomForFlow
                        // Along a band the rows are spread sideways, which is
                        // the same question the groups themselves are asked
                        // and therefore the same answer.
                        spreadsRows: panel.groupsAcross
                        widthShare: panel.widthShare
                    }
                }
            }
        }

        // --- Where the next key would lead ---------------------------------

        // One line, not a table: it answers a single question, which is what
        // pressing one more key is worth. The counts overlap on purpose, a
        // shortcut wanting CTRL and SHIFT is counted under both, because
        // either key keeps it in view.
        RowLayout {
            id: continuationRow

            Layout.fillWidth: true
            visible: panel.showContinuations && panel.continuations.length > 0
            spacing: panel.theme.spacingColumn

            Repeater {
                model: panel.continuations

                delegate: Text {
                    id: continuation
                    required property var modelData

                    // Through the catalogue rather than glued together: a
                    // language that puts the number first can then do so.
                    text: qsTr("%1 %2").arg(continuation.modelData.modifier).arg(continuation.modelData.count)
                    color: panel.theme.text
                    font.family: panel.theme.fontFamilyMono
                    font.pixelSize: panel.theme.fontSizeSmall
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }

        // --- What did not fit ----------------------------------------------

        // Said rather than swallowed. The panel clips what runs past its edge,
        // and a reader has no way of telling a list that ends from one that
        // was cut off; this line is the difference between the two. It costs a
        // row, which is why it only appears when there is something to report.
        Text {
            id: warningText

            Layout.fillWidth: true
            visible: shortcutFlow.overflows
            text: qsTr("More than fits here. Press another modifier to narrow it down.")
            color: panel.theme.warning
            font.family: panel.theme.fontFamily
            font.pixelSize: panel.theme.fontSizeSmall
            elide: Text.ElideRight
        }
    }
}
