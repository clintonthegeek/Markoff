// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: imageArea.implicitHeight + 8

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding: ListView.view ? ListView.view.binding : null
    readonly property var cursorState: liveBinding ? liveBinding.cursorState : null

    readonly property bool isSelected:
        cursorState !== null
        && cursorState.cursorKind === "BlockSelected"
        && cursorState.focusedAnchorRow === root.modelIndex

    readonly property bool isAltEditing:
        cursorState !== null
        && cursorState.cursorKind === "BlockInternalEdit"
        && cursorState.focusedAnchorRow === root.modelIndex

    readonly property string imgSrc: {
        const a = model.blockAttrs
        return a ? (a["src"] || "") : ""
    }
    readonly property string imgAlt: {
        const a = model.blockAttrs
        return a ? (a["alt"] || "") : ""
    }

    Item {
        id: imageArea
        width: parent.width
        implicitHeight: imgDisplay.implicitHeight + altRow.implicitHeight + 8

        Image {
            id: imgDisplay
            source: root.imgSrc
            width: parent.width - 16
            anchors.horizontalCenter: parent.horizontalCenter
            fillMode: Image.PreserveAspectFit
            visible: !root.isAltEditing
        }

        Rectangle {
            visible: imgDisplay.status !== Image.Ready && !root.isAltEditing
            width: parent.width - 16
            height: 80
            anchors.horizontalCenter: parent.horizontalCenter
            color: palette.alternateBase
            border.color: palette.mid
            Text {
                anchors.centerIn: parent
                text: root.imgSrc === "" ? "[image: no src]" : "[image: " + root.imgSrc + "]"
                color: palette.mid
            }
        }

        Text {
            id: altRow
            anchors { top: imgDisplay.bottom; left: parent.left; right: parent.right }
            anchors.margins: 8
            text: root.imgAlt
            color: palette.mid
            font.italic: true
            font.pixelSize: ((root.liveBinding && root.liveBinding.theme)
                              ? root.liveBinding.themePixelSizeFor(0)  // TextDefault
                              : 14) * 0.85
                            * (root.liveBinding ? root.liveBinding.fontScale : 1.0)
            visible: !root.isAltEditing && root.imgAlt !== ""
        }

        TextField {
            id: altInput
            visible: root.isAltEditing
            anchors { top: imgDisplay.bottom; left: parent.left; right: parent.right }
            anchors.margins: 8
            text: root.imgAlt
            font.pixelSize: ((root.liveBinding && root.liveBinding.theme)
                              ? root.liveBinding.themePixelSizeFor(0)  // TextDefault
                              : 14) * 0.85
                            * (root.liveBinding ? root.liveBinding.fontScale : 1.0)
            placeholderText: "Alt text…"
            background: null

            Keys.onReturnPressed: {
                const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
                if (handler) handler.changeImageAlt(model.blockAnchor, text)
                root.exitAltEdit()
            }
            Keys.onEscapePressed: root.exitAltEdit()
        }
    }

    Rectangle {
        visible: root.isSelected || root.isAltEditing
        anchors.fill: parent
        anchors.margins: -2
        border.color: palette.highlight
        border.width: 2
        color: "transparent"
        radius: 3
    }

    function positionAt(x, y) { return -1 }

    function takeFocus(qtPos) {
        root.forceActiveFocus()
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    function enterAltEdit() {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockInternalEdit",
                              block: model.blockAnchor, mode: "alt-edit" })
        altInput.forceActiveFocus()
    }

    function exitAltEdit() {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (root.isSelected && (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)) {
            root.enterAltEdit()
            event.accepted = true
            return
        }
        if (root.isSelected && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)) {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (handler) {
                event.accepted = handler.tryHandle(event.key, event.modifiers,
                    root.modelIndex, -1, true, model.text)
            }
            return
        }
        event.accepted = false
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: if (root.isSelected) root.enterAltEdit()
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.delegateAvailable(model.blockAnchor, model.kind, root)
    }

    Component.onDestruction: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.delegateGoingAway(model.blockAnchor)
    }
}
