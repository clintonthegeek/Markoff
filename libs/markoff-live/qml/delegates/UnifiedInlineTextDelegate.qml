// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

/// Unified delegate for the four text-inline block kinds: paragraph,
/// heading, blockquote, list-item. The `TextEdit` is persistent across
/// within-class kind transitions; ornaments (heading sizing,
/// blockquote bar, list-item marker) bind conditionally on
/// `model.kind`. Spec §5.1 of
/// `docs/specs/2026-05-15-tier-3-kind-transition-delegate-architecture-design.md`.
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string kind: model.kind
    readonly property int headingLevel: model.headingLevel || 0
    readonly property int indentLevel: model.indentLevel || 0
    readonly property string markerStyle: model.markerStyle || ""
    readonly property int markerNumber: model.markerNumber || 0
    readonly property bool checked: model.checked || false
    readonly property string blockText: model.text
    property var blockAnchor: undefined  // captured at Component.onCompleted

    readonly property var liveBinding:
        ListView.view ? ListView.view.binding : null
    readonly property var selectionView:
        liveBinding ? liveBinding.selectionView : null

    // True when this block is fully covered by a multi-block selection.
    // Drives marker-background paint (list-item only).
    property bool _fullySelected: false

    // -------- Theme slot dispatch --------
    readonly property int themeSlot: {
        if (kind === "heading") {
            // Slot enum: TextDefault=0, Heading1=1...Heading6=6.
            switch (headingLevel) {
                case 1: return 1
                case 2: return 2
                case 3: return 3
                case 4: return 4
                case 5: return 5
                default: return 6
            }
        }
        if (kind === "blockquote") {
            return 13  // Theme.Slot.Quote
        }
        // Paragraph and list-item fall to TextDefault.
        return 0
    }

    readonly property int markerSlot: 0  // TextDefault, used for list marker

    // -------- List-item geometry --------
    readonly property int indentPx: 8 + indentLevel * 24
    readonly property string markerText: {
        if (markerStyle === "dot")   return markerNumber + "."
        if (markerStyle === "paren") return markerNumber + ")"
        if (markerStyle === "minus") return "-"
        if (markerStyle === "plus")  return "+"
        if (markerStyle === "star")  return "*"
        if (markerStyle === "task")  return checked ? "[x]" : "[ ]"
        return "•"
    }

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
        text: model.text
    }

    // -------- Blockquote left bar (conditional) --------
    Rectangle {
        id: blockquoteBar
        visible: root.kind === "blockquote"
        anchors {
            left: parent.left
            top: parent.top
            bottom: parent.bottom
        }
        width: 3
        color: palette.highlight
        opacity: 0.6
    }

    // -------- List-item marker selection background (conditional) --------
    Rectangle {
        anchors.fill: markerLabel
        visible: root.kind === "list-item" && root._fullySelected
        color: palette.highlight
        z: -1
    }

    // -------- List-item marker (conditional) --------
    Text {
        id: markerLabel
        visible: root.kind === "list-item"
        anchors {
            left: parent.left
            top: parent.top
        }
        leftPadding: root.indentPx
        topPadding: 2
        text: root.markerText
        font.family: (root.liveBinding && root.liveBinding.theme)
                       ? root.liveBinding.themeFamilyFor(8 /* Slot.CodeBlock → Monospace */) : "monospace"
        font.pixelSize: (root.liveBinding && root.liveBinding.theme
                          ? root.liveBinding.themePixelSizeFor(0)  // TextDefault
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
            if (root.kind !== "list-item") { root._fullySelected = false; return }
            const sv = root.selectionView
            if (!sv) { root._fullySelected = false; return }
            const r = sv.rangeForBlock(root.modelIndex)
            if (!r || r.x < 0) { root._fullySelected = false; return }
            root._fullySelected = (r.x === 0 && r.y >= edit.length)
        }
    }

    // -------- Persistent TextEdit --------
    TextEdit {
        id: edit
        // Blockquote uses leftMargin on anchors (matching BlockquoteDelegate);
        // list-item and others use leftPadding on the TextEdit itself.
        anchors {
            fill: parent
            leftMargin: root.kind === "blockquote" ? 12 : 0
            rightMargin: root.kind === "blockquote" ? 8 : 0
        }
        leftPadding: root.kind === "list-item"
            ? markerLabel.implicitWidth + 12
            : (root.kind === "blockquote" ? 0 : 8)
        rightPadding: root.kind === "blockquote" ? 0 : 8
        topPadding: root.kind === "heading" ? 6 : (root.kind === "list-item" ? 2 : 4)
        bottomPadding: root.kind === "heading" ? 2 : (root.kind === "list-item" ? 2 : 4)
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        readonly property var theme: root.liveBinding ? root.liveBinding.theme : null
        readonly property real fontScale: root.liveBinding ? root.liveBinding.fontScale : 1.0

        font.pixelSize: theme
            ? root.liveBinding.themePixelSizeFor(root.themeSlot) * fontScale
            : 14 * fontScale
        font.family: theme ? root.liveBinding.themeFamilyFor(root.themeSlot) : ""
        font.bold: theme
            ? root.liveBinding.themeIsBold(root.themeSlot)
            : (root.kind === "heading" && root.headingLevel <= 3)
        font.italic: theme
            ? root.liveBinding.themeIsItalic(root.themeSlot)
            : (root.kind === "blockquote")
        color: (root.liveBinding && root.liveBinding.theme)
               ? root.liveBinding.themeColorFor(root.themeSlot)
               : "#222222"
        selectByMouse: false
        persistentSelection: true
        selectionColor: (root.liveBinding && root.liveBinding.theme)
                        ? root.liveBinding.themeColorFor(Theme.SelectionBackground)
                        : "#b0d0ff"
        selectedTextColor: (root.liveBinding && root.liveBinding.theme)
                           ? root.liveBinding.themeColorFor(Theme.EditorBackground)
                           : "#ffffff"

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

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            if (!root.liveBinding) { event.accepted = false; return }

            const k = event.key
            const mods = event.modifiers
            // Heading level-change: Ctrl+Shift+0..6 is structural for headings.
            const isLevelChange = root.kind === "heading"
                && (mods & Qt.ControlModifier) && (mods & Qt.ShiftModifier)
                && k >= Qt.Key_0 && k <= Qt.Key_6
            // Paragraph and heading include Escape; list-item includes Tab.
            const isStructural = (k === Qt.Key_Return || k === Qt.Key_Enter
                               || k === Qt.Key_Backspace || k === Qt.Key_Delete
                               || isLevelChange
                               || (root.kind === "paragraph" && k === Qt.Key_Escape)
                               || (root.kind === "list-item" && k === Qt.Key_Tab))
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
    }

    // positionAt: blockquote uses leftMargin (not leftPadding) so offset is 12.
    function positionAt(x, y) {
        const xOff = root.kind === "blockquote" ? 12 : edit.leftPadding
        return edit.positionAt(x - xOff, y - edit.topPadding)
    }

    function takeFocus(qtPos: int) {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) {
            const hint = cs.pendingVisualLineHint
            const desiredX = cs.desiredVisualX
            if (hint !== 0 && desiredX >= 0) {
                const lineH = edit.font.pixelSize
                const targetY = (hint === 1) ? lineH * 0.5 : edit.contentHeight - lineH * 0.5
                const xOff = root.kind === "blockquote" ? 12 : edit.leftPadding
                edit.cursorPosition = edit.positionAt(desiredX - xOff, targetY)
                edit.forceActiveFocus()
                return
            }
        }
        edit.cursorPosition = Math.min(Math.max(qtPos, 0), edit.length)
        edit.forceActiveFocus()
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
