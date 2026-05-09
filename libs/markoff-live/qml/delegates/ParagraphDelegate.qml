// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

/// Editable paragraph delegate. R3 surfaces (selection highlight, blockText)
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
        topPadding: 4; bottomPadding: 4
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        font.pixelSize: 14
        color: palette.text
        selectByMouse: true
        persistentSelection: true

        InlineHighlighterAttached {
            target: edit.textDocument
            spans: model.inlineSpans
            theme: root.liveBinding ? root.liveBinding.theme : null
            caretPosition: edit.activeFocus ? edit.cursorPosition : -1
            selectionStart: (edit.activeFocus && edit.selectionStart !== edit.selectionEnd)
                            ? edit.selectionStart : -1
            selectionEnd: (edit.activeFocus && edit.selectionStart !== edit.selectionEnd)
                          ? edit.selectionEnd : -1
        }

        // Forward structural keys (Return / Enter / Esc / Backspace / Delete)
        // to LiveStructuralKeyHandler. Navigation keys (Up/Down/Left/Right/etc.)
        // are forwarded to LiveNavigationController. R5 + R5.5 logic dispatches
        // based on row kind (paragraph / heading / code-block).
        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            if (!root.liveBinding) { event.accepted = false; return }

            const k = event.key
            const mods = event.modifiers
            const isStructural = (k === Qt.Key_Return || k === Qt.Key_Enter
                               || k === Qt.Key_Escape || k === Qt.Key_Backspace
                               || k === Qt.Key_Delete)
            const isNav = (k === Qt.Key_Up || k === Qt.Key_Down
                        || k === Qt.Key_Left || k === Qt.Key_Right
                        || k === Qt.Key_PageUp || k === Qt.Key_PageDown)
            const isCtrlHomeEnd = ((k === Qt.Key_Home || k === Qt.Key_End)
                                   && (mods & Qt.ControlModifier))

            if (isStructural) {
                const sh = root.liveBinding.structuralKeyHandler
                if (!sh) return
                event.accepted = sh.tryHandle(k, mods, root.modelIndex,
                                               edit.cursorPosition,
                                               edit.selectionStart === edit.selectionEnd,
                                               model.text)
                return
            }
            if (isNav || isCtrlHomeEnd) {
                const nh = root.liveBinding.navigationController
                if (!nh) return
                event.accepted = (nh.tryHandle(k, mods, root.modelIndex,
                                                edit.cursorPosition,
                                                edit, model.text) === 1)
                return
            }
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
                if (!cs || cs.focusedAnchorRow !== root.modelIndex || cs.focusedQtPos < 0)
                    return
                // If a VisualLineHint is pending, delegate to focusEditAt so the
                // column-preservation path (positionAt) is used.
                if (cs.pendingVisualLineHint !== 0 && cs.desiredVisualX >= 0) {
                    root.focusEditAt(cs.focusedQtPos)
                } else {
                    edit.cursorPosition = cs.focusedQtPos
                }
            }
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
            + " qtPos=" + qtPos + " editLen=" + edit.length)
        edit.forceActiveFocus()
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) {
            const hint = cs.pendingVisualLineHint  // 0=None, 1=FirstLine, 2=LastLine
            const desiredX = cs.desiredVisualX
            if (hint !== 0 && desiredX >= 0) {
                const lineH = edit.font.pixelSize
                const targetY = (hint === 1)
                    ? lineH * 0.5
                    : edit.contentHeight - lineH * 0.5
                edit.cursorPosition = edit.positionAt(desiredX - edit.leftPadding, targetY)
                return
            }
        }
        if (qtPos >= 0 && qtPos <= edit.length)
            edit.cursorPosition = qtPos
    }

    /// When a new delegate appears, check if the cursor state is already
    /// pointing at this row (set synchronously by the structural-key handler
    /// during Enter / Backspace-merge / Delete-merge / marker insertion). If
    /// so, focus immediately — the delegate just became live and the
    /// LiveView's onCursorChanged handler couldn't reach us via itemAtIndex
    /// earlier.
    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (!cs) {
            console.log("[dogfood] ParaDelegate.onCompleted modelIndex=" + root.modelIndex
                + " NO cursorState")
            return
        }
        const match = (cs.focusedAnchorRow === root.modelIndex)
        console.log("[dogfood] ParaDelegate.onCompleted modelIndex=" + root.modelIndex
            + " focusedAnchorRow=" + cs.focusedAnchorRow
            + " match=" + match)
        if (match) {
            Qt.callLater(function() { focusEditAt(cs.focusedQtPos >= 0 ? cs.focusedQtPos : 0) })
        }
    }
}
