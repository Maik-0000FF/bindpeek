// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

// The single source for every colour, size and spacing the overlay uses.
// Nothing outside this file paints a raw hex value or a raw pixel count.
//
// The palettes live here as data; which one applies, and the sizes that go
// with it, come from Appearance, which has already weighed the settings file
// against the desktop's light/dark setting.

import QtQuick

QtObject {
    id: theme

    // --- Palettes ---------------------------------------------------------

    // Names must match Settings::knownThemes(), which validates them before
    // they ever reach this file; a test compares the two lists.
    //
    // Only the tokens painted here are carried, so a palette is a fixed set of
    // ten values and nothing else.
    //
    // A palette's border must be far enough from its surface to be seen at
    // all; that too is measured by a test rather than left to the eye.
    //
    // The brand colour carries the column of shortcuts that fire on the next
    // key, set at body size, so it is ordinary text and wants 4.5:1 against
    // the surface behind it. Every palette here meets that except two, and
    // deliberately: "catppuccin-latte" (3.34) and "solarized-light" (2.97)
    // are those themes' own greens, and a value that passed the measure
    // would no longer be the theme somebody picked by name. They stay, a test
    // names them, and any palette added later has to meet the measure.
    //
    // The muted colour is the other way round: it is the themes' comment
    // colour, low on purpose, and eight of them sit below 4.5:1 for that
    // reason. Only the house palette is this program's own to answer for.
    readonly property var palettes: ({
            // The house palette, and the one a first start comes up in: a
            // purple accent and a green brand on a nearly black ground, which
            // is what this program looks like.
            "bindpeek": {
                background: "#08060f",
                surface: "#12101d",
                surfaceHover: "#1a1728",
                border: "#2a2640",
                borderFocus: "#4a3f70",
                accent: "#a855f7",
                brand: "#4ade80",
                text: "#f0fdf4",
                // 4.86:1 against the surface. The muted colour carries the
                // section headings and the disabled labels in the settings
                // window, which is text to be read rather than a hint.
                textMuted: "#7a8291",
                warning: "#fbbf24"
            },
            "dark": {
                background: "#0f1115",
                surface: "#181b22",
                surfaceHover: "#232832",
                border: "#2a2f3a",
                borderFocus: "#3f4654",
                accent: "#60a5fa",
                brand: "#4ade80",
                text: "#e5e7eb",
                textMuted: "#9ca3af",
                warning: "#fbbf24"
            },
            "light": {
                background: "#ececef",
                surface: "#ffffff",
                surfaceHover: "#dfdfe3",
                border: "#d4d4d8",
                borderFocus: "#a1a1aa",
                accent: "#2563eb",
                // A darker green than the obvious one: on white the obvious
                // one measures 3.30:1, which is under the measure for the
                // column it carries. This one is 5.02:1 and reads as the same
                // green.
                brand: "#15803d",
                text: "#0f172a",
                textMuted: "#52525b",
                warning: "#d97706"
            },
            "contrast": {
                background: "#000000",
                surface: "#0a0a0a",
                surfaceHover: "#1a1a1a",
                border: "#ffffff",
                borderFocus: "#ffd60a",
                accent: "#ffd60a",
                // Not white, which is what the plain text is set in: the panel
                // tells the shortcut that fires from the ones a key further on
                // by their colour, and a palette that gives both the same
                // value leaves nothing to tell them apart by. Yellow on this
                // ground still measures 14:1.
                brand: "#ffd60a",
                text: "#ffffff",
                textMuted: "#e5e5e5",
                warning: "#ffd60a"
            },
            "catppuccin-mocha": {
                background: "#1e1e2e",
                surface: "#313244",
                surfaceHover: "#45475a",
                border: "#45475a",
                borderFocus: "#cba6f7",
                accent: "#cba6f7",
                brand: "#a6e3a1",
                text: "#cdd6f4",
                textMuted: "#a6adc8",
                warning: "#f9e2af"
            },
            "nord": {
                background: "#2e3440",
                surface: "#3b4252",
                surfaceHover: "#434c5e",
                border: "#434c5e",
                borderFocus: "#88c0d0",
                accent: "#88c0d0",
                brand: "#a3be8c",
                text: "#eceff4",
                textMuted: "#7b88a1",
                warning: "#ebcb8b"
            },
            "gruvbox-dark": {
                background: "#282828",
                surface: "#3c3836",
                surfaceHover: "#504945",
                border: "#504945",
                borderFocus: "#fabd2f",
                accent: "#fabd2f",
                brand: "#b8bb26",
                text: "#ebdbb2",
                textMuted: "#a89984",
                warning: "#fe8019"
            },
            "dracula": {
                background: "#282a36",
                surface: "#343746",
                surfaceHover: "#44475a",
                border: "#44475a",
                borderFocus: "#bd93f9",
                accent: "#bd93f9",
                brand: "#50fa7b",
                text: "#f8f8f2",
                textMuted: "#6272a4",
                warning: "#f1fa8c"
            },
            "tokyo-night": {
                background: "#1a1b26",
                surface: "#24283b",
                surfaceHover: "#292e42",
                border: "#292e42",
                borderFocus: "#7aa2f7",
                accent: "#7aa2f7",
                brand: "#9ece6a",
                text: "#c0caf5",
                textMuted: "#565f89",
                warning: "#e0af68"
            },
            "rose-pine": {
                background: "#191724",
                surface: "#1f1d2e",
                surfaceHover: "#26233a",
                border: "#403d52",
                borderFocus: "#c4a7e7",
                accent: "#c4a7e7",
                brand: "#9ccfd8",
                text: "#e0def4",
                textMuted: "#908caa",
                warning: "#f6c177"
            },
            "catppuccin-latte": {
                background: "#eff1f5",
                surface: "#ffffff",
                surfaceHover: "#e6e9ef",
                border: "#ccd0da",
                borderFocus: "#8839ef",
                accent: "#8839ef",
                brand: "#40a02b",
                text: "#4c4f69",
                textMuted: "#6c6f85",
                warning: "#df8e1d"
            },
            "solarized-light": {
                background: "#eee8d5",
                surface: "#fdf6e3",
                surfaceHover: "#e4ddc8",
                border: "#d8d0ba",
                borderFocus: "#268bd2",
                accent: "#268bd2",
                brand: "#859900",
                text: "#073642",
                textMuted: "#657b83",
                warning: "#b58900"
            },
            "eldritch": {
                background: "#212337",
                surface: "#323449",
                surfaceHover: "#3d3f5a",
                // Not the surface again: a border painted in the colour of
                // the plate behind it is no border at all. The hover shade is
                // the nearest value that is one, and six other palettes here
                // use it for their border too.
                border: "#3d3f5a",
                borderFocus: "#a48cf2",
                accent: "#a48cf2",
                brand: "#37f499",
                text: "#ebfafa",
                textMuted: "#7081d0",
                warning: "#f7c67f"
            },
            "kanagawa": {
                background: "#1f1f28",
                surface: "#2a2a37",
                surfaceHover: "#363646",
                border: "#363646",
                borderFocus: "#7e9cd8",
                accent: "#7e9cd8",
                brand: "#98bb6c",
                text: "#dcd7ba",
                textMuted: "#727169",
                warning: "#e6c384"
            }
        })

    // Appearance has validated the name already; the fallback only guards
    // against this file and Settings drifting apart.
    readonly property var p: palettes[Appearance.theme] || palettes["bindpeek"]

    readonly property color surface: p.surface
    readonly property color background: p.background
    readonly property color surfaceHover: p.surfaceHover
    readonly property color border: p.border
    readonly property color borderFocus: p.borderFocus
    readonly property color accent: p.accent
    readonly property color brand: p.brand
    readonly property color text: p.text
    readonly property color textMuted: p.textMuted
    readonly property color warning: p.warning

    // Only the panel body fades; borders, text and pills stay fully opaque, so
    // the content never washes out over a bright window underneath.
    readonly property real panelOpacity: Appearance.opacity

    // --- Typography -------------------------------------------------------

    // Resolve to the first installed candidate instead of hard coding one.
    // Without a fallback list fontconfig would substitute an arbitrary face
    // whose metrics break the layout. The trailing generic alias always
    // resolves.
    // Empty entries are skipped, so an unset fontFamily can simply be put at
    // the head of a list rather than needing a branch of its own.
    function pickFamily(candidates) {
        var installed = Qt.fontFamilies();
        for (var i = 0; i < candidates.length; ++i) {
            if (candidates[i] !== "" && installed.indexOf(candidates[i]) !== -1)
                return candidates[i];
        }
        return candidates[candidates.length - 1];
    }

    // A family from the settings is used only if it is actually installed.
    //
    // Passing an unknown name straight through does not give the user the face
    // they asked for: fontconfig substitutes some other one, and its metrics
    // are what the layout is then built on. That is how a panel ends up with
    // clipped rows on a machine where the font simply is not there. Falling
    // back to a candidate list keeps the result predictable.
    readonly property string fontFamily: pickFamily([Appearance.fontFamily, "Inter", "Cantarell", "Noto Sans", "Ubuntu", "DejaVu Sans", "sans-serif"])

    // The shortcut column is set in this one, and it carries more than letters:
    // the keys that print nothing are shown with the symbol on their keycap,
    // which sits in the arrow and control-picture blocks. The candidates are
    // ordered by which of them actually have those, measured rather than
    // assumed; a family without them would leave the rendering to a per-glyph
    // fallback, and a row would be set in two typefaces at once.
    readonly property string fontFamilyMono: pickFamily(["JetBrainsMono Nerd Font", "JetBrains Mono", "DejaVu Sans Mono", "Fira Code", "Noto Sans Mono", "monospace"])

    // The face the small interface symbols are drawn in.
    //
    // A face of its own because the ones an interface is set in rarely carry
    // the enclosed and geometric blocks these come from: measured, Inter has
    // none of them. Left to the interface font, each symbol would be pulled
    // from whatever fontconfig substitutes for it, one at a time and possibly
    // from a different family per symbol.
    readonly property string fontFamilyIcon: pickFamily(["DejaVu Sans", "Noto Sans Symbols 2", "Symbola", "sans-serif"])

    // The size that was asked for, which is the largest the panel may use.
    readonly property int configuredFontSizePt: Appearance.fontSizePt

    // The size actually set in.
    //
    // Writable, and written by whoever draws the panel: a size that does not
    // fit the display is not a size, it is a list with rows missing from it.
    // The panel lowers this until everything fits and leaves it at the
    // configured size wherever that already does, so the setting reads as
    // "this large, or as close to it as the screen allows".
    property int fontSizePt: configuredFontSizePt

    // Below this the rows are shreds of text rather than a cheat sheet. A
    // panel that cannot fit even here says so in its footer instead of
    // shrinking on out of sight.
    readonly property int minFontSizePt: 7

    // Every size is derived from the one base size, so raising it scales the
    // whole panel instead of only the body text.
    readonly property int fontSizeBody: fontSizePt
    readonly property int fontSizeTitle: Math.round(fontSizePt * 1.45)
    readonly property int fontSizeGroup: Math.round(fontSizePt * 0.95)
    readonly property int fontSizeSmall: Math.round(fontSizePt * 0.85)

    // --- Metrics ----------------------------------------------------------

    readonly property int radiusPanel: Appearance.cornerRadius
    // The pills inside follow the text they hold, not the plate they sit on.
    // Both settings are about the panel as a shape: its corners and its frame.
    // Handing those two numbers to the keys as well turned a key into a box
    // with a letter in it at one end of the sliders and into a lozenge at the
    // other, and neither is what the sliders were reached for. At the size the
    // panel starts at this is the same six pixels the keys have always had.
    readonly property int radiusPill: Math.round(fontSizeBody * 0.45)
    readonly property int borderWidth: Appearance.borderWidth
    // A hairline, and it stays one whatever the frame is set to. A key is
    // marked off from the plate, not framed like it.
    readonly property int borderWidthKey: 1

    // Spacing follows the font size as well, so a larger panel does not end up
    // with cramped gaps.
    readonly property int paddingPanel: Math.round(fontSizeBody * 1.7)
    readonly property int paddingPill: Math.round(fontSizeBody * 0.6)
    readonly property int spacingGroup: Math.round(fontSizeBody * 1.4)
    readonly property int spacingRow: Math.round(fontSizeBody * 0.45)
    readonly property int spacingColumn: Math.round(fontSizeBody * 0.85)

    // What a row adds on its right where a group wraps into a further column.
    //
    // The wrap is a Flow, and a Flow holds its rows and its columns the same
    // distance apart. The rows want the tight row gap, so the rest of the way
    // up to the group gap is carried by the rows themselves. Two columns of
    // one group then stand exactly as far apart as two groups side by side,
    // which is what keeps the plate reading as one table.
    readonly property int gutterWrap: spacingGroup - spacingRow

    readonly property int columnShortcut: Math.round(fontSizeBody * 10)
    readonly property int columnDescription: Math.round(fontSizeBody * 18.5)

    // Smallest a bounded panel may be squeezed to. Below this the rows are
    // shreds of text rather than a cheat sheet, so a very small screen or a
    // very large margin is allowed to be overrun instead.
    readonly property int minPanelWidth: 320
    readonly property int minPanelHeight: 240

    // The screen the panel is measured against.
    //
    // Handed in by whoever draws it: a window knows which output it is on,
    // and that is the honest answer for a panel that sizes itself. Appearance
    // measures the smallest screen attached and stands in until something
    // better is known, which is the safe answer rather than the right one.
    property int screenWidth: Appearance.screenWidth
    property int screenHeight: Appearance.screenHeight

    // Largest the panel may grow to. Tied to the screen rather than to fixed
    // numbers: a cheat sheet that runs past the edge is exactly as useless as
    // one that is cut off, and any fixed number is generous on one display and
    // half the width on the next.
    //
    // Each axis is reduced by what is actually kept clear on it, ends counted,
    // which Appearance has already worked out from the position.
    readonly property int maxPanelWidth: Math.max(minPanelWidth, screenWidth - Appearance.horizontalReservedPx)
    readonly property int maxPanelHeight: Math.max(minPanelHeight, screenHeight - Appearance.verticalReservedPx)
}
