// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.kde.syntaxhighlighting
import org.markoff.live 1.0

Rectangle {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight + 16
    color: Qt.rgba(0, 0, 0, 0.05)
    radius: 4

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
        anchors { left: parent.left; right: parent.right
                  top: parent.top; bottom: parent.bottom; margins: 8 }
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.NoWrap
        font.family: "monospace"
        font.pixelSize: 13
        color: palette.text
        selectByMouse: true
        persistentSelection: true

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (!handler) { event.accepted = false; return }
            const k = event.key
            if (k !== Qt.Key_Backspace && k !== Qt.Key_Delete && k !== Qt.Key_Tab) {
                return
            }
            const handled = handler.tryHandle(k, event.modifiers, root.modelIndex,
                edit.cursorPosition, edit.selectionStart === edit.selectionEnd, model.text)
            event.accepted = handled
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

    Row {
        id: langTagRow
        anchors { top: parent.top; right: parent.right; margins: 4 }
        spacing: 4

        property bool editing: false

        Text {
            visible: !langTagRow.editing && model.codeLanguage !== ""
            text: model.codeLanguage
            font.pixelSize: 11
            color: palette.mid
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
            font.pixelSize: 11
            color: palette.text
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
