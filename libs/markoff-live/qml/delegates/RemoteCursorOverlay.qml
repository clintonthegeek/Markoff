// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Rectangle {
    id: overlay
    required property color cursorColor
    required property string cursorLabel
    width: 2
    height: 16
    color: cursorColor
    opacity: 0.8

    Rectangle {
        anchors { left: parent.right; bottom: parent.top }
        color: parent.color
        radius: 2
        height: labelText.implicitHeight + 2
        width: labelText.implicitWidth + 4
        Text {
            id: labelText
            anchors.centerIn: parent
            text: overlay.cursorLabel
            color: "#ffffff"
            font.pixelSize: 9
        }
    }
}
