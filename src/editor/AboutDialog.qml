// SPDX-FileCopyrightText: 2026 Maik-0000FF
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// Identity, version and where to go from here.
//
// Everything shown comes from AppInfo, so the name, the version and the links
// are not typed in a second time here. Square and flat like the rest of the
// editor, and it wears the palette the overlay is set to.
Popup {
    id: root

    // The editor's own look, handed in so this file needs no palette of its
    // own.
    required property var ui

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
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

    // One link, opened in whatever the desktop uses for the web.
    component LinkRow: Text {
        id: link
        property string url: ""
        Layout.fillWidth: true
        color: mouse.containsMouse ? root.ui.text : root.ui.accent
        font.family: root.ui.fontFamily
        font.pixelSize: root.ui.fontSize

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: Qt.openUrlExternally(link.url)
        }
    }

    contentItem: ColumnLayout {
        spacing: root.ui.spacingRow

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: root.ui.paddingWindow
            Layout.bottomMargin: 0
            spacing: root.ui.spacingRow

            Image {
                source: AppInfo.iconSource
                sourceSize.width: root.ui.iconSourceSize
                sourceSize.height: root.ui.iconSourceSize
                Layout.preferredWidth: root.ui.iconSize
                Layout.preferredHeight: root.ui.iconSize
                fillMode: Image.PreserveAspectFit
                smooth: true
            }
            ColumnLayout {
                spacing: root.ui.spacingTight
                Text {
                    text: AppInfo.name
                    color: root.ui.text
                    font.family: root.ui.fontFamily
                    font.pixelSize: root.ui.fontSizeTitle
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("Version %1").arg(AppInfo.version)
                    color: root.ui.textMuted
                    font.family: root.ui.fontFamilyMono
                    font.pixelSize: root.ui.fontSizeSmall
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: root.ui.paddingWindow
            Layout.rightMargin: root.ui.paddingWindow
            text: AppInfo.description
            color: root.ui.textMuted
            wrapMode: Text.WordWrap
            font.family: root.ui.fontFamily
            font.pixelSize: root.ui.fontSize
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.ui.paddingWindow
            Layout.rightMargin: root.ui.paddingWindow
            spacing: root.ui.spacingLinks

            LinkRow {
                text: qsTr("View on GitHub")
                url: AppInfo.repositoryUrl
            }
            LinkRow {
                text: qsTr("Report an issue")
                url: AppInfo.issuesUrl
            }
            LinkRow {
                text: qsTr("License: %1").arg(AppInfo.licenseName)
                url: AppInfo.licenseUrl
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: root.ui.paddingWindow
            Layout.topMargin: root.ui.spacingRow

            Text {
                text: qsTr("Developed by %1").arg(AppInfo.developer)
                color: root.ui.textMuted
                font.family: root.ui.fontFamily
                font.pixelSize: root.ui.fontSizeSmall
            }
            Item {
                Layout.fillWidth: true
            }
            Button {
                id: closeButton
                text: qsTr("Close")
                font.family: root.ui.fontFamily
                font.pixelSize: root.ui.fontSize
                onClicked: root.close()
                background: Rectangle {
                    implicitWidth: root.ui.buttonWidth
                    implicitHeight: root.ui.buttonHeight
                    color: closeButton.down ? root.ui.surfaceHover : root.ui.background
                    border.color: closeButton.activeFocus ? root.ui.accent : closeButton.down || closeButton.hovered ? root.ui.lineHover : root.ui.line
                    border.width: root.ui.lineWidth
                    radius: root.ui.radius
                }
                contentItem: Text {
                    text: closeButton.text
                    color: root.ui.text
                    font: closeButton.font
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    onOpened: closeButton.forceActiveFocus()
}
