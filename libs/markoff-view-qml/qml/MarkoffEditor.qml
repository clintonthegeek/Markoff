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

    /// Either "source" or "live". Default: "source" (unchanged from Phase 1).
    property string mode: "source"

    /// Exposes the inner EditorBackend so the host (e.g. AstInspectorPane) can
    /// subscribe to parseUpdatedAt without going through the document directly.
    readonly property alias editorBackend: sourceEditor.editorBackend

    // PHASE-2 SEAM: SourceEditor and LiveView are siblings; `mode` toggles which
    // is visible/enabled. Both share the same EditorBackend (LiveView consumes
    // sourceEditor.editorBackend).
    SourceEditor {
        id: sourceEditor
        anchors.fill: parent
        anchors.bottomMargin: searchBar.visible ? searchBar.implicitHeight : 0
        document: root.document
        theme: root.theme
        focus: true
        visible: root.mode === "source"
        enabled: root.mode === "source"
    }
    LiveView {
        id: liveView
        anchors.fill: parent
        anchors.bottomMargin: searchBar.visible ? searchBar.implicitHeight : 0
        editorBackend: sourceEditor.editorBackend
        visible: root.mode === "live"
        enabled: root.mode === "live"
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
