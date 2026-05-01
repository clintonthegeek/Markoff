// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

import org.markoff.view.qml

Item {
    id: root

    property int    blockIndex: -1
    property string blockText: ""
    property var    blockAnchor   // Markoff::BlockAnchor
    property var    document: null  // Markoff::MarkoffDocument *
    property var    selectionModel: null
    property var    theme: null

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    implicitHeight: textEdit.implicitHeight

    /// Proxy positionAt to the inner TextEdit. The hit-test layer in
    /// LiveView.qml calls this on the delegate; without the proxy it would
    /// be undefined (Item has no positionAt) and offsets would collapse to 0.
    function positionAt(x, y) { return textEdit.positionAt(x, y) }

    /// Proxy length so LiveView.qml can clamp INT32_MAX sentinel when needed.
    readonly property int textLength: textEdit.length

    // Expose cursor/selection state for LiveView.qml's Keys handler.
    readonly property int cursorPosition: textEdit.cursorPosition
    readonly property string selectedText: textEdit.selectedText

    LiveEditBinding {
        id: editBinding
        document: root.document
        blockAnchor: root.blockAnchor !== undefined ? root.blockAnchor : editBinding.blockAnchor
        textDocument: textEdit.textDocument
    }

    TextEdit {
        id: textEdit
        anchors.left: parent.left
        anchors.right: parent.right
        textFormat: TextEdit.PlainText
        readOnly: false
        selectByMouse: false
        wrapMode: TextEdit.Wrap
        font.pixelSize: 16

        InlineFormatHighlighter {
            document: textEdit.textDocument
            source: root.blockText
        }

        onActiveFocusChanged: {
            if (activeFocus) {
                const lv = root.ListView.view
                if (lv) lv.parent.focusedDelegate = root
            }
        }
    }

    // Model-driven text updates: use setModelText so the cycle guard is held
    // synchronously while the QTextDocument update fires contentsChange.
    // The QML begin/end pattern doesn't work because Qt Quick defers
    // TextEdit text updates past the guard window.
    onBlockTextChanged: {
        editBinding.setModelText(root.blockText)
    }

    Connections {
        target: root.selectionModel
        function onSelectionChanged() {
            const r = root.selectionModel.rangeForBlock(root.blockIndex)
            if (r.x === -1) {
                textEdit.deselect()
            } else {
                const end = Math.min(r.y, textEdit.length)
                textEdit.select(r.x, end)
            }
        }
    }
}
