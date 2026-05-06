// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: 20

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding: ListView.view ? ListView.view.binding : null
    readonly property var cursorState: liveBinding ? liveBinding.cursorState : null

    Rectangle {
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        height: 2
        color: root.isSelected ? palette.highlight : palette.mid
        radius: 1
    }

    readonly property bool isSelected:
        cursorState !== null
        && cursorState.cursorKind === "BlockSelected"
        && cursorState.focusedAnchorRow === root.modelIndex

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

    function focusEditAt(qtPos) {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (!cs) return
        root.forceActiveFocus()
        cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (!root.isSelected) { event.accepted = false; return }
        const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
        if (!handler) { event.accepted = false; return }
        const k = event.key
        if (k !== Qt.Key_Delete && k !== Qt.Key_Backspace
                && k !== Qt.Key_Up && k !== Qt.Key_Down) {
            event.accepted = false; return
        }
        const handled = handler.tryHandle(k, event.modifiers, root.modelIndex,
            -1, true, model.text)
        event.accepted = handled
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && cs.focusedAnchorRow === root.modelIndex)
            Qt.callLater(function() { focusEditAt(-1) })
    }
}
