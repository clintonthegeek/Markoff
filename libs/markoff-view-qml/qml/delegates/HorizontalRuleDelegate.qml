// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    id: root

    property int blockIndex: -1

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    height: 16

    /// HR has no text content; positionAt returns 0 (start-of-block).
    function positionAt(x, y) { return 0 }
    readonly property int textLength: 0

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: "#888"
    }
}
