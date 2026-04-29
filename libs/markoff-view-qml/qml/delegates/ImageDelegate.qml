// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

Item {
    id: root

    property int    blockIndex: -1
    property string imageSrc: ""
    property string imageAlt: ""
    property string imageTitle: ""

    /// Image has no text content; positionAt returns 0 (start-of-block).
    function positionAt(x, y) { return 0 }
    readonly property int textLength: 0

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    implicitHeight: image.status === Image.Ready ? image.implicitHeight : altLabel.implicitHeight + 16

    Image {
        id: image
        anchors.left: parent.left
        anchors.right: parent.right
        source: root.imageSrc
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        visible: status === Image.Ready
    }

    Rectangle {
        id: altLabel
        visible: image.status !== Image.Ready
        anchors.fill: parent
        color: "#222"
        Text {
            anchors.centerIn: parent
            color: "#ccc"
            text: root.imageAlt.length > 0
                ? "[image: " + root.imageAlt + "]"
                : "[image: " + root.imageSrc + "]"
            font.italic: true
        }
    }
}
