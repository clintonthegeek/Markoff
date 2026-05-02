// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Image block (R2/R3: shows source markdown as placeholder text).
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text

    // ListView.view attached property only resolves on the delegate ROOT;
    // children must access via `root.selectionView`.
    readonly property var selectionView:
        ListView.view && ListView.view.binding
            ? ListView.view.binding.selectionView : null

    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: 8; rightPadding: 8; topPadding: 4; bottomPadding: 4
        readOnly: true
        textFormat: TextEdit.PlainText
        text: model.text
        wrapMode: TextEdit.Wrap
        font.pixelSize: 13
        color: palette.placeholderText
        selectByMouse: false
        persistentSelection: true

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

    function positionAt(x, y) { return edit.positionAt(x - edit.leftPadding, y - edit.topPadding) }
}
