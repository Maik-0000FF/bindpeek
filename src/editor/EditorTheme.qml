// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// Look of the editor window itself.
//
// The colours come from the palette the user picked for the overlay, so the
// editor shows the choice rather than describing it. Only the shapes stay the
// editor own: flat and square throughout, no radius, no gradient, no shadow.
// Surfaces are told apart by their value and by hairline rules, not by depth.
//
// The fallbacks below apply only until a palette is handed in, which keeps
// this file usable on its own and stops a missing binding from painting
// everything black on black.

import QtQuick

QtObject {
    // The active palette, handed in by Editor.qml.
    property var palette: null

    readonly property color background: palette ? palette.background : "#101216"
    readonly property color surface: palette ? palette.surface : "#181b21"
    readonly property color surfaceHover: palette ? palette.surfaceHover : "#20242c"
    readonly property color line: palette ? palette.border : "#272c35"
    // The outline of a control the pointer is over.
    //
    // Not the resting line: seven of the palettes give border and surfaceHover
    // the same value, and a control whose body turns into its own outline
    // colour loses its edge exactly while it is being pointed at. Taking the
    // focus colour instead answers that for every palette at once, and it is
    // also the honest signal, the control is about to be used.
    readonly property color lineHover: palette ? palette.borderFocus : "#3f4654"
    readonly property color accent: palette ? palette.brand : "#4ade80"
    // A muted stand-in for the accent, mixed from the palette rather than
    // named separately, so every theme gets one without a table of its own.
    readonly property color accentDim: Qt.darker(accent, 2.2)
    readonly property color text: palette ? palette.text : "#e6e8ec"
    readonly property color textMuted: palette ? palette.textMuted : "#8b93a1"
    readonly property color warning: palette ? palette.warning : "#fbbf24"

    // Square everywhere. Named rather than writing 0 at every call site, so the
    // decision is visible and reversible in one place.
    readonly property int radius: 0
    readonly property int lineWidth: 1

    readonly property int paddingWindow: 20
    readonly property int paddingBox: 14
    // Text inset inside a control, so a label does not start on the border.
    readonly property int paddingControl: 10
    readonly property int spacingSection: 22
    readonly property int spacingRow: 12
    readonly property int labelWidth: 150
    readonly property int controlWidth: 230

    readonly property int fontSize: 13
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeHeading: 12
    readonly property int fontSizeTitle: 19
    // A heading is set in capitals, which needs air between the letters to
    // stay readable at this size.
    readonly property real headingLetterSpacing: 1.2

    // --- Window -----------------------------------------------------------

    // What the window opens at and the smallest it may become. Chosen rather
    // than measured: the settings column has a width of its own and the
    // preview next to it needs room to show a panel rather than a sliver.
    readonly property int windowWidth: 940
    readonly property int windowHeight: 660
    readonly property int windowMinWidth: 820
    readonly property int windowMinHeight: 560

    // Width of the settings column. The preview takes whatever is left.
    readonly property int settingsWidth: 470

    // --- Controls ---------------------------------------------------------

    // The slider: the rail it runs on and the grip that runs along it.
    readonly property int sliderTrackHeight: 2
    readonly property int sliderHandleWidth: 10
    readonly property int sliderHandleHeight: 20
    // Room for the number next to a slider, measured against the longest one
    // any of them shows.
    readonly property int sliderValueWidth: 62

    // How tall a dropdown may grow before it scrolls instead.
    readonly property int popupMaxHeight: 320
    // What something switched off is drawn at. Faint enough to read as
    // inactive at a glance, strong enough to still show what it would look
    // like.
    readonly property real dimmedOpacity: 0.35

    // The bar beside a list too long to show at once: how wide it is drawn,
    // and how far it stays off the edge. A row keeps the two together clear
    // so its last letters are not drawn underneath it.
    readonly property int scrollBarWidth: 6
    readonly property int scrollBarMargin: 2

    // The two together, which is what a list keeps clear on its right.
    //
    // A bar attached to a view is drawn over the view rather than beside it,
    // so a row that used the full width would end underneath it. Asked once
    // here because both lists that scroll keep the same room.
    readonly property int scrollRoom: scrollBarWidth + scrollBarMargin * 2

    // The switch: the drawn track, the knob that slides in it, and the gap
    // that keeps the knob off the track's edge.
    //
    // The control takes its own size from these. Without that it would be as
    // wide as its padding alone, because a Switch derives its width from the
    // content item, and this one has none: the track would be drawn outside
    // the control and only a few pixels of it could be clicked.
    readonly property int switchWidth: 40
    readonly property int switchHeight: 20
    readonly property int switchKnob: 16
    readonly property int switchGap: 2

    // --- Dialogs ----------------------------------------------------------

    readonly property int dialogWidth: 460
    // Drawn from the palette rather than a fixed grey: a dark scrim over a
    // light theme reads as a different product.
    readonly property color scrim: Qt.rgba(background.r, background.g, background.b, 0.7)
    readonly property int iconSize: 48
    // Twice the drawn size, so the mark stays sharp on a scaled display.
    readonly property int iconSourceSize: iconSize * 2
    readonly property int spacingTight: 2
    readonly property int spacingLinks: 4
    readonly property int buttonWidth: 100
    readonly property int buttonHeight: 32

    // Taken from the palette object rather than resolved again here. A second
    // candidate list would be a second thing to keep in sync, and the editor
    // showing a different face than the panel it configures would be wrong
    // anyway.
    readonly property string fontFamily: palette ? palette.fontFamily : "sans-serif"
    readonly property string fontFamilyMono: palette ? palette.fontFamilyMono : "monospace"
    readonly property string fontFamilyIcon: palette ? palette.fontFamilyIcon : "sans-serif"

    // --- Header -----------------------------------------------------------

    // The bar along the top: the mark and the name on the left, the two icon
    // buttons on the right.
    readonly property int headerHeight: 44
    readonly property int headerIconSize: 22
    // The square an icon button occupies. Large enough to be hit without aim,
    // which a bare glyph is not.
    readonly property int iconButtonSize: 30
    readonly property int fontSizeIcon: 16
    // How long a pointer has to rest on an icon before its name appears. Long
    // enough not to fire while the pointer is only passing over.
    readonly property int tooltipDelayMs: 400
    // How wide the hint may become before it wraps instead. A sentence drawn
    // as one line reaches across the window and points at nothing.
    readonly property int tooltipMaxWidth: 280

    // The drawn marks on a button: the box one occupies and the weight of the
    // bars inside it.
    readonly property int iconMarkSize: 16
    readonly property int iconMarkBar: 2

    // The symbols themselves, named rather than typed at the call site: a
    // glyph in the middle of a layout says nothing about what it is for, and
    // the same one is wanted in more than one place soon enough.
    readonly property string iconInfo: "ⓘ"
    // The mark on a dropdown. Measured for coverage rather than picked by
    // eye: this one is in DejaVu Sans, which is the first candidate the icon
    // family resolves to.
    readonly property string iconDropdown: "▾"
}
