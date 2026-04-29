// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    id: root

    property int blockIndex: -1
    property var selectionModel: null
    property var theme: null

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    height: 16

    /// HR has no text content; positionAt returns 0 (start-of-block).
    function positionAt(x, y) { return 0 }
    readonly property int textLength: 0

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: "#888"
    }

    Rectangle {
        id: selectionOverlay
        objectName: "selectionOverlay"
        anchors.fill: parent
        color: root.theme ? root.theme.selectionBackground : "#406080"
        opacity: 0.35
        // Q_INVOKABLE rangeForBlock() isn't tracked by QML's binding system,
        // so we drive `selected` via Connections on the model's signal.
        property bool selected: false
        visible: selected
    }

    Connections {
        target: root.selectionModel
        function onSelectionChanged() {
            selectionOverlay.selected =
                root.selectionModel.rangeForBlock(root.blockIndex).x !== -1
        }
    }
}
