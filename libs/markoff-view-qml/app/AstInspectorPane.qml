// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

/// Side pane: reserved for future AST inspector functionality.
/// The parseUpdatedAt relay was removed in D4 (live mode retired).
Rectangle {
    id: root
    color: palette.window
    border.color: palette.mid
    border.width: 1

    /// External: bind to MarkoffEditor.editorBackend
    property var editorBackend: null

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        Label {
            text: qsTr("AST Inspector")
            font.bold: true
        }
        Label {
            text: qsTr("(parse relay retired in D4)")
            color: palette.placeholderText
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"
        }
    }
}
