// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: isEditing ? editArea.implicitHeight : renderArea.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text
    property var blockAnchor: undefined  // captured at Component.onCompleted; stays valid through onDestruction

    readonly property var liveBinding: ListView.view ? ListView.view.binding : null

    readonly property bool isSelected:
        liveBinding !== null && liveBinding.cursorState !== null
        && liveBinding.cursorState.cursorKind === "BlockSelected"
        && liveBinding.cursorState.focusedAnchorRow === root.modelIndex

    readonly property bool isEditing:
        liveBinding !== null && liveBinding.cursorState !== null
        && liveBinding.cursorState.cursorKind === "BlockInternalEdit"
        && liveBinding.cursorState.focusedAnchorRow === root.modelIndex

    readonly property bool displayMode: {
        const a = model.blockAttrs
        return a ? (a["displayMode"] || false) : false
    }

    // ---- Render mode (read-only view) ----
    Column {
        id: renderArea
        width: parent.width
        visible: !root.isEditing
        topPadding: 8; bottomPadding: 8

        Text {
            width: parent.width - 16
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.blockText
            // Math uses Slot.Math (id 12); falls through Slot→Monospace role.
            font.family: (root.liveBinding && root.liveBinding.theme)
                           ? root.liveBinding.themeFamilyFor(12) : "monospace"
            font.pixelSize: ((root.liveBinding && root.liveBinding.theme)
                              ? root.liveBinding.themePixelSizeFor(12) : 13)
                            * (root.liveBinding ? root.liveBinding.fontScale : 1.0)
            color: (root.liveBinding && root.liveBinding.theme)
                   ? root.liveBinding.themeColorFor(Theme.Quote)
                   : "#666666"
            wrapMode: Text.Wrap
        }
    }

    // ---- Edit mode ----
    Column {
        id: editArea
        visible: root.isEditing
        width: parent.width
        topPadding: 8; bottomPadding: 8; spacing: 4

        LiveEditBinding {
            id: editBinding
            binding: root.liveBinding
            modelIndex: root.modelIndex
            textDocument: latexEdit.textDocument
            composing: latexEdit.inputMethodComposing
            text: model.text
        }

        TextEdit {
            id: latexEdit
            width: parent.width - 16
            anchors.horizontalCenter: parent.horizontalCenter
            height: Math.max(60, implicitHeight)
            readOnly: false
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            // Math uses Slot.Math (id 12); falls through Slot→Monospace role.
            font.family: (root.liveBinding && root.liveBinding.theme)
                           ? root.liveBinding.themeFamilyFor(12) : "monospace"
            font.pixelSize: ((root.liveBinding && root.liveBinding.theme)
                              ? root.liveBinding.themePixelSizeFor(12) : 13)
                            * (root.liveBinding ? root.liveBinding.fontScale : 1.0)
            color: (root.liveBinding && root.liveBinding.theme)
                   ? root.liveBinding.themeColorFor(Theme.TextDefault)
                   : "#222222"
            selectionColor: (root.liveBinding && root.liveBinding.theme)
                            ? root.liveBinding.themeColorFor(Theme.SelectionBackground)
                            : "#b0d0ff"
            selectedTextColor: (root.liveBinding && root.liveBinding.theme)
                               ? root.liveBinding.themeColorFor(Theme.EditorBackground)
                               : "#ffffff"

            Keys.priority: Keys.BeforeItem
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape) {
                    root.exitEditMode()
                    event.accepted = true
                }
            }
        }
    }

    // Focus / edit ring
    Rectangle {
        visible: root.isSelected || root.isEditing
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

    function enterEditMode() {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockInternalEdit",
                              block: model.blockAnchor, mode: "editing-latex" })
        Qt.callLater(function() { latexEdit.forceActiveFocus() })
    }

    function exitEditMode() {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (root.isSelected && event.key === Qt.Key_F2) {
            root.enterEditMode(); event.accepted = true; return
        }
        if (root.isSelected
                && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)) {
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
        onDoubleClicked: if (root.isSelected) root.enterEditMode()
    }

    function takeFocus(qtPos: int) {
        root.forceActiveFocus()
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) cs.request({ variant: "BlockSelected", block: model.blockAnchor })
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        blockAnchor = model.blockAnchor
        if (cs) cs.delegateAvailable(blockAnchor, model.kind, root)
    }

    Component.onDestruction: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && blockAnchor !== undefined) cs.delegateGoingAway(blockAnchor, root)
    }
}
