// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

import org.kde.syntaxhighlighting

Rectangle {
    id: root

    required property int    blockIndex
    required property string codeLanguage
    required property string codeText
    required property var    selectionModel

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    color: "#1e1e1e"
    radius: 4
    implicitHeight: textEdit.implicitHeight + 16

    TextEdit {
        id: textEdit
        anchors.fill: parent
        anchors.margins: 8
        text: root.codeText
        textFormat: TextEdit.PlainText
        readOnly: true
        selectByMouse: false
        wrapMode: TextEdit.NoWrap
        font.family: "monospace"
        font.pixelSize: 13
        color: "#dcdcdc"

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

    SyntaxHighlighter {
        textEdit: textEdit
        definition: root.codeLanguage.length > 0 ? root.codeLanguage : "Markdown"
    }
}
