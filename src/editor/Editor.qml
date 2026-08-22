// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Theme.qml is shared with the overlay and sits one level up in the resources.
import ".."

// The settings window. Left half sets values, right half shows what they do.
//
// The preview is not a drawing of the panel but the same Theme.qml the overlay
// renders with, fed by the same Appearance class. A change here therefore
// cannot look different there.
Window {
    id: win
    width: ui.windowWidth
    height: ui.windowHeight
    minimumWidth: ui.windowMinWidth
    minimumHeight: ui.windowMinHeight
    title: qsTr("bindpeek settings")
    color: ui.background
    // Starts hidden: the tray icon decides when it appears.
    visible: false

    Theme {
        id: preview

        // The display this window is on, rather than the one Appearance keeps.
        //
        // That one is the box that fits inside every screen attached, taken
        // width and height apart: a wide screen next to an upright one answers
        // with a square no monitor has. As the bound for a panel that has to
        // fit wherever the compositor puts it, that is exactly right. As the
        // answer to which way the groups run, it is a shape nobody is looking
        // at, and the preview would draw a column while the panel drew a band.
        //
        // Only the shape is decided by these two here. What the preview may
        // grow to is the box it sits in, which is handed to it further down.
        //
        // Asked plainly, without waiting for the window to have landed
        // somewhere. There is nothing to wait for: this preview is looked at
        // while the window is up and at no other time, and while it is up Qt
        // names its screen. What it works out before that is drawn on nobody's
        // screen. Opened onto a display the window was not expected on, the
        // shape can still be redrawn once, which is one redrawing against a
        // shape that would have been wrong for as long as the window stood
        // there.
        screenWidth: win.screen ? win.screen.width : Appearance.screenWidth
        screenHeight: win.screen ? win.screen.height : Appearance.screenHeight
    }
    // The editor wears the palette the overlay is set to, so a choice is seen
    // rather than described.
    EditorTheme {
        id: ui
        palette: preview
    }

    AboutDialog {
        id: about
        ui: ui
    }

    // Putting every value back at once cannot be undone: what was set a
    // moment ago is gone, and nothing anywhere remembers it. The same dialog
    // the window close goes through asks first.
    ConfirmDialog {
        id: resetConfirm
        ui: ui
        question: qsTr("Restore the defaults?")
        explanation: qsTr("Every setting goes back to the value it has on a first start. What is set now cannot be brought back.")
        confirmText: qsTr("Restore")
        onConfirmed: SettingsModel.resetToDefaults()
    }

    // Closing the window leaves the program running in the tray, which is easy
    // to mistake for having quit. The dialog says so instead of leaving it to
    // be discovered. It appears every time: there is nowhere to remember that
    // it has been read.
    ConfirmDialog {
        id: closeConfirm
        ui: ui
        question: qsTr("Close the settings window?")
        explanation: qsTr("bindpeek keeps running in the tray. Open the settings again from its icon.")
        confirmText: qsTr("Close")
        onConfirmed: win.hide()
    }

    // The window manager button has to go through the same question. It is the
    // way most people close a window, and it is exactly the case the dialog
    // was built for: the program stays in the tray and looks quit.
    onClosing: function (close) {
        close.accepted = false;
        closeConfirm.open();
    }

    // The palette knows its own colours; AppInfo only needs to be told which
    // way round they are, so it can pick a mark that stays visible.
    Binding {
        target: AppInfo
        property: "darkSurface"
        value: preview.surface.hsvValue < 0.5
    }

    // --- small building blocks -------------------------------------------
    // The rows in the order the controller would put them: what fires on the
    // next key first, then what is one modifier away, and among equals by
    // where those modifiers stand in the display order.
    //
    // The order itself is asked for rather than written down here, so there is
    // one opinion about which modifier comes first.
    //
    // Where the row was written is the last key, and it is not decoration.
    // Measured on this engine, a sort moves four rows a comparison calls
    // equal, so without it the order the sample was written in would be lost
    // and the preview would list what no panel lists. The C++ side keeps it
    // with a stable sort; this keeps it by asking.
    function sortedEntries(entries) {
        var order = Appearance.modifierOrder;
        var rows = [];
        for (var i = 0; i < entries.length; ++i)
            rows.push({
                at: i,
                entry: entries[i]
            });
        rows.sort(function (left, right) {
            if (left.entry.caps.length !== right.entry.caps.length)
                return left.entry.caps.length - right.entry.caps.length;
            for (var j = 0; j < left.entry.caps.length; ++j) {
                var here = order.indexOf(left.entry.caps[j]);
                var there = order.indexOf(right.entry.caps[j]);
                if (here < 0)
                    here = order.length;
                if (there < 0)
                    there = order.length;
                if (here !== there)
                    return here - there;
            }
            return left.at - right.at;
        });
        var out = [];
        for (var k = 0; k < rows.length; ++k)
            out.push(rows[k].entry);
        return out;
    }

    // The made-up list put through both arrangements, which for the preview
    // means doing here what the controller does for the real panel: the rows
    // of every group in its order, and where the setting asks for it, a group
    // per combination instead of the headings the sample carries.
    //
    // Written out a second time rather than shared, because there is nothing
    // to share with: the controller works on binds and modifiers in C++, this
    // works on the made-up list this file holds. What has to agree is the
    // shape of the answer, and that is what the preview shows.
    function previewGroups(groups) {
        var out = [];
        if (!Appearance.arrangesByModifier) {
            for (var g = 0; g < groups.length; ++g)
                out.push({
                    name: groups[g].name,
                    entries: win.sortedEntries(groups[g].entries)
                });
            return out;
        }
        var all = [];
        for (var h = 0; h < groups.length; ++h)
            for (var e = 0; e < groups[h].entries.length; ++e)
                all.push(groups[h].entries[e]);
        var rows = win.sortedEntries(all);
        for (var i = 0; i < rows.length; ++i) {
            if (out.length === 0 || out[out.length - 1].name !== rows[i].section)
                out.push({
                    name: rows[i].section,
                    entries: []
                });
            out[out.length - 1].entries.push(rows[i]);
        }
        return out;
    }

    component Heading: Text {
        color: ui.textMuted
        font.family: ui.fontFamily
        font.pixelSize: ui.fontSizeHeading
        font.capitalization: Font.AllUppercase
        font.letterSpacing: ui.headingLetterSpacing
    }

    component Row_: RowLayout {
        id: row
        property string label: ""
        Layout.fillWidth: true
        spacing: ui.spacingRow
        Text {
            text: row.label
            color: ui.text
            font.family: ui.fontFamily
            font.pixelSize: ui.fontSize
            Layout.preferredWidth: ui.labelWidth
        }
    }

    component Box: Rectangle {
        color: ui.surface
        border.color: ui.line
        border.width: ui.lineWidth
        radius: ui.radius
    }

    // A bar that can be seen and taken hold of, not the thin marker that only
    // appears while something is already being scrolled.
    //
    // Shown exactly while there is more than fits, which is what `size` says:
    // a view showing all of its content reports one, anything less means the
    // list goes on past its edge. A window too small for every setting is
    // otherwise a window that appears to have fewer of them.
    component Bar: ScrollBar {
        id: bar

        policy: bar.size < 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        // As wide as the room the rows keep clear for it.
        width: ui.scrollRoom

        contentItem: Rectangle {
            implicitWidth: ui.scrollBarWidth
            radius: width / 2
            color: bar.pressed ? ui.text : bar.hovered ? ui.lineHover : ui.textMuted
        }
        background: Rectangle {
            color: "transparent"
        }
    }

    // A slider with its value spelled out, because a bare handle says nothing
    // about milliseconds or pixels.
    component ValueSlider: RowLayout {
        id: vs
        property alias from: slider.from
        property alias to: slider.to
        property alias value: slider.value
        property alias stepSize: slider.stepSize
        property string suffix: ""
        // Digits follow the step, so a value that moves in hundredths is
        // written with two and one that moves in whole units with none.
        readonly property int decimals: Math.max(0, -Math.floor(Math.log10(vs.stepSize)))
        signal moved(real v)

        spacing: ui.spacingRow

        Slider {
            id: slider
            Layout.preferredWidth: ui.controlWidth
            snapMode: Slider.SnapAlways
            stepSize: 1
            // Reachable with the tab key, and it moves with the arrow keys
            // once it is: every value here can be set without a pointer.
            focusPolicy: Qt.StrongFocus
            onMoved: vs.moved(value)
            // The arrow keys move the handle without going through onMoved,
            // so the value would change on screen and never be written.
            onValueChanged: if (slider.activeFocus)
                vs.moved(slider.value)

            background: Rectangle {
                x: slider.leftPadding
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: slider.availableWidth
                height: ui.sliderTrackHeight
                color: ui.line
                radius: ui.radius
                Rectangle {
                    width: slider.visualPosition * parent.width
                    height: parent.height
                    color: ui.accent
                    radius: ui.radius
                }
            }
            handle: Rectangle {
                x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
                y: slider.topPadding + slider.availableHeight / 2 - height / 2
                width: ui.sliderHandleWidth
                height: ui.sliderHandleHeight
                radius: ui.radius
                color: slider.pressed ? ui.accent : ui.text
                // The grip says where the keyboard is, or a tab into it would
                // move a value nobody can see is selected.
                border.color: slider.activeFocus ? ui.accent : "transparent"
                border.width: ui.lineWidth
            }
        }
        Text {
            text: vs.value.toFixed(vs.decimals) + vs.suffix
            color: ui.textMuted
            font.family: ui.fontFamilyMono
            font.pixelSize: ui.fontSize
            Layout.preferredWidth: ui.sliderValueWidth
        }
    }

    component Choice: ComboBox {
        id: cb

        Layout.preferredWidth: ui.controlWidth
        font.family: ui.fontFamily
        font.pixelSize: ui.fontSize

        background: Rectangle {
            color: cb.hovered ? ui.surfaceHover : ui.surface
            border.color: cb.activeFocus ? ui.accent : cb.hovered ? ui.lineHover : ui.line
            border.width: ui.lineWidth
            radius: ui.radius
        }
        contentItem: Text {
            leftPadding: ui.paddingControl
            // Room for the mark, so a long entry is elided rather than drawn
            // underneath it.
            rightPadding: ui.paddingControl * 2 + ui.fontSizeIcon
            text: cb.displayText === "" ? qsTr("(automatic)") : cb.displayText
            color: ui.text
            font: cb.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // Drawn here rather than left to the style. Everything else in this
        // window takes its colour from the palette the user picked; the one
        // thing that did not was this mark, and on a dark palette the style's
        // own is all but invisible.
        indicator: Text {
            x: cb.width - width - ui.paddingControl
            y: (cb.height - height) / 2
            text: ui.iconDropdown
            color: cb.hovered || cb.activeFocus ? ui.text : ui.textMuted
            font.family: ui.fontFamilyIcon
            font.pixelSize: ui.fontSizeIcon
        }
        popup: Popup {
            y: cb.height
            width: cb.width
            implicitHeight: Math.min(contentItem.implicitHeight, ui.popupMaxHeight)
            padding: ui.lineWidth
            background: Rectangle {
                color: ui.surface
                // The open popup takes the hover colour for its edge as well.
                // Its highlighted row is painted in surfaceHover and sits
                // flush against this border, and in the palettes where those
                // two are the same value the edge disappears along with it.
                border.color: ui.lineHover
                border.width: ui.lineWidth
                radius: ui.radius
            }
            contentItem: ListView {
                id: choiceList
                clip: true
                implicitHeight: contentHeight
                model: cb.popup.visible ? cb.delegateModel : null

                // A list of fourteen palettes in a box that shows eight of
                // them has to say so before it is touched; see Bar.
                ScrollBar.vertical: Bar {}
            }
        }
        delegate: ItemDelegate {
            id: item
            required property var modelData
            required property int index
            width: cb.width
            // The distance from the edge is set on the text below, so the
            // control's own side padding is taken out of the way. Left
            // standing it adds to that distance, and a name would step
            // sideways as the list opens, which is the one movement a dropdown
            // must not make. Only the sides: the padding above and below is
            // what gives a row its height here, the background being a plain
            // rectangle with no height of its own, and nulling all four would
            // press the rows against each other.
            horizontalPadding: 0
            highlighted: cb.highlightedIndex === index
            background: Rectangle {
                color: item.highlighted ? ui.surfaceHover : "transparent"
                radius: ui.radius
            }
            contentItem: Text {
                // The same distance from the edge as the closed field, so a
                // name does not move sideways when the list opens, plus the
                // room the bar takes on the other side.
                leftPadding: ui.paddingControl
                rightPadding: ui.paddingControl + ui.scrollRoom
                text: item.modelData === "" ? qsTr("(automatic)") : item.modelData
                color: ui.text
                font.family: ui.fontFamily
                font.pixelSize: ui.fontSize
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // The switch has to state its own size.
    //
    // A Switch takes its width from its content item, never from its
    // indicator: the stock one carries a label whose left padding is the
    // indicator's width, and that is the only thing holding the two apart.
    // This one has no label, so without the two lines below the control ends
    // up as wide as its padding, the track is drawn outside it, and the few
    // pixels that overlap are the only place a click counts. Measured in Qt
    // 6.11: 12 pixels of control under a track drawn 40 wide.
    component Toggle: Switch {
        id: sw
        implicitWidth: leftPadding + ui.switchWidth + rightPadding
        implicitHeight: topPadding + ui.switchHeight + bottomPadding
        // Tab reaches it, space flips it.
        focusPolicy: Qt.StrongFocus
        indicator: Rectangle {
            implicitWidth: ui.switchWidth
            implicitHeight: ui.switchHeight
            x: sw.leftPadding
            y: sw.topPadding + (sw.availableHeight - height) / 2
            radius: ui.radius
            color: sw.checked ? ui.accentDim : ui.surface
            border.color: sw.activeFocus ? ui.accent : sw.checked ? ui.accent : sw.hovered ? ui.lineHover : ui.line
            border.width: ui.lineWidth
            Rectangle {
                x: sw.checked ? parent.width - width - ui.switchGap : ui.switchGap
                y: ui.switchGap
                width: ui.switchKnob
                height: ui.switchKnob
                radius: ui.radius
                color: sw.checked ? ui.accent : ui.textMuted
            }
        }
        contentItem: Item {}
    }

    // A square button carrying one symbol.
    //
    // The symbol is set in a face of its own, because the one an interface is
    // set in seldom carries these blocks and each glyph would otherwise be
    // pulled from a different substitute. The tooltip is not decoration: a
    // symbol with no words next to it is a guess until it is hovered, and the
    // same text names the button for a screen reader.
    component IconButton: Button {
        id: iconBtn
        required property string symbol
        required property string tip

        Accessible.role: Accessible.Button
        Accessible.name: iconBtn.tip

        ThemedToolTip {
            ui: ui
            hovered: iconBtn.hovered
            text: iconBtn.tip
        }

        background: Rectangle {
            implicitWidth: ui.iconButtonSize
            implicitHeight: ui.iconButtonSize
            color: iconBtn.down || iconBtn.hovered ? ui.surfaceHover : "transparent"
            border.color: iconBtn.activeFocus ? ui.accent : iconBtn.down || iconBtn.hovered ? ui.lineHover : "transparent"
            border.width: ui.lineWidth
            radius: ui.radius
        }
        contentItem: Text {
            text: iconBtn.symbol
            color: iconBtn.hovered || iconBtn.activeFocus ? ui.text : ui.textMuted
            font.family: ui.fontFamilyIcon
            font.pixelSize: ui.fontSizeIcon
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // One of the three alignment buttons.
    //
    // Three marks rather than a dropdown: they are one question with three
    // answers, and the mark on each says what it does without a word. The
    // tooltip carries the word for anyone who wants it spelled out, and the
    // accessible name carries it for anyone who cannot see the mark.
    component AlignButton: Button {
        id: alignBtn
        // The word this button stands for, taken from the setting itself.
        required property string word
        property bool atStart: false
        property bool atEnd: false
        // What this button does, in one place for the tooltip and for
        // anything reading the window aloud.
        //
        // Where the setting does not apply the button says that instead: it
        // is the same fact as its being switched off, and saying it at each
        // of the three would be saying it three times.
        required property string tip
        readonly property string spoken: alignBtn.enabled ? alignBtn.tip : qsTr("Only applies to a panel at an edge")

        // Deliberately not checkable. A checkable button owns its own state:
        // clicking the one already chosen unchecks it, and the click writes a
        // value the setting already holds, so nothing announces a change and
        // the row is left with nothing marked. Measured on Qt 6.11. Left to a
        // plain binding, the marks follow the setting and cannot disagree
        // with it.
        checked: SettingsModel.alignment === alignBtn.word
        onClicked: SettingsModel.alignment = alignBtn.word
        enabled: SettingsModel.anchoredToEdge

        Accessible.role: Accessible.RadioButton
        Accessible.name: alignBtn.spoken

        ThemedToolTip {
            ui: ui
            hovered: alignBtn.hovered
            text: alignBtn.spoken
        }

        background: Rectangle {
            implicitWidth: ui.iconButtonSize
            implicitHeight: ui.iconButtonSize
            color: alignBtn.checked || alignBtn.down || alignBtn.hovered ? ui.surfaceHover : "transparent"
            border.color: alignBtn.activeFocus ? ui.accent : alignBtn.checked ? ui.lineHover : "transparent"
            border.width: ui.lineWidth
            radius: ui.radius
        }
        contentItem: AlignIcon {
            ui: ui
            atStart: alignBtn.atStart
            atEnd: alignBtn.atEnd
            // The mark follows the axis the panel is actually stretched
            // along, so a button on a bottom panel shows left, centre and
            // right, and the same button on a side panel shows top, middle
            // and bottom.
            across: !Appearance.spanVertical
            colour: !alignBtn.enabled ? ui.textMuted : alignBtn.checked || alignBtn.hovered ? ui.text : ui.textMuted
        }
    }

    component Button_: Button {
        id: btn
        property bool primary: false
        font.family: ui.fontFamily
        font.pixelSize: ui.fontSize
        background: Rectangle {
            implicitWidth: ui.buttonWidth
            implicitHeight: ui.buttonHeight
            color: !btn.enabled ? ui.surface : btn.down ? ui.surfaceHover : btn.primary ? ui.accentDim : ui.surface
            border.color: btn.activeFocus || (btn.primary && btn.enabled) ? ui.accent : btn.down || btn.hovered ? ui.lineHover : ui.line
            border.width: ui.lineWidth
            radius: ui.radius
        }
        contentItem: Text {
            text: btn.text
            color: btn.enabled ? ui.text : ui.textMuted
            font: btn.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // --- layout -----------------------------------------------------------

    // Escape is handled here rather than through a Shortcut. A shortcut is
    // matched before the focused item ever sees the key, which would take
    // Escape away from every open dropdown and dialog. Letting the key travel
    // up the focus chain instead means anything open on top consumes it first
    // and only an otherwise idle window asks the question.
    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: closeConfirm.open()

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: ui.paddingWindow
            spacing: ui.paddingWindow

            // --- header --------------------------------------------------------

            // What the window is, and the two things that are neither a
            // setting nor a preview: the way back to the defaults and the way
            // to what this program is. Both sit here rather than under the
            // settings, where they would read as one more value to choose.
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: ui.headerHeight
                spacing: ui.spacingRow

                Image {
                    source: AppInfo.iconSource
                    sourceSize.width: ui.headerIconSize * 2
                    sourceSize.height: ui.headerIconSize * 2
                    Layout.preferredWidth: ui.headerIconSize
                    Layout.preferredHeight: ui.headerIconSize
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }

                Text {
                    text: AppInfo.name
                    color: ui.text
                    font.family: ui.fontFamily
                    font.pixelSize: ui.fontSizeTitle
                    font.weight: Font.DemiBold
                }

                Item {
                    Layout.fillWidth: true
                }

                IconButton {
                    symbol: ui.iconInfo
                    tip: qsTr("About")
                    onClicked: about.open()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: ui.paddingWindow

                // --- settings ------------------------------------------------------

                // The settings, and they scroll on their own rather than
                // inside a view that would carry them.
                //
                // A view of that kind hands out a flickable, or takes the one
                // it is given; measured before this was written, it did
                // neither. What is written here stood at no height at all
                // while what it held was a thousand pixels tall, so nothing
                // scrolled and the bar beside it reported that everything
                // fitted. A list that scrolls, its bar, and nothing in
                // between cannot come apart that way.
                Flickable {
                    id: settingsFlick

                    // Tabbing must bring the focused row into view.
                    //
                    // Nothing does that on its own: the focus travels down the
                    // list whether or not the row is on screen, and a focus
                    // ring nobody can see is worse than none, because the next
                    // key press then lands somewhere invisible.
                    //
                    // Only rows of this list are followed. The focus also goes
                    // to the header buttons and into dialogs, and scrolling to
                    // those would move the list for no reason.
                    function holds(item) {
                        for (var at = item; at; at = at.parent)
                            if (at === settingsFlick)
                                return true;
                        return false;
                    }
                    function reveal(item) {
                        if (!item || !settingsFlick.holds(item))
                            return;
                        // Mapped into the content item, not into the
                        // Flickable: the rows live in the content item, which
                        // sits at -contentY, so a position taken against the
                        // Flickable is where the row is on screen rather than
                        // where it is in the list. The two agree only while
                        // the list is scrolled to the top, which is exactly
                        // the case that hides the mistake.
                        var top = item.mapToItem(settingsFlick.contentItem, 0, 0).y - ui.spacingRow;
                        var bottom = top + item.height + ui.spacingRow * 2;
                        if (top < settingsFlick.contentY)
                            settingsFlick.contentY = Math.max(0, top);
                        else if (bottom > settingsFlick.contentY + settingsFlick.height)
                            settingsFlick.contentY = bottom - settingsFlick.height;
                    }

                    Layout.fillHeight: true
                    // The rows keep their width and the bar is given room
                    // beside them rather than over them, which is why this is
                    // wider than the rows are.
                    Layout.preferredWidth: ui.settingsWidth + ui.scrollRoom
                    clip: true

                    contentWidth: settingsFlick.width - ui.scrollRoom
                    contentHeight: settingsColumn.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds

                    // A window shorter than the list is the ordinary case, not
                    // the odd one: there are more settings here than fit a
                    // small window, and without a bar the ones below the edge
                    // are settings the reader has no reason to believe exist.
                    //
                    // Nothing is said here about where it stands. A bar given
                    // to a list of this kind places itself along its edge,
                    // which is the whole reason it is given to the list.
                    ScrollBar.vertical: Bar {}

                    Connections {
                        target: win
                        function onActiveFocusItemChanged() {
                            settingsFlick.reveal(win.activeFocusItem);
                        }
                    }

                    ColumnLayout {
                        id: settingsColumn

                        // As wide as the rows are allowed to be, which is the
                        // list less the room kept clear for the bar.
                        width: settingsFlick.contentWidth
                        spacing: ui.spacingSection

                        // Whether the panel is up at all, first of
                        // everything and as a switch rather than a report.
                        //
                        // The one question a reader arrives with is why
                        // nothing appears on screen, and it used to be
                        // answered by a line in the smallest type in the
                        // furthest corner. It also answered only half:
                        // the panel could be switched on from the tray
                        // alone, which is the one part of the program
                        // that has to be found before it can be used.
                        //
                        // Above the first heading rather than under one:
                        // it is not a setting among settings, it is
                        // whether any of them are doing anything.
                        Row_ {
                            label: qsTr("Overlay")
                            Toggle {
                                id: overlaySwitch
                                checked: OverlayControl.running
                                // A session that cannot host the panel
                                // says so instead of offering a switch
                                // that does nothing.
                                enabled: OverlayControl.usable
                                // The order of the two things a switch
                                // does lives with the panel, not here and
                                // again in the tray.
                                onToggled: OverlayControl.requestToggle(SettingsModel)

                                ThemedToolTip {
                                    ui: ui
                                    hovered: overlaySwitch.hovered
                                    text: !OverlayControl.usable ? OverlayControl.unsupportedReason : OverlayControl.running ? qsTr("The panel is up. Hold a modifier to see it.") : qsTr("The panel is off. Nothing appears on screen.")
                                }
                            }
                        }

                        Heading {
                            text: qsTr("Behaviour")
                        }

                        Row_ {
                            label: qsTr("Delay")
                            ValueSlider {
                                from: SettingsModel.showDelayMin
                                to: SettingsModel.showDelayMax
                                stepSize: SettingsModel.showDelayStep
                                value: SettingsModel.showDelayMs
                                suffix: " ms"
                                onMoved: function (v) {
                                    SettingsModel.showDelayMs = v;
                                }
                            }
                        }

                        // Not a placement matter and not an appearance one: it
                        // decides whether a question is asked at all.
                        Row_ {
                            label: qsTr("Ignore a lone Shift")
                            Toggle {
                                checked: SettingsModel.ignoreLoneShift
                                onToggled: SettingsModel.ignoreLoneShift = checked
                            }
                        }

                        // Under a heading of its own rather than among the
                        // two above it. Those say when the panel appears;
                        // this says what is on it, which is a different
                        // question and the most consequential one the
                        // program asks.
                        Heading {
                            text: qsTr("Contents")
                        }

                        // What a further modifier would reach is always shown; this
                        // only says how. The preview next to it draws each one, so
                        // the words are compared by looking rather than by reading.
                        Row_ {
                            label: qsTr("Deeper shortcuts")
                            Choice {
                                model: SettingsModel.disclosures
                                currentIndex: SettingsModel.disclosures.indexOf(SettingsModel.disclosure)
                                onActivated: function (i) {
                                    SettingsModel.disclosure = SettingsModel.disclosures[i];
                                }
                            }
                        }

                        // Which headings the list is cut into: the ones
                        // the session gives, or one per combination. The
                        // preview beside it draws both, so the two words
                        // are told apart by looking.
                        Row_ {
                            label: qsTr("Grouped by")
                            Choice {
                                model: SettingsModel.arrangements
                                currentIndex: SettingsModel.arrangements.indexOf(SettingsModel.arrangement)
                                onActivated: function (i) {
                                    SettingsModel.arrangement = SettingsModel.arrangements[i];
                                }
                            }
                        }

                        Heading {
                            text: qsTr("Placement")
                        }

                        Row_ {
                            label: qsTr("Position")
                            Choice {
                                model: SettingsModel.positions
                                currentIndex: SettingsModel.positions.indexOf(SettingsModel.position)
                                onActivated: function (i) {
                                    SettingsModel.position = SettingsModel.positions[i];
                                }
                            }
                        }
                        // Where the content sits along the edge the panel
                        // spans. Nothing to see in the centre position,
                        // which spans nothing and is therefore as wide as
                        // what it holds.
                        Row_ {
                            label: qsTr("Alignment")
                            RowLayout {
                                spacing: ui.spacingTight

                                // Written out rather than repeated over a
                                // list: each button names the word it
                                // stands for and which end it marks, so
                                // nothing has to be worked out from where
                                // a word happens to sit in that list.
                                AlignButton {
                                    word: SettingsModel.alignmentStart
                                    atStart: true
                                    tip: Appearance.spanVertical ? qsTr("Top") : qsTr("Left")
                                }
                                AlignButton {
                                    word: SettingsModel.alignmentCenter
                                    tip: Appearance.spanVertical ? qsTr("Middle") : qsTr("Centred")
                                }
                                AlignButton {
                                    word: SettingsModel.alignmentEnd
                                    atEnd: true
                                    tip: Appearance.spanVertical ? qsTr("Bottom") : qsTr("Right")
                                }
                            }
                        }
                        // Two distances, and they are not the same one: how far
                        // the panel sits from its edge, and how far it stops short
                        // of that edge's two ends.
                        Row_ {
                            label: qsTr("Distance to edge")
                            ValueSlider {
                                from: SettingsModel.marginMin
                                to: SettingsModel.marginMax
                                value: SettingsModel.marginPx
                                suffix: " px"
                                enabled: SettingsModel.anchoredToEdge
                                onMoved: function (v) {
                                    SettingsModel.marginPx = v;
                                }
                            }
                        }
                        Row_ {
                            label: qsTr("Distance at the ends")
                            ValueSlider {
                                from: SettingsModel.marginMin
                                to: SettingsModel.marginMax
                                value: SettingsModel.edgeInsetPx
                                suffix: " px"
                                enabled: SettingsModel.anchoredToEdge
                                onMoved: function (v) {
                                    SettingsModel.edgeInsetPx = v;
                                }
                            }
                        }

                        Heading {
                            text: qsTr("Colours")
                        }

                        Row_ {
                            label: qsTr("Follow the system")
                            Toggle {
                                checked: SettingsModel.followSystemScheme
                                onToggled: SettingsModel.followSystemScheme = checked
                            }
                        }
                        Row_ {
                            label: SettingsModel.followSystemScheme ? qsTr("Light palette") : qsTr("Palette")
                            Choice {
                                model: SettingsModel.themes
                                currentIndex: SettingsModel.themes.indexOf(SettingsModel.followSystemScheme ? SettingsModel.themeLight : SettingsModel.theme)
                                onActivated: function (i) {
                                    if (SettingsModel.followSystemScheme)
                                        SettingsModel.themeLight = SettingsModel.themes[i];
                                    else
                                        SettingsModel.theme = SettingsModel.themes[i];
                                }
                            }
                        }
                        Row_ {
                            label: qsTr("Dark palette")
                            visible: SettingsModel.followSystemScheme
                            Choice {
                                model: SettingsModel.themes
                                currentIndex: SettingsModel.themes.indexOf(SettingsModel.themeDark)
                                onActivated: function (i) {
                                    SettingsModel.themeDark = SettingsModel.themes[i];
                                }
                            }
                        }

                        Heading {
                            text: qsTr("Type")
                        }

                        Row_ {
                            label: qsTr("Font")
                            Choice {
                                model: SettingsModel.fontFamilies
                                currentIndex: SettingsModel.fontFamilies.indexOf(SettingsModel.fontFamily)
                                onActivated: function (i) {
                                    SettingsModel.fontFamily = SettingsModel.fontFamilies[i];
                                }
                            }
                        }
                        Row_ {
                            label: qsTr("Font size")
                            ValueSlider {
                                from: SettingsModel.fontSizeMin
                                to: SettingsModel.fontSizeMax
                                value: SettingsModel.fontSizePt
                                suffix: " pt"
                                onMoved: function (v) {
                                    SettingsModel.fontSizePt = v;
                                }
                            }
                        }

                        // Named for what is in it. The three below are the
                        // plate itself: how its corners are cut, what
                        // holds its edge, and how much of the desktop
                        // shows through it. Only the first two are a
                        // frame, and a heading that names one of three is
                        // where a setting goes missing.
                        Heading {
                            text: qsTr("Surface")
                        }

                        Row_ {
                            label: qsTr("Corner radius")
                            ValueSlider {
                                from: SettingsModel.radiusMin
                                to: SettingsModel.radiusMax
                                value: SettingsModel.cornerRadiusPx
                                suffix: " px"
                                onMoved: function (v) {
                                    SettingsModel.cornerRadiusPx = v;
                                }
                            }
                        }
                        Row_ {
                            label: qsTr("Border")
                            ValueSlider {
                                from: SettingsModel.borderMin
                                to: SettingsModel.borderMax
                                value: SettingsModel.borderWidthPx
                                suffix: " px"
                                onMoved: function (v) {
                                    SettingsModel.borderWidthPx = v;
                                }
                            }
                        }
                        Row_ {
                            label: qsTr("Opacity")
                            ValueSlider {
                                from: SettingsModel.opacityMin
                                to: SettingsModel.opacityMax
                                stepSize: SettingsModel.opacityStep
                                value: SettingsModel.opacity
                                onMoved: function (v) {
                                    SettingsModel.opacity = v;
                                }
                            }
                        }

                        // The way back, at the end of the values it undoes and
                        // set apart from them by a rule: it is not a setting
                        // but an act on all of them at once.
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: ui.spacingSection
                            Layout.preferredHeight: ui.lineWidth
                            color: ui.line
                        }

                        Button_ {
                            Layout.topMargin: ui.spacingRow
                            text: qsTr("Restore defaults")
                            onClicked: resetConfirm.open()
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }
                }

                // --- preview -------------------------------------------------------

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: ui.spacingRow

                    Heading {
                        text: qsTr("Preview")
                    }

                    Box {
                        id: previewBox
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: ui.background
                        clip: true

                        // Drawn faintly while the panel is off. The preview is
                        // what the eye goes to, and one that looks the same
                        // whether or not anything is on screen is what leaves
                        // a reader wondering where their panel is.
                        opacity: OverlayControl.running ? 1 : ui.dimmedOpacity

                        // The panel itself, not a drawing of it: the same item the
                        // overlay puts on screen, with a made-up list and the
                        // shape the chosen position gives it. Anything that can be
                        // seen here can therefore not look different there.
                        //
                        // Bounded by this box rather than by the screen, so the
                        // preview stays inside the window while the real panel is
                        // measured against the display.
                        PanelBody {
                            anchors.centerIn: parent
                            theme: preview
                            groupsAcross: preview.groupsAcross
                            alignsAtStart: Appearance.alignsAtStart
                            alignsAtEnd: Appearance.alignsAtEnd
                            deeperInSections: Appearance.deeperInSections
                            arrangesByModifier: Appearance.arrangesByModifier
                            showContinuations: Appearance.showContinuations
                            maxWidth: previewBox.width - ui.paddingBox * 2
                            maxHeight: previewBox.height - ui.paddingBox * 2
                            // Stretched along the same axis the compositor
                            // stretches the real one. Without this the plate
                            // is exactly as wide as what it holds, and the
                            // alignment has nowhere to move it to: the one
                            // setting in this group whose effect the preview
                            // could not show.
                            width: Appearance.spanHorizontal ? maxWidth : implicitWidth
                            height: Appearance.spanVertical ? maxHeight : implicitHeight
                            heldText: "SUPER"
                            // What the two further keys would still reach, in the
                            // same shape the controller delivers.
                            continuations: [
                                {
                                    modifier: "CTRL",
                                    count: 1
                                },
                                {
                                    modifier: "SHIFT",
                                    count: 1
                                }
                            ]
                            // The keys and names are invented, the shape is not.
                            // One row that fires at once and one that wants a
                            // further modifier, because telling those two apart is
                            // what the panel is for. The field names are the roles
                            // OverlayController fills in.
                            readonly property var sampleGroups: [
                                {
                                    name: qsTr("Programs"),
                                    entries: [
                                        {
                                            shortcut: "T",
                                            key: "T",
                                            description: "ghostty",
                                            deeper: false,
                                            section: "SUPER",
                                            caps: ["SUPER"]
                                        },
                                        {
                                            shortcut: "B",
                                            key: "B",
                                            description: "firefox",
                                            deeper: false,
                                            section: "SUPER",
                                            caps: ["SUPER"]
                                        },
                                        {
                                            shortcut: "SHIFT+T",
                                            key: "T",
                                            description: "kitty",
                                            deeper: true,
                                            section: "SUPER+SHIFT",
                                            caps: ["SUPER", "SHIFT"]
                                        }
                                    ]
                                },
                                {
                                    name: qsTr("Windows"),
                                    entries: [
                                        {
                                            shortcut: "←",
                                            key: "←",
                                            description: qsTr("Focus left"),
                                            deeper: false,
                                            section: "SUPER",
                                            caps: ["SUPER"]
                                        },
                                        {
                                            shortcut: "CTRL+C",
                                            key: "C",
                                            description: qsTr("Close window"),
                                            deeper: true,
                                            section: "SUPER+CTRL",
                                            caps: ["SUPER", "CTRL"]
                                        }
                                    ]
                                }
                            ]

                            // The same list the controller would hand over,
                            // put through the two settings that decide what
                            // reaches the panel at all.
                            //
                            // A disclosure that answers only the keys being
                            // held never gets the deeper rows: the controller
                            // drops them before the panel sees them, and a
                            // preview that kept them would show the one thing
                            // the setting is chosen by as no difference at
                            // all. The arrangement is the same case a second
                            // time, one heading further up.
                            groups: win.previewGroups(Appearance.showsDeeper ? sampleGroups : sampleGroups.map(function (group) {
                                return {
                                    name: group.name,
                                    entries: group.entries.filter(function (entry) {
                                        return !entry.deeper;
                                    })
                                };
                            }).filter(function (group) {
                                return group.entries.length > 0;
                            }))
                        }
                    }

                    // --- actions ----------------------------------------------------

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: ui.spacingRow

                        // Kept for what is worth saying and nothing else.
                        //
                        // Whether the panel is up is answered by the switch in
                        // the header, and that every change takes effect at
                        // once is answered by the preview moving while a
                        // slider is dragged. A line that says the same thing
                        // in every state is read once and then stops being
                        // read, which is a poor place for the one line that
                        // matters: a session that cannot host the panel, or a
                        // panel left over from an earlier version that would
                        // not make way.
                        Text {
                            Layout.fillWidth: true
                            text: OverlayControl.notice !== "" ? OverlayControl.notice : !OverlayControl.usable ? OverlayControl.unsupportedReason : ""
                            color: ui.warning
                            wrapMode: Text.WordWrap
                            font.family: ui.fontFamily
                            font.pixelSize: ui.fontSizeSmall
                        }
                    }
                }
            }
        }
    }
}
