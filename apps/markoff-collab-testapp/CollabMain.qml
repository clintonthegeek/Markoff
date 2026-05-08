// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.markoff.live 1.0

ApplicationWindow {
    id: window
    width: 1400; height: 700
    visible: true
    title: "Markoff D5 Collab Testapp"

    RowLayout {
        anchors.fill: parent
        spacing: 4

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label { text: "Replica A"; font.bold: true; padding: 4 }
            LiveListModelBinding {
                id: bindingA
                document: ctxDocumentA
            }
            LiveView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                binding: bindingA
                focus: true
            }
        }

        Rectangle { width: 2; Layout.fillHeight: true; color: "#555" }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label { text: "Replica B"; font.bold: true; padding: 4 }
            LiveListModelBinding {
                id: bindingB
                document: ctxDocumentB
            }
            LiveView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                binding: bindingB
            }
        }
    }
}
