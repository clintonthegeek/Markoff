// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.markoff.view.qml

/// In-place find bar. Bind `searchBackend` to a SearchBackend instance.
/// Toggle visibility via `visible` from the outer shell.
Rectangle {
    id: root

    /// External: the SearchBackend this bar drives.
    property var searchBackend: null

    implicitHeight: row.implicitHeight + 12
    color: palette.window
    border.color: palette.mid
    border.width: 1

    // Auto-find as the user types (live search). Plain implementation;
    // a debounce could be added later.
    onSearchBackendChanged: if (searchBackend) needleField.text = searchBackend.needle

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        TextField {
            id: needleField
            Layout.fillWidth: true
            placeholderText: qsTr("Find…")
            onTextChanged: {
                if (root.searchBackend) {
                    root.searchBackend.needle = text
                    root.searchBackend.findAll()
                }
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    if (root.searchBackend) {
                        if (event.modifiers & Qt.ShiftModifier) {
                            root.searchBackend.findPrevious()
                        } else {
                            root.searchBackend.findNext()
                        }
                    }
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape) {
                    root.visible = false
                    event.accepted = true
                }
            }
        }

        Label {
            text: root.searchBackend
                  ? qsTr("%1 match(es)").arg(root.searchBackend.matchCount)
                  : ""
            Layout.alignment: Qt.AlignVCenter
        }

        ToolButton {
            text: qsTr("▲")  // up arrow — previous
            enabled: root.searchBackend && root.searchBackend.matchCount > 0
            onClicked: if (root.searchBackend) root.searchBackend.findPrevious()
        }

        ToolButton {
            text: qsTr("▼")  // down arrow — next
            enabled: root.searchBackend && root.searchBackend.matchCount > 0
            onClicked: if (root.searchBackend) root.searchBackend.findNext()
        }

        ToolButton {
            text: qsTr("✕")  // close
            onClicked: root.visible = false
        }
    }

    // Take focus on show.
    onVisibleChanged: if (visible) needleField.forceActiveFocus()
}
