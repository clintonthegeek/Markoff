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

    // ---- Projection-layer "hole" support (T21). ----
    // When isHole === true, this delegate is a transient projection row
    // backing an unparsed user-intent paragraph (preedit pattern). The
    // delegate writes go to the layer's bufferText; source is NOT touched
    // until commitBlockHole.
    property bool isHole: false
    property var  holeId: 0
    property var  projectionLayer: null

    // Cycle guard: when applying model.text → editBinding.setModelText we
    // must NOT mirror that change back to the layer's buffer.
    property bool m_applyingModelBuffer: false

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
        // On hole rows the binding must NOT call applyLocalEdit; we sever
        // its textDocument hookup so contentsChange is never observed.
        // Option A from the brief: setTextDocument(nullptr) cleanly
        // disconnects (verified in LiveEditBinding.cpp::rewireTextDocument).
        textDocument: root.isHole ? null : textEdit.textDocument
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
            // ---- Hole-row key intercepts (run before structural handler). ----
            if (root.isHole && root.projectionLayer) {
                if (event.key === Qt.Key_Escape) {
                    root.projectionLayer.dropBlockHole(root.holeId)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Backspace
                    && textEdit.cursorPosition === 0
                    && textEdit.length === 0) {
                    root.projectionLayer.dropBlockHole(root.holeId)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    const buf = textEdit.getText(0, textEdit.length)
                    if (buf.length > 0) {
                        // Commit current hole; user re-presses Enter at end
                        // of the now-real paragraph to open the next hole.
                        // (One-keystroke chain is deferred — see LiveView.)
                        root.projectionLayer.commitBlockHole(root.holeId)
                    }
                    // empty buffer: consume Enter, keep hole open.
                    event.accepted = true
                    return
                }
            }

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
            // Hole focus-out: commit if non-empty, drop if empty.
            if (root.isHole && root.projectionLayer && !activeFocus) {
                const buf = textEdit.getText(0, textEdit.length)
                if (buf.length > 0)
                    root.projectionLayer.commitBlockHole(root.holeId)
                else
                    root.projectionLayer.dropBlockHole(root.holeId)
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

        // Buffer mirror: hole-row text changes flow back into the layer's
        // pending buffer. Cycle-guarded against model→delegate replays.
        onTextChanged: {
            if (!root.isHole || !root.projectionLayer) return
            if (root.m_applyingModelBuffer) return
            root.projectionLayer.setBlockHoleBuffer(
                root.holeId,
                textEdit.getText(0, textEdit.length)
            )
            // Restart idle-commit timer on every keystroke.
            idleCommitTimer.restart()
        }

        InlineFormatHighlighter {
            document: textEdit.textDocument
            source: root.blockText
            projectionLayer: root.modelBinding ? root.modelBinding.projectionLayer : null
            blockIndex: root.blockIndex
        }

    }

    // Idle-commit timer for hole rows: 250ms after the last keystroke,
    // commit the buffer to source.
    Timer {
        id: idleCommitTimer
        interval: 250
        repeat: false
        onTriggered: {
            if (!root.isHole || !root.projectionLayer) return
            const buf = textEdit.getText(0, textEdit.length)
            if (buf.length > 0) root.projectionLayer.commitBlockHole(root.holeId)
        }
    }

    // IME finalization: when the layer is about to commit our hole, force
    // any pending Qt input-method composition to deliver as a final
    // committed inputMethodEvent before the layer reads bufferText.
    Connections {
        target: root.projectionLayer
        enabled: root.isHole
        function onAboutToCommit(id) {
            if (id !== root.holeId) return
            if (Qt.inputMethod && typeof Qt.inputMethod.commit === "function") {
                Qt.inputMethod.commit()
            }
        }
    }

    // Model-driven text updates: use setModelText so the cycle guard is held
    // synchronously while the QTextDocument update fires contentsChange.
    // The QML begin/end pattern doesn't work because Qt Quick defers
    // TextEdit text updates past the guard window.
    onBlockTextChanged: {
        // Hold the buffer-mirror guard while the model-driven text update
        // propagates through QTextDocument::setPlainText → onTextChanged.
        root.m_applyingModelBuffer = true
        editBinding.setModelText(root.blockText)
        root.m_applyingModelBuffer = false
    }

    // On fresh delegate creation the textDocument binding may not have been
    // resolved when onBlockTextChanged first fires, leaving the TextEdit empty.
    // Re-apply the text after the component is fully initialised.
    Component.onCompleted: {
        if (root.blockText.length > 0) {
            root.m_applyingModelBuffer = true
            editBinding.setModelText(root.blockText)
            root.m_applyingModelBuffer = false
        }
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
