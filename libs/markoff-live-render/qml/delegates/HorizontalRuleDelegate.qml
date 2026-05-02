// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Horizontal rule. BlockSelected focus ring when focused.
/// positionAt returns -1: tells BlockHitTester this is a non-text block.
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    height: 17

    property int modelIndex: index
    readonly property string blockText: ""  // non-text; contributes nothing to copy

    // Focused when cursorKind == "BlockSelected" and block matches.
    // We compare by modelIndex since we can't inspect BlockAnchor from QML.
    readonly property bool isFocused: {
        const cs = ListView.view && ListView.view.binding
                   ? ListView.view.binding.cursorState : null
        return cs ? cs.cursorKind === "BlockSelected" && _checkFocus() : false
    }

    function _checkFocus() {
        // Heuristic: if cursorKind is BlockSelected and this is the most recently
        // clicked non-text block, show the ring. Full anchor comparison needs R4.
        return false  // refined in R4 when LiveCursorState exposes focused row index
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width - 16
        height: 1
        color: palette.mid
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        color: "transparent"
        border.color: palette.highlight
        border.width: root.isFocused ? 2 : 0
        radius: 2
    }

    // positionAt: -1 signals to BlockHitTester that this is non-text.
    function positionAt(x, y) { return -1 }
}
