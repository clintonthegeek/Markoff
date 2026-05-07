// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.markoff.view.qml

/// Outer shell. Wraps a SourceEditor with search bar and completion popup.
///
/// External property contract:
///   - document         : Markoff.MarkoffDocument *
///   - theme            : Markoff.Theme
///   - completionModel  : CompletionPopupModel  (optional — host-supplied so
///                        emoji + future tag/wikilink providers can be wired in)
Item {
    id: root

    property var document: null
    property var theme
    property var completionModel: null

    /// Exposes the inner EditorBackend so the host can bind to document/session.
    readonly property alias editorBackend: sourceEditor.editorBackend

    SourceEditor {
        id: sourceEditor
        anchors.fill: parent
        anchors.bottomMargin: searchBar.visible ? searchBar.implicitHeight : 0
        document: root.document
        theme: root.theme
    }

    // Internal SearchBackend wired to the source editor's EditorBackend.
    SearchBackend {
        id: searchBackend
        editorBackend: sourceEditor.editorBackend
    }

    SearchBar {
        id: searchBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        searchBackend: searchBackend
        visible: false
    }

    CompletionPopup {
        id: completionPopup
        model: root.completionModel
    }

    // Global key bindings for the outer shell:
    //   Ctrl+F => toggle search bar
    //   Esc    => close popup or search bar
    Keys.onPressed: (event) => {
        if (event.modifiers & Qt.ControlModifier) {
            if (event.key === Qt.Key_F) {
                searchBar.visible = !searchBar.visible
                event.accepted = true
                return
            }
        }
        if (event.key === Qt.Key_Escape) {
            if (completionPopup.visible) {
                completionPopup.close()
                event.accepted = true
                return
            }
            if (searchBar.visible) {
                searchBar.visible = false
                event.accepted = true
                return
            }
        }
    }

}
