// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

/// Editable list-item delegate. Stores full markdown text including the
/// "- " marker prefix in the CRDT buffer (source-faithful). Structural keys
/// (Enter / Backspace / Delete / Tab) are forwarded to LiveStructuralKeyHandler.
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding:
        ListView.view ? ListView.view.binding : null
    readonly property var selectionView:
        liveBinding ? liveBinding.selectionView : null

    readonly property int indentLevel: {
        const a = model.blockAttrs
        return (a && a["indentLevel"]) ? a["indentLevel"] : 0
    }

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
        text: model.text
    }

    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: 8 + root.indentLevel * 16
        rightPadding: 8
        topPadding: 2
        bottomPadding: 2
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        font.pixelSize: 14
        color: palette.text
        selectByMouse: true
        persistentSelection: true

        // Forward structural keys to LiveStructuralKeyHandler.
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (!handler) { event.accepted = false; return }
            const k = event.key
            if (k !== Qt.Key_Return && k !== Qt.Key_Enter
                    && k !== Qt.Key_Backspace && k !== Qt.Key_Delete
                    && k !== Qt.Key_Tab) {
                return
            }
            const handled = handler.tryHandle(k, event.modifiers, root.modelIndex,
                edit.cursorPosition, edit.selectionStart === edit.selectionEnd,
                model.text)
            event.accepted = handled
        }

        function applySelection() {
            const sv = root.selectionView
            if (!sv) { deselect(); return }
            const r = sv.rangeForBlock(model.index)
            if (!r || r.x < 0) { deselect(); return }
            select(r.x, Math.min(r.y, length))
        }

        Connections {
            target: root.selectionView
            function onSelectionChanged() { edit.applySelection() }
        }

        Connections {
            target: root.liveBinding ? root.liveBinding.cursorState : null
            function onCursorChanged() {
                const cs = root.liveBinding ? root.liveBinding.cursorState : null
                if (cs && cs.focusedAnchorRow === root.modelIndex && cs.focusedQtPos >= 0)
                    edit.cursorPosition = cs.focusedQtPos
            }
        }
    }

    function positionAt(x, y) {
        return edit.positionAt(x - edit.leftPadding, y - edit.topPadding)
    }

    function focusEditAt(qtPos) {
        edit.forceActiveFocus()
        if (qtPos >= 0 && qtPos <= edit.length)
            edit.cursorPosition = qtPos
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && cs.focusedAnchorRow === root.modelIndex)
            Qt.callLater(function() { focusEditAt(cs.focusedQtPos >= 0 ? cs.focusedQtPos : 0) })
    }
}
