// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

import org.kde.syntaxhighlighting
import org.markoff.view.qml

Rectangle {
    id: root

    property int    blockIndex: -1
    property string codeLanguage: ""
    property string codeText: ""
    property var    blockAnchor   // Markoff::BlockAnchor
    property var    document: null  // Markoff::MarkoffDocument *
    property var    selectionModel: null
    property var    theme: null
    property var    structuralKeyHandler: null

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    color: root.theme ? root.theme.codeBlockBackground : "#1e1e1e"
    radius: 4
    implicitHeight: textEdit.implicitHeight + 16

    function positionAt(x, y) { return textEdit.positionAt(x, y) }
    readonly property int textLength: textEdit.length

    // Expose cursor/selection state (used by hit-test layer and external consumers).
    readonly property int cursorPosition: textEdit.cursorPosition
    readonly property string selectedText: textEdit.selectedText

    LiveEditBinding {
        id: editBinding
        document: root.document
        blockAnchor: root.blockAnchor !== undefined ? root.blockAnchor : editBinding.blockAnchor
        textDocument: textEdit.textDocument
    }

    TextEdit {
        id: textEdit
        anchors.fill: parent
        anchors.margins: 8
        textFormat: TextEdit.PlainText
        readOnly: false
        selectByMouse: false
        wrapMode: TextEdit.NoWrap
        font.family: "monospace"
        font.pixelSize: 13
        color: root.theme ? root.theme.codeBlock : "#dcdcdc"
        // Tab inserts a literal tab (Qt TextEdit default when readOnly: false).
        // Enter inserts \n (TextEdit default). The handler returns false for
        // both, so TextEdit handles them naturally.

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            if (!root.structuralKeyHandler) return
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter ||
                event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete ||
                event.key === Qt.Key_Tab) {
                const handled = root.structuralKeyHandler.tryHandle(
                    event.key,
                    event.modifiers,
                    root.blockAnchor,
                    root.blockIndex,
                    textEdit.cursorPosition,
                    textEdit.selectedText.length === 0,
                    root.codeText
                )
                if (handled) event.accepted = true
            }
        }

        Connections {
            target: root.selectionModel
            function onSelectionChanged() {
                const r = root.selectionModel.rangeForBlock(root.blockIndex)
                if (r.x === -1) {
                    textEdit.deselect()
                } else {
                    const end = Math.min(r.y, textEdit.length)
                    textEdit.select(r.x, end)
                }
            }
        }
    }

    // Model-driven text updates: use setModelText so the cycle guard is held
    // synchronously while the QTextDocument update fires contentsChange.
    onCodeTextChanged: {
        editBinding.setModelText(root.codeText)
    }

    SyntaxHighlighter {
        textEdit: textEdit
        definition: root.codeLanguage.length > 0 ? root.codeLanguage : "Markdown"
    }
}
