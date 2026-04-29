// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Item {
    id: root

    property int    blockIndex: -1
    property string blockText: ""
    property int    headingLevel: 0
    property var    selectionModel: null
    property var    theme: null

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    implicitHeight: textEdit.implicitHeight

    function positionAt(x, y) { return textEdit.positionAt(x, y) }
    readonly property int textLength: textEdit.length

    TextEdit {
        id: textEdit
        anchors.left: parent.left
        anchors.right: parent.right
        text: root.blockText
        textFormat: TextEdit.PlainText
        readOnly: true
        selectByMouse: false
        wrapMode: TextEdit.Wrap
        font.pixelSize: {
            switch (root.headingLevel) {
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
