// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

import org.kde.syntaxhighlighting

Rectangle {
    id: root

    property int    blockIndex: -1
    property string codeLanguage: ""
    property string codeText: ""
    property var    selectionModel: null

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    color: "#1e1e1e"
    radius: 4
    implicitHeight: textEdit.implicitHeight + 16

    function positionAt(x, y) { return textEdit.positionAt(x, y) }
    readonly property int textLength: textEdit.length

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
