// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.kde.syntaxhighlighting
import org.markoff.live 1.0

Rectangle {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight + 16
    color: (liveBinding && liveBinding.theme)
           ? liveBinding.themeColorFor(Theme.CodeBlockBackground)
           : "#f4f4f4"
    radius: 4

    property int modelIndex: index
    readonly property string blockText: model.text
    property var blockAnchor: undefined  // captured at Component.onCompleted; stays valid through onDestruction

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
        anchors { left: parent.left; right: parent.right
                  top: parent.top; bottom: parent.bottom; margins: 8 }
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.NoWrap
        readonly property var theme: root.liveBinding ? root.liveBinding.theme : null
        readonly property real fontScale: root.liveBinding ? root.liveBinding.fontScale : 1.0
        readonly property int  baseSlot: 8  // Theme.Slot.CodeBlock

        font.pixelSize: theme ? root.liveBinding.themePixelSizeFor(baseSlot) * fontScale : 13 * fontScale
        font.family:    theme ? root.liveBinding.themeFamilyFor(baseSlot) : "monospace"
        font.bold:      theme ? root.liveBinding.themeIsBold(baseSlot) : false
        font.italic:    theme ? root.liveBinding.themeIsItalic(baseSlot) : false
        color: (root.liveBinding && root.liveBinding.theme)
               ? root.liveBinding.themeColorFor(Theme.CodeBlock)
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
            // See ParagraphDelegate for the rationale on this guard.
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

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            if (!root.liveBinding) { event.accepted = false; return }

            const k = event.key
            const mods = event.modifiers
            const isStructural = (k === Qt.Key_Backspace || k === Qt.Key_Delete
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

        SyntaxHighlighter {
            textEdit: model.codeLanguage.length > 0 ? edit : null
            definition: model.codeLanguage
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

    Row {
        id: langTagRow
        anchors { top: parent.top; right: parent.right; margins: 4 }
        spacing: 4

        property bool editing: false

        Text {
            visible: !langTagRow.editing && model.codeLanguage !== ""
            text: model.codeLanguage
            font.pixelSize: ((root.liveBinding && root.liveBinding.theme)
                              ? root.liveBinding.themePixelSizeFor(0)  // TextDefault
                              : 14) * 0.8
                            * (root.liveBinding ? root.liveBinding.fontScale : 1.0)
            color: (root.liveBinding && root.liveBinding.theme)
                   ? root.liveBinding.themeColorFor(Theme.Quote)
                   : "#666666"
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    langTagRow.editing = true
                    langInput.forceActiveFocus()
                    langInput.selectAll()
                }
            }
        }
        TextInput {
            id: langInput
            visible: langTagRow.editing
            text: model.codeLanguage
            font.pixelSize: ((root.liveBinding && root.liveBinding.theme)
                              ? root.liveBinding.themePixelSizeFor(0)  // TextDefault
                              : 14) * 0.8
                            * (root.liveBinding ? root.liveBinding.fontScale : 1.0)
            color: (root.liveBinding && root.liveBinding.theme)
                   ? root.liveBinding.themeColorFor(Theme.CodeBlock)
                   : "#222222"
            onActiveFocusChanged: if (!activeFocus) langTagRow.editing = false
            Keys.onReturnPressed: {
                const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
                if (handler) handler.changeCodeLanguage(model.blockAnchor, text)
                langTagRow.editing = false
                edit.forceActiveFocus()
            }
            Keys.onEscapePressed: {
                langTagRow.editing = false
                edit.forceActiveFocus()
            }
        }
    }

    function positionAt(x, y) { return edit.positionAt(x - 8, y - 8) }

    function takeFocus(qtPos: int) {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) {
            const hint = cs.pendingVisualLineHint
            const desiredX = cs.desiredVisualX
            if (hint !== 0 && desiredX >= 0) {
                const lineH = edit.font.pixelSize
                const targetY = (hint === 1) ? lineH * 0.5 : edit.contentHeight - lineH * 0.5
                // CodeBlock TextEdit has 8px margin via anchors.margins
                edit.cursorPosition = edit.positionAt(desiredX - 8, targetY)
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
