// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    height: 16

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: "#888"
    }
}
