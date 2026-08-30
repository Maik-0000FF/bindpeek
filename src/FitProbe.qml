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

    // The output the panel is on, which is the bound the theme below is
    // measured against; see Theme.screenWidth.
    property int screenWidth: 0
    property int screenHeight: 0

    // How long an answer is waited for.
    //
    // The search costs about five rounds and a round is 16 ms, so an answer
    // that is coming at all is here long before this. What this is for is the
    // answer that never comes: whoever waits on it is holding the panel on
    // screen still, and a wait without an end would hold it there for good.
    property int budgetMs: 500

    // Whether the question has been answered, one way or the other.
    //
    // True once the search has come to rest, and true as well once the budget
    // above has run out, because a caller waiting on this needs to be let go
    // either way. `size` says which of the two it was.
    readonly property bool answered: body.fitSettled || probe.gaveUp

    // The size that fits, or zero where no answer was reached in time.
    readonly property int size: probe.gaveUp ? 0 : body.theme.fontSizePt

    // Whether the budget ran out on the question now standing.
    //
    // Held until the next question rather than dropped at the next rest. An
    // answer that arrives after the wait was given up on is one the panel has
    // already stepped past, and taking it then would put the type back up in
    // front of the reader, which is the one direction a gesture never moves.
    property bool gaveUp: false

    Timer {
        id: budget

        interval: probe.budgetMs
        onTriggered: probe.gaveUp = true
    }

    // A question is put whenever the search leaves its rest, and answered when
    // it comes back to one. That is the whole of the state here.
    Connections {
        target: body
        function onFitSettledChanged() {
            if (body.fitSettled) {
                budget.stop();
                return;
            }
            probe.gaveUp = false;
            budget.restart();
        }
    }

    // The first question is put while the object is being made, so the signal
    // above has already passed by the time anything could hear it.
    Component.onCompleted: if (!body.fitSettled) {
        budget.restart();
    }

    Theme {
        id: probeTheme

        screenWidth: probe.screenWidth
        screenHeight: probe.screenHeight
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
        // The bound is the same display the panel on screen is measured
        // against, and this one is never on screen, which is what makes it
        // halve its way to an answer instead of stepping towards one.
        fitsToBounds: true
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
