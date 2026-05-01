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
    property var    structuralKeyHandler: null
    property var    modelBinding: null
    property var    fenceController: null  // LiveSpeculativeFenceController*

    width: ListView.view ? ListView.view.width - 24 : 600
    x: 12
    implicitHeight: textEdit.implicitHeight

    /// Proxy positionAt to the inner TextEdit. The hit-test layer in
    /// LiveView.qml calls this on the delegate; without the proxy it would
    /// be undefined (Item has no positionAt) and offsets would collapse to 0.
    function positionAt(x, y) { return textEdit.positionAt(x, y) }

    /// Proxy length so LiveView.qml can clamp INT32_MAX sentinel when needed.
    readonly property int textLength: textEdit.length

    // Expose cursor/selection state (used by hit-test layer and external consumers).
    readonly property int cursorPosition: textEdit.cursorPosition
    readonly property string selectedText: textEdit.selectedText

    function focusAtEnd()      { textEdit.forceActiveFocus(); textEdit.cursorPosition = textEdit.length }
    function focusAtStart()    { textEdit.forceActiveFocus(); textEdit.cursorPosition = 0 }
    // Place cursor at pos; if the TextEdit text hasn't populated yet, schedule
    // a retry so the position sticks once content arrives.
    function focusAtPos(pos) {
        textEdit.forceActiveFocus()
        if (textEdit.length >= pos) {
            textEdit.cursorPosition = pos
        } else {
            // Text not yet synced — retry once more after the next event cycle.
            Qt.callLater(function() {
                textEdit.cursorPosition = Math.min(pos, textEdit.length)
            })
        }
    }

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

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            if (!root.structuralKeyHandler) return
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter ||
                event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete ||
                event.key === Qt.Key_Tab) {
                const handled = root.structuralKeyHandler.tryHandle(
                    event.key,
                    event.modifiers,
                    root.blockAnchor,
                    root.blockIndex,
                    textEdit.cursorPosition,
                    textEdit.selectedText.length === 0,
                    textEdit.getText(0, textEdit.length)
                )
                if (handled) event.accepted = true
            }
        }

        onActiveFocusChanged: {
            if (activeFocus && root.modelBinding) {
                root.modelBinding.notifyFocused(root.blockAnchor, textEdit.cursorPosition)
            }
        }
        onCursorPositionChanged: {
            if (activeFocus && root.modelBinding) {
                root.modelBinding.notifyFocusedCursorMoved(textEdit.cursorPosition)
            }
        }
        onInputMethodComposingChanged: {
            editBinding.composing = inputMethodComposing
            if (root.modelBinding)
                root.modelBinding.setRowComposing(root.blockIndex, inputMethodComposing)
        }

        InlineFormatHighlighter {
            document: textEdit.textDocument
            source: root.blockText
        }

    }

    // Model-driven text updates: use setModelText so the cycle guard is held
    // synchronously while the QTextDocument update fires contentsChange.
    // The QML begin/end pattern doesn't work because Qt Quick defers
    // TextEdit text updates past the guard window.
    onBlockTextChanged: {
        editBinding.setModelText(root.blockText)
    }

    // On fresh delegate creation the textDocument binding may not have been
    // resolved when onBlockTextChanged first fires, leaving the TextEdit empty.
    // Re-apply the text after the component is fully initialised.
    Component.onCompleted: {
        if (root.blockText.length > 0)
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

    Connections {
        target: root.modelBinding
        function onFocusRestoreRequested(anchor, qtPos) {
            if (root.modelBinding && root.modelBinding.isFocusRestoreTarget(root.blockAnchor)) {
                textEdit.forceActiveFocus()
                textEdit.cursorPosition = Math.min(qtPos, textEdit.length)
            }
        }
    }

    Connections {
        target: editBinding
        function onEditApplied(anchor, postText) {
            if (root.fenceController) {
                root.fenceController.onEditApplied(anchor, root.blockIndex, postText)
            }
        }
    }
}
