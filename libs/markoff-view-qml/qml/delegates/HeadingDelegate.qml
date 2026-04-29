// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

TextEdit {
    id: textEdit

    required property int    blockIndex
    required property string blockText
    required property int    headingLevel
    required property var    selectionModel

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12

    text: textEdit.blockText
    textFormat: TextEdit.PlainText
    readOnly: true
    selectByMouse: false
    wrapMode: TextEdit.Wrap
    font.pixelSize: {
        switch (textEdit.headingLevel) {
            case 1: return 28
            case 2: return 24
            case 3: return 20
            case 4: return 18
            case 5: return 16
            case 6: return 14
            default: return 16
        }
    }
    font.bold: true

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
