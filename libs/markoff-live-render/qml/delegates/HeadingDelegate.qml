// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

/// Editable heading delegate. R3 surfaces (selection highlight, blockText)
/// retained; R4 adds LiveEditBinding so contentsChange routes to the CRDT.
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
        leftPadding: 8; rightPadding: 8
        topPadding: 6; bottomPadding: 2
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        font.pixelSize: {
            switch (model.headingLevel) {
                case 1: return 28; case 2: return 24; case 3: return 20
                case 4: return 18; case 5: return 16; default: return 14
            }
        }
        font.bold: model.headingLevel <= 3
        color: palette.text
        selectByMouse: true
        persistentSelection: true

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (!handler) { event.accepted = false; return }
            const k = event.key
            const mods = event.modifiers
            const isLevelChange = (mods & Qt.ControlModifier) && (mods & Qt.ShiftModifier)
                                  && k >= Qt.Key_0 && k <= Qt.Key_6
            if (k !== Qt.Key_Return && k !== Qt.Key_Enter
                    && k !== Qt.Key_Backspace && k !== Qt.Key_Delete
                    && !isLevelChange) {
                return
            }
            const handled = handler.tryHandle(k, mods, root.modelIndex,
                edit.cursorPosition, edit.selectionStart === edit.selectionEnd, model.text)
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

        onTextChanged: {
            const cs = root.liveBinding ? root.liveBinding.cursorState : null
            if (cs && cs.focusedAnchorRow === root.modelIndex && cs.focusedQtPos >= 0)
                cursorPosition = cs.focusedQtPos
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

    function positionAt(x, y) { return edit.positionAt(x - edit.leftPadding, y - edit.topPadding) }

    function focusEditAt(qtPos) {
        edit.forceActiveFocus()
        if (qtPos >= 0 && qtPos <= edit.length)
            edit.cursorPosition = qtPos
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (!cs) return
        if (cs.focusedAnchorRow === root.modelIndex) {
            Qt.callLater(function() { focusEditAt(cs.focusedQtPos >= 0 ? cs.focusedQtPos : 0) })
        }
    }
}
