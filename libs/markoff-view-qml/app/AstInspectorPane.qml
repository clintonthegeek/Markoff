// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/// Side pane: shows parse-event status. Exercises the parseUpdatedAt seam.
/// In Phase-1 we don't render the full AST tree (Markoff::Document isn't
/// QML-friendly); we just show that parses are flowing. Phase-2 will replace
/// this with a real AST tree view.
Rectangle {
    id: root
    color: palette.window
    border.color: palette.mid
    border.width: 1

    /// External: bind to MarkoffEditor.editorBackend
    property var editorBackend: null

    property int  parseCount: 0
    property string lastVersion: qsTr("(no parse yet)")

    Connections {
        target: root.editorBackend
        // Foundation's signal signature is (parsed, parseSequence, blockAnchors).
        // We just count and surface the parse-sequence number. Arg names are
        // positional in QML callbacks.
        function onParseUpdatedAt(parsed, parseSequence, blockAnchors) {
            root.parseCount += 1
            root.lastVersion = qsTr("parse #%1 (seq %2)")
                                   .arg(root.parseCount)
                                   .arg(parseSequence)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        Label {
            text: qsTr("AST Inspector")
            font.bold: true
        }
        Label { text: root.lastVersion }
        Label {
            text: qsTr("Parses received: %1").arg(root.parseCount)
            color: root.parseCount > 0 ? palette.text : palette.placeholderText
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
            // Reserved for future Phase-2 AST tree rendering.
        }
    }
}
