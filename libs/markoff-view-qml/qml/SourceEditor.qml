// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

import org.kde.syntaxhighlighting
import org.markoff.view.qml

/// Source-mode markdown editor. Phase-1 POC.
///
/// External property contract:
///   - document : Markoff.MarkoffDocument *  (required)
///   - theme    : Markoff.Theme              (Q_GADGET value type)
///
/// Exposes (read-only):
///   - editorBackend : alias to the internal EditorBackend (for outer shell to attach
///                     SearchBar / CompletionPopup to)
Item {
    id: root

    // External inputs
    property var document: null
    property var theme

    // Read-only exposure of the C++ backend so siblings (search bar, completion popup)
    // can bind to it via the outer shell.
    readonly property alias editorBackend: backend

    EditorBackend {
        id: backend
        document: root.document
        theme: root.theme
    }

    SourceTextDocumentBinding {
        id: binding
        markoffDocument: backend.document
        session: backend.session
        // textArea.textDocument is a QQuickTextDocument; the underlying
        // QTextDocument is exposed via the QML extension's invokable setter
        // (the binding's own textDocument property is QTextDocument *, which
        // QML can't extract from QQuickTextDocument without a helper).
        Component.onCompleted: setQtQuickDocument(textArea.textDocument)
    }

    // KSyntaxHighlighting attached to the TextArea (sibling, not child).
    // Kept outside the TextArea body so that Breeze's TextArea.qml doesn't
    // try to slot it in as a TextInput child.
    SyntaxHighlighter {
        textEdit: textArea
        definition: "Markdown"
    }

    // Reverse direction: when the binding notifies (because Session changed
    // externally, e.g. SearchEngine moved selection to a match), update the
    // TextArea programmatically. Sibling of the TextArea (not a child) so
    // that Breeze's TextArea.qml doesn't try to slot it in as a TextInput.
    Connections {
        target: binding
        function onCursorPositionChanged() {
            if (textArea.cursorPosition !== binding.cursorPosition) {
                textArea.cursorPosition = binding.cursorPosition
            }
        }
        function onSelectionStartChanged() {
            if (textArea.selectionStart !== binding.selectionStart) {
                // Use select() to set both ends atomically.
                textArea.select(binding.selectionStart, binding.selectionEnd)
            }
        }
        function onSelectionEndChanged() {
            if (textArea.selectionEnd !== binding.selectionEnd) {
                textArea.select(binding.selectionStart, binding.selectionEnd)
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true

        TextArea {
            id: textArea
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            font.family: "monospace"
            // Phase-1: use the system font; theme.font integration is a nice-to-have
            // we can wire in once Theme exposes a per-Slot font hook.

            // Two-way bind cursor + selection through the binding:
            // QML reads from binding (which mirrors backend's anchors back to int);
            // QML writes to binding via Binding handlers below.

            // Forward direction: TextArea state → binding ints (which lift to backend → Session).
            onCursorPositionChanged: binding.cursorPosition = cursorPosition
            onSelectionStartChanged: binding.selectionStart = selectionStart
            onSelectionEndChanged:   binding.selectionEnd   = selectionEnd

            // Standard text-editor key bindings handled here (full set will accrete later;
            // Phase-1 covers the must-haves: undo/redo via the foundation's CRDT stack).
            Keys.onPressed: (event) => {
                if (event.modifiers & Qt.ControlModifier) {
                    if (event.key === Qt.Key_Z && !(event.modifiers & Qt.ShiftModifier)) {
                        backend.undo()
                        event.accepted = true
                        return
                    }
                    if (event.key === Qt.Key_Y ||
                        (event.key === Qt.Key_Z && (event.modifiers & Qt.ShiftModifier))) {
                        backend.redo()
                        event.accepted = true
                        return
                    }
                    if (event.key === Qt.Key_C) {
                        // Copy as raw markdown (rather than letting TextArea's default
                        // copy do plain selectedText). For source mode the contents are
                        // identical; the contract holds for Phase-2 Live mode.
                        const md = backend.copySelectionAsMarkdown()
                        if (md.length > 0) {
                            // Copy to clipboard via Qt.application or a small C++ helper.
                            // Cleanest: use Qt's built-in TextArea.copy() which copies
                            // selectedText — for source mode this matches.
                            textArea.copy()
                        }
                        event.accepted = true
                        return
                    }
                }
            }
        }
    }
}
