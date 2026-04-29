// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.markoff.view.qml

/// Outer shell. Phase-1 wraps a single SourceEditor; Phase-2 will sibling it
/// with a LiveEditor that consumes the same EditorBackend via the same
/// MarkoffDocument + Session.
///
/// External property contract:
///   - document         : Markoff.MarkoffDocument *
///   - theme            : Markoff.Theme
///   - completionModel  : CompletionPopupModel  (optional — host-supplied so
///                        emoji + future tag/wikilink providers can be wired in)
///
/// This is the architectural seam. Read libs/markoff-view-qml/CLAUDE.md (T23)
/// for the Phase-2 plan.
Item {
    id: root

    property var document: null
    property var theme
    property var completionModel: null

    // The single Phase-1 source editor. Phase-2 may toggle to a LiveEditor here.
    SourceEditor {
        id: sourceEditor
        anchors.fill: parent
        anchors.bottomMargin: searchBar.visible ? searchBar.implicitHeight : 0
        document: root.document
        theme: root.theme
        focus: true
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

    // Phase-2 placeholder:
    //   To add LiveEditor as a sibling, create a LiveEditor.qml that consumes
    //   the same EditorBackend (sourceEditor.editorBackend.document, .session),
    //   then introduce a `mode` property here that StackLayout-toggles between
    //   sourceEditor and liveEditor. The seam is intentional — no other file
    //   needs to change.
}
