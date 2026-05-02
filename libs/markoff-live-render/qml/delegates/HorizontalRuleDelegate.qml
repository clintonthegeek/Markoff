// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

/// Read-only horizontal rule — a thin separator line.
Item {
    width: ListView.view ? ListView.view.width : 600
    height: 17  // 1px line + 8px padding top + 8px bottom

    Rectangle {
        anchors.centerIn: parent
        width: parent.width - 16
        height: 1
        color: palette.mid
    }
}
