// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

/// Read-only paragraph with selection highlight.
/// `modelIndex` is a declared QML property (not just the context var) so
/// BlockHitTester can read it from C++ via QObject::property("modelIndex").
/// `blockText` exposes the raw text for Ctrl-C copy collection in LiveView.qml.
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    // Expose index as a real Q_PROPERTY-accessible value (context var "index"
    // is not readable from C++ via QObject::property).
    property int modelIndex: index
    readonly property string blockText: model.text

    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: 8; rightPadding: 8
        topPadding: 4; bottomPadding: 4
        readOnly: true
        textFormat: TextEdit.PlainText
        text: model.text
        wrapMode: TextEdit.Wrap
        font.pixelSize: 14
        color: palette.text
        selectByMouse: false

        function applySelection() {
            const sv = ListView.view && ListView.view.binding
                       ? ListView.view.binding.selectionView : null
            if (!sv) { deselect(); return }
            const r = sv.rangeForBlock(model.index)
            if (!r || r.x < 0) { deselect(); return }
            select(r.x, Math.min(r.y, length))
        }

        Connections {
            target: ListView.view && ListView.view.binding
                    ? ListView.view.binding.selectionView : null
            function onSelectionChanged() { edit.applySelection() }
        }
    }

    // positionAt forwarded from root item so BlockHitTester can call it via invokeMethod.
    function positionAt(x, y) { return edit.positionAt(x - edit.leftPadding, y - edit.topPadding) }
}
