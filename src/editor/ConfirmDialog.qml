// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A question with two answers, in the same square and flat manner as the rest
// of the editor and drawn from the same tokens.
//
// Kept separate from AboutDialog even though both are modal popups: that one
// states facts and has a single way out, this one asks and its answer decides
// what happens next.
Popup {
    id: root

    required property var ui
    property string question: ""
    property string explanation: ""
    property string confirmText: qsTr("OK")
    property string cancelText: qsTr("Cancel")

    signal confirmed

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    // Escape closes without confirming: the safe answer is the one that
    // changes nothing.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 0
    implicitWidth: root.ui.dialogWidth

    background: Rectangle {
        color: root.ui.surface
        border.color: root.ui.line
        border.width: root.ui.lineWidth
        radius: root.ui.radius
    }
    Overlay.modal: Rectangle {
        color: root.ui.scrim
    }

    contentItem: ColumnLayout {
        spacing: root.ui.spacingRow

        Text {
            Layout.fillWidth: true
            Layout.margins: root.ui.paddingWindow
            Layout.bottomMargin: 0
            text: root.question
            color: root.ui.text
            wrapMode: Text.WordWrap
            font.family: root.ui.fontFamily
            font.pixelSize: root.ui.fontSize
            font.weight: Font.DemiBold
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: root.ui.paddingWindow
            Layout.rightMargin: root.ui.paddingWindow
            visible: root.explanation !== ""
            text: root.explanation
            color: root.ui.textMuted
            wrapMode: Text.WordWrap
            font.family: root.ui.fontFamily
            font.pixelSize: root.ui.fontSizeSmall
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: root.ui.paddingWindow
            Layout.topMargin: root.ui.spacingRow
            spacing: root.ui.spacingRow

            Item {
                Layout.fillWidth: true
            }

            DialogButton {
                id: cancelButton
                text: root.cancelText
                onClicked: root.close()
            }
            DialogButton {
                text: root.confirmText
                primary: true
                onClicked: {
                    root.close();
                    root.confirmed();
                }
            }
        }
    }

    component DialogButton: Button {
        id: btn
        property bool primary: false
        font.family: root.ui.fontFamily
        font.pixelSize: root.ui.fontSize
        background: Rectangle {
            implicitWidth: root.ui.buttonWidth
            implicitHeight: root.ui.buttonHeight
            color: btn.down ? root.ui.surfaceHover : btn.primary ? root.ui.accentDim : root.ui.background
            border.color: btn.activeFocus || btn.primary ? root.ui.accent : btn.down || btn.hovered ? root.ui.lineHover : root.ui.line
            border.width: root.ui.lineWidth
            radius: root.ui.radius
        }
        contentItem: Text {
            text: btn.text
            color: root.ui.text
            font: btn.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // The cancelling button takes the focus, so the highlighted answer is the
    // one that changes nothing.
    onOpened: cancelButton.forceActiveFocus()
}
