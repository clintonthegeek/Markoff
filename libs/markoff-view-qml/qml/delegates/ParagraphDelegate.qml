// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

TextEdit {
    id: textEdit

    required property int    blockIndex
    required property string blockText
    required property var    selectionModel  // LiveSelectionModel *

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12

    text: textEdit.blockText
    textFormat: TextEdit.MarkdownText
    readOnly: true
    selectByMouse: false
    wrapMode: TextEdit.Wrap
    font.pixelSize: 16

    Connections {
        target: textEdit.selectionModel
        function onSelectionChanged() {
            const r = textEdit.selectionModel.rangeForBlock(textEdit.blockIndex)
            if (r.x === -1) {
                textEdit.deselect()
            } else {
                const end = Math.min(r.y, textEdit.length)
                textEdit.select(r.x, end)
            }
        }
    }
}
