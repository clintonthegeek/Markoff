// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import org.markoff.live 1.0

BlockOnlyDelegateBase {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: 20

    // Rule line
    Rectangle {
        objectName: "hrRule"
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        height: 2
        color: root.isSelected
               ? ((root.liveBinding && root.liveBinding.theme)
                  ? root.liveBinding.themeColorFor(Theme.SelectionBackground)
                  : "#b0d0ff")
               : ((root.liveBinding && root.liveBinding.theme)
                  ? root.liveBinding.themeColorFor(Theme.Quote)
                  : "#666666")
        radius: 1
    }

    // Selection outline
    Rectangle {
        visible: root.isSelected
        anchors.fill: parent
        anchors.margins: -2
        border.color: (root.liveBinding && root.liveBinding.theme)
                      ? root.liveBinding.themeColorFor(Theme.SelectionBackground)
                      : "#b0d0ff"
        border.width: 2
        color: "transparent"
        radius: 3
    }

    function positionAt(x, y) { return -1 }
}
