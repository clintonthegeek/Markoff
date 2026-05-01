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

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    color: root.theme ? root.theme.codeBlockBackground : "#1e1e1e"
    radius: 4
    implicitHeight: textEdit.implicitHeight + 16

    function positionAt(x, y) { return textEdit.positionAt(x, y) }
    readonly property int textLength: textEdit.length

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
        // Enter inserts \n (TextEdit default). Neither is intercepted here.

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

    // Model-driven text updates go through the cycle guard so that
    // contentsChange fired by the text assignment is suppressed.
    onCodeTextChanged: {
        editBinding.beginModelUpdate()
        textEdit.text = root.codeText
        editBinding.endModelUpdate()
    }

    SyntaxHighlighter {
        textEdit: textEdit
        definition: root.codeLanguage.length > 0 ? root.codeLanguage : "Markdown"
    }
}
