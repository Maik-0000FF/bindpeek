// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// The size that fits, worked out where nobody is looking.
//
// The panel on screen cannot answer this question about itself. Asking it
// means trying a size, and every size tried is a size the reader sees: with a
// combination held, a list that grew walks the type down a point at a time
// until it fits, and the walk is the answer being worked out in public.
//
// So a second panel is built off screen and asked instead. It is the same
// item, drawn from the same list against the same bound, and out of sight it
// halves its way to an answer in about five rounds rather than stepping. The
// answer is one number, and the panel on screen takes it in one go.
//
// Its own Theme, because the search writes the size it is trying into the
// theme it measures against. Sharing one with the panel on screen would put
// every try on screen after all, which is the thing being avoided.
Item {
    id: probe

    // The panel this answers for.
    //
    // Everything that decides a size is read off it rather than wired up a
    // second time, so the two can never be measured against different
    // questions. A list that reaches one of them reaches the other in the
    // same breath.
    required property PanelBody like

    // How long a caller holds what it has before giving up on an answer.
    //
    // The search costs about five rounds and a round is 16 ms, so an answer
    // that is coming at all is here long before this. What this is for is the
    // answer that never comes: whoever waits on it is holding the panel on
    // screen still, and a wait without an end would hold it there for good.
    property int budgetMs: 500

    // Whether the caller should go on holding what it has.
    readonly property bool waiting: !body.fitSettled && !probe.gaveUp

    // The size the search came to rest at.
    readonly property int size: body.theme.fontSizePt

    // The same number, at the moment it is reached.
    //
    // A caller cannot wait on `waiting` alone. Where the budget ran out first,
    // the wait is already over when the answer lands, and an answer nobody is
    // told about is one the panel walks its way to instead.
    signal found(int size)

    // Whether the searching has run past the budget without coming to rest.
    //
    // A watch over the resting, not a stopwatch per question. A question put
    // while the search is still running does not clear this and does not start
    // the count again: what a caller is waiting on is an answer, and a search
    // that has not produced one in half a second is not about to.
    //
    // Cleared at the next rest, so the question after that is waited on again.
    property bool gaveUp: false

    // Kept ticking rather than fired once, so that a search which never rests
    // is caught however many questions pass through it. A Timer that has run
    // out puts itself out, and the binding below would only notice at the next
    // change of rest, which is the very thing that is not happening.
    Timer {
        id: budget

        interval: probe.budgetMs
        repeat: true
        running: !body.fitSettled
        onTriggered: probe.gaveUp = true
    }

    // Every rest is an answer, including one reached after the wait was given
    // up on. It is worth telling: an answer can only ever lower the type, so a
    // late one costs a redraw and nothing else.
    //
    // The first rest is the one the panel is made in, a moment before its first
    // search starts. It carries the size that was configured, which is the size
    // the panel already has, so nothing moves on it.
    Connections {
        target: body
        function onFitSettledChanged() {
            if (!body.fitSettled) {
                return;
            }
            probe.gaveUp = false;
            probe.found(probe.size);
        }
    }

    // A theme of its own, and for one reason: it is where the search writes
    // the size it is trying.
    //
    // Nothing measured against a display is taken from here. The bound comes
    // from the panel below, which has it from whoever draws it, so this one is
    // never asked which output anything is on.
    Theme {
        id: probeTheme
    }

    // Off screen, but laid out.
    //
    // Measured: an item that is not visible is never laid out, and a panel
    // that was never laid out answers every width with zero, which measures as
    // fitting at any size at all. So it is drawn, at nothing.
    PanelBody {
        id: body

        anchors.fill: parent
        opacity: 0

        theme: probeTheme
        // Measured only while the panel it answers for is being read, and out
        // of sight of anyone even then, which is what makes it halve its way
        // to an answer instead of stepping towards one.
        //
        // Not before that: a panel not yet on screen searches for its own size
        // and takes no answer from here, so the same search beside it would be
        // run twice over for a number nobody reads, on the one stretch where
        // it is felt, between the key going down and the panel appearing.
        fitsToBounds: probe.like.showing
        showing: false

        groupsAcross: probe.like.groupsAcross
        alignsAtStart: probe.like.alignsAtStart
        alignsAtEnd: probe.like.alignsAtEnd
        deeperInSections: probe.like.deeperInSections
        arrangesByModifier: probe.like.arrangesByModifier
        showContinuations: probe.like.showContinuations
        maxWidth: probe.like.maxWidth
        maxHeight: probe.like.maxHeight
        heldText: probe.like.heldText
        message: probe.like.message
        groups: probe.like.groups
        continuations: probe.like.continuations
    }
}
