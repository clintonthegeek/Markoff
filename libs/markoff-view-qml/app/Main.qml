// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.markoff.view.qml
import org.markoff.view.qml.app

ApplicationWindow {
    id: appWindow
    visible: true
    width: 1100
    height: 720
    title: qsTr("markoff-view-qml POC")

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        // Left: the editor.
        MarkoffEditor {
            id: editor
            SplitView.fillWidth: true
            SplitView.minimumWidth: 300
            document: doc
            theme: theme
            completionModel: completionModel
            focus: true
        }

        // Right: AST inspector pane (the seam-exercise per plan).
        AstInspectorPane {
            SplitView.preferredWidth: 320
            SplitView.minimumWidth: 200
            editorBackend: editor.editorBackend
        }
    }
}
