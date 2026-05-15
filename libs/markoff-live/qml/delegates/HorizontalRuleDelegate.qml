// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

BlockOnlyDelegateBase {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: 20

    // Rule line
    Rectangle {
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        height: 2
        color: root.isSelected ? palette.highlight : palette.mid
        radius: 1
    }

    // Selection outline
    Rectangle {
        visible: root.isSelected
        anchors.fill: parent
        anchors.margins: -2
        border.color: palette.highlight
        border.width: 2
        color: "transparent"
        radius: 3
    }

    function positionAt(x, y) { return -1 }
}
