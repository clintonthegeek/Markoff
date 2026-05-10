// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

/// Per-item ListItem delegate. Marker is rendered as a non-editable
/// label (or task-list checkbox) to the left of the TextEdit, populated
/// from model.markerStyle / model.markerNumber / model.checked. Indent
/// is rendered as left padding (3 spaces of width per indent level).
/// Buffer holds content only; the marker is reconstructed at serialize.
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

    // True when this block is fully covered by a multi-block selection
    // (range starts at 0 and reaches the end of the block's text). Used to
    // paint the list marker as part of the selection so the bullet/number
    // visibly belongs to the selected range. Recomputed via Connections
    // on selectionView.selectionChanged because LiveSelectionView's range
    // is read through a Q_INVOKABLE method, not a notifying Q_PROPERTY.
    property bool _fullySelected: false

    readonly property int indentLevel: model.indentLevel || 0
    readonly property string markerStyle: model.markerStyle || ""
    readonly property int markerNumber: model.markerNumber || 0
    readonly property bool checked: model.checked || false

    readonly property string markerText: {
        if (markerStyle === "dot")   return markerNumber + "."
        if (markerStyle === "paren") return markerNumber + ")"
        if (markerStyle === "minus") return "-"
        if (markerStyle === "plus")  return "+"
        if (markerStyle === "star")  return "*"
        if (markerStyle === "task")  return checked ? "[x]" : "[ ]"
        return "•"
    }

    readonly property int indentPx: 8 + indentLevel * 24

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
        text: model.text
    }

    // Selection-highlight backdrop for the marker. Painted only when the
    // whole block is in a multi-block selection (D4 of the architectural
    // pass): markdown markers are rendered separately from `model.text` so
    // the TextEdit's native `select(...)` paint never touches them.
    Rectangle {
        anchors.fill: markerLabel
        color: palette.highlight
        visible: root._fullySelected
        z: -1
    }

    Text {
        id: markerLabel
        anchors {
            left: parent.left
            top: parent.top
        }
        leftPadding: root.indentPx
        topPadding: 2
        text: root.markerText
        font.family: (root.liveBinding && root.liveBinding.theme)
                       ? root.liveBinding.theme.familyFor(8 /* Slot.CodeBlock → Monospace */) : "monospace"
        font.pixelSize: (root.liveBinding && root.liveBinding.theme
                          ? root.liveBinding.theme.pixelSizeFor(0)  // TextDefault
                          : 14)
                        * (root.liveBinding ? root.liveBinding.fontScale : 1.0)
        color: root._fullySelected ? palette.highlightedText : palette.text

        MouseArea {
            visible: root.markerStyle === "task"
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (!root.liveBinding || !root.liveBinding.document) return
                root.liveBinding.document.toggleListItemChecked(model.blockAnchor)
            }
        }
    }

    Connections {
        target: root.selectionView
        function onSelectionChanged() {
            const sv = root.selectionView
            if (!sv) { root._fullySelected = false; return }
            const r = sv.rangeForBlock(root.modelIndex)
            if (!r || r.x < 0) { root._fullySelected = false; return }
            root._fullySelected = (r.x === 0 && r.y >= edit.length)
        }
    }

    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: markerLabel.implicitWidth + 12
        rightPadding: 8
        topPadding: 2
        bottomPadding: 2
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        readonly property var theme: root.liveBinding ? root.liveBinding.theme : null
        readonly property real fontScale: root.liveBinding ? root.liveBinding.fontScale : 1.0
        readonly property int  baseSlot: 0  // Theme.Slot.TextDefault

        font.pixelSize: theme ? theme.pixelSizeFor(baseSlot) * fontScale : 14 * fontScale
        font.family:    theme ? theme.familyFor(baseSlot) : ""
        font.bold:      theme ? theme.isBold(baseSlot) : false
        font.italic:    theme ? theme.isItalic(baseSlot) : false
        color: palette.text
        selectByMouse: false
        persistentSelection: true

        onCursorPositionChanged: {
            const cs = root.liveBinding ? root.liveBinding.cursorState : null
            if (model.blockAnchor !== undefined && cs)
                cs.syncFromTextEdit(model.blockAnchor, edit.cursorPosition)
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

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            if (!root.liveBinding) { event.accepted = false; return }

            const k = event.key
            const mods = event.modifiers
            const isStructural = (k === Qt.Key_Return || k === Qt.Key_Enter
                               || k === Qt.Key_Backspace || k === Qt.Key_Delete
                               || k === Qt.Key_Tab)
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

        Connections {
            target: root.liveBinding ? root.liveBinding.cursorState : null
            function onCursorChanged() {
                const cs = root.liveBinding ? root.liveBinding.cursorState : null
                if (!cs || cs.focusedAnchorRow !== root.modelIndex || cs.focusedQtPos < 0)
                    return
                if (cs.pendingVisualLineHint !== 0 && cs.desiredVisualX >= 0) {
                    root.focusEditAt(cs.focusedQtPos)
                } else {
                    edit.cursorPosition = cs.focusedQtPos
                }
            }
        }
    }

    function positionAt(x, y) {
        return edit.positionAt(x - edit.leftPadding, y - edit.topPadding)
    }

    function focusEditAt(qtPos) {
        edit.forceActiveFocus()
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) {
            const hint = cs.pendingVisualLineHint
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

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && cs.focusedAnchorRow === root.modelIndex)
            Qt.callLater(function() { focusEditAt(cs.focusedQtPos >= 0 ? cs.focusedQtPos : 0) })
    }
}
