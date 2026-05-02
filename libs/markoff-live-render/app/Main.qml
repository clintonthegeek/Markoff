// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window
    width: 800
    height: 600
    visible: true
    title: qsTr("markoff-live-render (scaffold)")

    Rectangle {
        anchors.fill: parent
        color: "#fafafa"
        Label {
            anchors.centerIn: parent
            text: qsTr("markoff-live-render: scaffold (R1C). Real UI lands in R2.")
        }
    }
}
