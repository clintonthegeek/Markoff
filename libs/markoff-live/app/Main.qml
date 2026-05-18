// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.markoff.live 1.0

ApplicationWindow {
    id: window
    width: 900
    height: 700
    visible: true
    color: (modelBinding.theme)
           ? modelBinding.themeColorFor(Theme.EditorBackground)
           : "#ffffff"
    title: ctxMain.title

    LiveListModelBinding {
        id: modelBinding
        document: ctxDocument
        Component.onCompleted: {
            modelBinding.setSession(ctxSession)
            modelBinding.setLinkService(ctxMain.linkService)
            modelBinding.setFromContext(ctxMain.filePath)
            if (actionController) {
                actionController.saveRequested.connect(ctxMain.save)
                actionController.themeToggleRequested.connect(modelBinding.applyDefaultTheme)
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        LiveView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            binding: modelBinding
            focus: true
        }

        // Status bar: shows hover/link status messages from the link service.
        Rectangle {
            id: statusBar
            Layout.fillWidth: true
            height: ctxMain.statusMessage !== "" ? statusLabel.implicitHeight + 6 : 0
            visible: ctxMain.statusMessage !== ""
            color: (modelBinding.theme)
                   ? modelBinding.themeColorFor(Theme.CodeBlockBackground)
                   : "#f0f0f0"

            Text {
                id: statusLabel
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
                anchors.leftMargin: 8
                text: ctxMain.statusMessage
                color: (modelBinding.theme)
                       ? modelBinding.themeColorFor(Theme.TextDefault)
                       : "#222222"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }
    }
}
