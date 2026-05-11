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
        readonly property var theme: root.liveBinding ? root.liveBinding.theme : null
        readonly property real fontScale: root.liveBinding ? root.liveBinding.fontScale : 1.0
        readonly property int  baseSlot: 0  // Theme.Slot.TextDefault

        font.pixelSize: theme ? root.liveBinding.themePixelSizeFor(baseSlot) * fontScale : 14 * fontScale
        font.family:    theme ? root.liveBinding.themeFamilyFor(baseSlot) : ""
        font.bold:      theme ? root.liveBinding.themeIsBold(baseSlot) : false
        font.italic:    theme ? root.liveBinding.themeIsItalic(baseSlot) : false
        color: palette.text
        // Option B: TextEdit is a renderer + cursor + IME only. All selection
        // is owned by LiveSelectionView and rendered via select()/deselect()
        // from applySelection(). Native mouse selection is gone.
        selectByMouse: false
        persistentSelection: true

        // One-way sync: TextEdit's cursorPosition is the live caret;
        // mirror it into LiveCursorState so m_cursor stays canonical
        // through within-block typing and within-block arrow nav.
        // Guard: when LiveEditBinding is mid-setPlainText AND m_cursor
        // already targets this row, the cursor move is a side effect of
        // the document reset (it always lands at end-of-text), not a
        // user navigation. Suppress the echo AND restore the canonical
        // position so the QTextEdit's visible cursor matches m_cursor.
        // Initial load (m_cursor is NoCursor) bypasses this guard so
        // the echo still seeds focused state.
        onCursorPositionChanged: {
            const cs = root.liveBinding ? root.liveBinding.cursorState : null
            if (model.blockAnchor !== undefined && cs) {
                if (editBinding.isApplyingTextUpdate()
                        && cs.focusedAnchorRow === root.modelIndex) {
                    if (cs.focusedQtPos >= 0
                            && cs.focusedQtPos <= edit.length
                            && edit.cursorPosition !== cs.focusedQtPos) {
                        edit.cursorPosition = cs.focusedQtPos
                    }
                    return
                }
                cs.syncFromTextEdit(model.blockAnchor, edit.cursorPosition)
            }
        }

        InlineHighlighterAttached {
            target: edit.textDocument
            spans: model.inlineSpans
            theme: root.liveBinding ? root.liveBinding.theme : null
            fontScale: root.liveBinding ? root.liveBinding.fontScale : 1.0
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
                        || k === Qt.Key_Home || k === Qt.Key_End
                        || k === Qt.Key_PageUp || k === Qt.Key_PageDown)

            if (isStructural) {
                const sh = root.liveBinding.structuralKeyHandler
                if (!sh) return
                event.accepted = sh.tryHandle(k, mods, root.modelIndex,
                                               edit.cursorPosition,
                                               edit.selectionStart === edit.selectionEnd,
                                               model.text)
                return
            }
            if (isNav) {
                const nh = root.liveBinding.navigationController
                if (!nh) return
                event.accepted = (nh.tryHandle(k, mods, root.modelIndex,
                                                edit.cursorPosition,
                                                edit, model.text) === 1)
                return
            }
        }

        // Render the selection range and place the caret at the active end.
        // Direction-preserving: subsequent Shift+arrow extends from the same
        // anchor instead of flipping to whichever side `select(min, max)` would
        // have parked the cursor on.
        function applySelection() {
            const sv = root.selectionView
            if (!sv) { deselect(); return }
            const r = sv.rangeForBlock(model.index)
            if (!r || r.x < 0) { deselect(); return }
            const blockLen = length
            const start = Math.min(r.x, blockLen)
            const end   = Math.min(r.y, blockLen)
            if (start === end) {
                cursorPosition = start
                return
            }
            const myIdx = model.index
            const cursorAtEnd = (myIdx === sv.activeBlock())
                ? (sv.activeQtPos() === end)
                : (sv.activeBlock() > myIdx)
            const cursorPos = cursorAtEnd ? end   : start
            const otherPos  = cursorAtEnd ? start : end
            cursorPosition = otherPos
            moveCursorSelection(cursorPos, TextEdit.SelectCharacters)
        }

        Connections {
            target: root.selectionView
            function onSelectionChanged() { edit.applySelection() }
        }
    }

    function positionAt(x, y) { return edit.positionAt(x - edit.leftPadding, y - edit.topPadding) }

    function takeFocus(qtPos) {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) {
            const hint = cs.pendingVisualLineHint
            const desiredX = cs.desiredVisualX
            if (hint !== 0 && desiredX >= 0) {
                const lineH = edit.font.pixelSize
                const targetY = (hint === 1) ? lineH * 0.5 : edit.contentHeight - lineH * 0.5
                edit.cursorPosition = edit.positionAt(desiredX - edit.leftPadding, targetY)
                edit.forceActiveFocus()
                return
            }
        }
        edit.cursorPosition = Math.min(Math.max(qtPos, 0), edit.length)
        edit.forceActiveFocus()
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
