// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls

import org.markoff.view.qml

/// Floating completion popup. Bind:
///   - model       : CompletionPopupModel
///   - parentEditor: TextArea — used for cursor-rectangle anchoring & insertion
///
/// Caller positions us via the open(rectInEditorCoords) helper or by binding
/// x/y to the editor's cursorRectangle.
Popup {
    id: root

    /// External: the model providing candidates.
    property var model: null
    /// External: the TextArea this popup completes for. Used for insertion.
    property var parentEditor: null

    width: 240
    height: Math.min(220, listView.contentHeight + 8)
    padding: 4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    modal: false
    focus: false  // we want to handle keys via parentEditor; popup just floats

    background: Rectangle {
        color: palette.window
        border.color: palette.mid
        border.width: 1
        radius: 4
    }

    contentItem: ListView {
        id: listView
        clip: true
        model: root.model
        currentIndex: 0
        keyNavigationWraps: true

        delegate: ItemDelegate {
            // `display` is a FINAL property on ItemDelegate in Qt 6; don't re-declare it.
            // Model roles are accessible via `model.*` in delegates.
            required property string insertion
            required property int index

            width: ListView.view.width
            text: model.display
            highlighted: ListView.isCurrentItem
            onClicked: {
                listView.currentIndex = index
                root.acceptCurrent()
            }
        }

        // Allow the parent editor to forward Up/Down/Enter to us via
        // root.handleKey(event), called from MarkoffEditor.qml in T20.
    }

    /// Selects the current candidate, invokes insertion in the parent editor,
    /// and closes the popup.
    function acceptCurrent() {
        if (!model || !parentEditor) { close(); return; }
        if (listView.currentIndex < 0 || listView.currentIndex >= model.count) {
            close(); return;
        }
        const item = listView.currentItem
        if (item && typeof item.insertion === "string" && item.insertion.length > 0) {
            parentEditor.insert(parentEditor.cursorPosition, item.insertion)
        }
        close()
    }

    /// Outer shell forwards key events here. Returns true if handled.
    function handleKey(event) {
        if (!visible) return false
        if (event.key === Qt.Key_Down) {
            listView.incrementCurrentIndex()
            return true
        }
        if (event.key === Qt.Key_Up) {
            listView.decrementCurrentIndex()
            return true
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter ||
            event.key === Qt.Key_Tab) {
            acceptCurrent()
            return true
        }
        if (event.key === Qt.Key_Escape) {
            close()
            return true
        }
        return false
    }

    onModelChanged: {
        if (model && model.count > 0) {
            listView.currentIndex = 0
        }
    }
}
