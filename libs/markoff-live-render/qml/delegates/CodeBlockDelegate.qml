// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.kde.syntaxhighlighting
import org.markoff.live.render 1.0

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
    }

    TextEdit {
        id: edit
        anchors { left: parent.left; right: parent.right
                  top: parent.top; bottom: parent.bottom; margins: 8 }
        readOnly: false
        textFormat: TextEdit.PlainText
        text: model.text
        wrapMode: TextEdit.NoWrap
        font.family: "monospace"
        font.pixelSize: 13
        color: palette.text
        selectByMouse: true
        persistentSelection: true

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
    }

    function positionAt(x, y) { return edit.positionAt(x - 8, y - 8) }
}
