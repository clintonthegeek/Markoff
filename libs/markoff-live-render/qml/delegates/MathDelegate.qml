// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Item {
    id: root
    property int modelIndex: index
    readonly property string blockText: model.text
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: label.implicitHeight + 16

    function positionAt(x, y) { return -1 }
    function focusEditAt(qtPos) {}

    Text {
        id: label
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        leftPadding: 8
        text: model.text
        color: palette.mid
        font.pixelSize: 13
        font.family: "monospace"
        wrapMode: Text.Wrap
    }
}
