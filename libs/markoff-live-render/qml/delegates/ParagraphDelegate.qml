// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

/// Editable paragraph delegate. R3 surfaces (selection highlight, blockText)
/// retained; R4 adds LiveEditBinding so contentsChange routes to the CRDT.
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text
    property bool isHole: model.isHole === true

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
        text: root.isHole ? (model.bufferText || "") : model.text
        holeId: model.holeId || 0
    }

    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: 8; rightPadding: 8
        topPadding: 4; bottomPadding: 4
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        font.pixelSize: 14
        color: palette.text
        selectByMouse: true
        persistentSelection: true

        // Forward structural keys (Return / Enter / Esc / Backspace / Delete)
        // to LiveStructuralKeyHandler. R5 + R5.5 logic dispatches based on
        // row kind (paragraph / heading / code-block) and hole-vs-inner-row.
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (!handler) { event.accepted = false; return }

            // Only forward keys whose dispatch matters: structural keys + abandons.
            const k = event.key
            if (k !== Qt.Key_Return && k !== Qt.Key_Enter
                && k !== Qt.Key_Escape && k !== Qt.Key_Backspace && k !== Qt.Key_Delete) {
                return  // let TextEdit handle normally
            }

            const handled = handler.tryHandle(
                k,
                event.modifiers,
                root.modelIndex,
                edit.cursorPosition,
                edit.selectionStart === edit.selectionEnd,
                root.isHole ? (model.bufferText || "") : model.text
            )
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
    }

    function positionAt(x, y) { return edit.positionAt(x - edit.leftPadding, y - edit.topPadding) }

    /// Called by LiveView's MouseArea after a click resolves. Routes
    /// keyboard focus into the TextEdit so the user can type. R4: the
    /// LiveView MouseArea has preventStealing:true and consumes clicks
    /// before they reach the TextEdit, so we must put focus there
    /// programmatically.
    function focusEditAt(qtPos) {
        console.log("[dogfood] ParaDelegate.focusEditAt modelIndex=" + root.modelIndex
            + " isHole=" + root.isHole + " holeId=" + (model.holeId || 0)
            + " qtPos=" + qtPos + " editLen=" + edit.length)
        edit.forceActiveFocus()
        if (qtPos >= 0 && qtPos <= edit.length)
            edit.cursorPosition = qtPos
    }

    /// When a new delegate appears, check if the cursor state is already
    /// pointing at this row (set synchronously by the structural-key handler
    /// during Enter / Backspace-merge / Delete-merge / EOB-Enter). If so,
    /// focus immediately — the delegate just became live and the LiveView's
    /// onCursorChanged handler couldn't reach us via itemAtIndex earlier.
    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (!cs) {
            console.log("[dogfood] ParaDelegate.onCompleted modelIndex=" + root.modelIndex
                + " isHole=" + root.isHole + " holeId=" + (model.holeId || 0)
                + " NO cursorState")
            return
        }
        const proxy = root.liveBinding ? root.liveBinding.proxyModel : null

        const focusedProxy = (proxy && cs.focusedAnchorRow >= 0)
                                ? proxy.proxyRowForInner(cs.focusedAnchorRow) : -1
        const match = (focusedProxy === root.modelIndex)
        console.log("[dogfood] ParaDelegate.onCompleted modelIndex=" + root.modelIndex
            + " isHole=false focusedAnchorRow=" + cs.focusedAnchorRow
            + " focusedProxy=" + focusedProxy
            + " match=" + match)
        if (match) {
            Qt.callLater(function() { focusEditAt(cs.focusedQtPos >= 0 ? cs.focusedQtPos : 0) })
        }
    }
}
