// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

Rectangle {
    id: root
    required property var liveBinding
    readonly property var fc: liveBinding ? liveBinding.findController : null

    visible: fc && fc.isActive
    height: visible ? 32 : 0
    color: "#f0f0f0"

    signal closed()

    function requestClose() {
        if (fc) fc.deactivate()
        root.closed()
    }

    Row {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        TextField {
            id: needleInput
            width: 200
            text: fc ? fc.needle : ""
            placeholderText: "Find..."
            onTextChanged: if (fc) fc.needle = text
            Keys.onReturnPressed: if (fc) fc.findNext()
            Keys.onEscapePressed: root.requestClose()
        }

        Button {
            text: "<"
            enabled: fc && fc.matchCount > 0
            onClicked: if (fc) fc.findPrevious()
        }
        Button {
            text: ">"
            enabled: fc && fc.matchCount > 0
            onClicked: if (fc) fc.findNext()
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: fc ? (fc.matchCount > 0
                ? ((fc.currentMatchIndex + 1) + " / " + fc.matchCount)
                : "0 / 0") : ""
        }

        Item { width: 8; height: 1 }

        Button {
            text: "×"
            onClicked: root.requestClose()
        }
    }
}
