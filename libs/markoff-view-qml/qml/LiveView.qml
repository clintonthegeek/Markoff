// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Qt.labs.qmlmodels

import org.markoff.view.qml

import "delegates"

/// Read-only Live render of a markdown document. Sibling of SourceEditor.qml
/// inside MarkoffEditor.qml.
///
/// External property contract:
///   - editorBackend : EditorBackend *  (required; same one as Source mode)
Item {
    id: root

    property var editorBackend  // EditorBackend *
    property var theme   // Markoff::Theme value type; null → delegates fall back to hex defaults

    // Grab focus when the document is empty so the first keystroke is received here.
    focus: listView.count === 0

    // Set to true when insertFirstCharacter has been called; cleared once we
    // redirect focus into the freshly-created delegate.
    property bool m_firstInsertPending: false

    // Length (in UTF-16 code units) of the text inserted via insertFirstCharacter.
    // Used to position the cursor correctly after the delegate materialises.
    property int m_firstInsertLength: 0

    Keys.onPressed: (event) => {
        if (listView.count !== 0) { event.accepted = false; return }
        const text = event.text
        if (text.length === 0 || text.charCodeAt(0) < 32) { event.accepted = false; return }
        if (!root.editorBackend) { event.accepted = false; return }
        structuralKeys.insertFirstCharacter(text)
        root.m_firstInsertPending = true
        root.m_firstInsertLength = text.length
        event.accepted = true
    }

    LiveStructuralKeyHandler {
        id: structuralKeys
        document: root.editorBackend ? root.editorBackend.document : null
        model: binding.model
        projectionLayer: binding.projectionLayer
    }

    // Hole reification / drop → focus routing. The layer fires holeReified
    // once `applyLocalEdit` produces the new real row, and holeDropped when
    // a pending hole is abandoned without source mutation.
    //
    // NOTE: the spec's "Enter on non-empty hole opens a new hole below the
    // just-committed paragraph" chain is not implemented here. After a
    // commit the user lands at end of the new real paragraph; pressing
    // Enter again triggers the standard EOB path which opens a fresh hole.
    // The two-keystroke path is correct; the one-keystroke chain is a
    // follow-up nicety.
    Connections {
        target: binding.projectionLayer
        function onHoleReified(viewRow, qtPos) {
            const item = listView.itemAtIndex(viewRow)
            if (item && typeof item.focusAtPos === "function") {
                item.focusAtPos(qtPos)
            } else {
                Qt.callLater(function() {
                    const it = listView.itemAtIndex(viewRow)
                    if (it && typeof it.focusAtPos === "function") it.focusAtPos(qtPos)
                })
            }
        }
        function onHoleCreated(viewRow) {
            // Two reasons we MUST defer + verify here:
            //   (1) onHoleCreated runs synchronously inside the keyPressEvent
            //       that triggered the hole; ListView has not yet incubated
            //       the new delegate, so itemAtIndex(viewRow) returns the
            //       previously-incubated delegate at that visual position
            //       (the paragraph that was at viewRow before the insert).
            //       Calling focusAtStart on that wrong delegate lands the
            //       user's next keystroke at the start of the WRONG row.
            //       (Dogfood-surfaced: "t at start of next paragraph".)
            //   (2) After callLater fires, verify item.isHole === true so
            //       a stale item from before the insert never receives
            //       focus.
            // Mirrors the empty-doc m_firstInsertPending pattern's two
            // levels of Qt.callLater.
            let attempts = 0
            function tryFocus() {
                attempts++
                const item = listView.itemAtIndex(viewRow)
                if (item && item.isHole === true
                    && typeof item.focusAtStart === "function") {
                    item.focusAtStart()
                    return
                }
                if (attempts < 10) Qt.callLater(tryFocus)
            }
            Qt.callLater(function() { Qt.callLater(tryFocus) })
        }
        function onHoleDropped(prevViewRow) {
            const targetRow = prevViewRow - 1
            if (targetRow < 0) return
            const item = listView.itemAtIndex(targetRow)
            if (item && typeof item.focusAtEnd === "function") item.focusAtEnd()
        }
    }

    // Mid-block Enter focus routing. The structural-key handler emits this
    // signal after applying the "\n\n" split edit; the new "second half"
    // row appears at `viewRow` once the parse returns. Mirrors the
    // holeReified pattern (two Qt.callLater + bounded retry) since the
    // delegate may not be incubated yet when the signal fires.
    Connections {
        target: structuralKeys
        function onFocusAfterStructuralEdit(viewRow, qtPos) {
            let attempts = 0
            function tryFocus() {
                attempts++
                const item = listView.itemAtIndex(viewRow)
                if (item && typeof item.focusAtPos === "function") {
                    item.focusAtPos(qtPos)
                    return
                }
                if (attempts < 10) Qt.callLater(tryFocus)
            }
            Qt.callLater(function() { Qt.callLater(tryFocus) })
        }
    }

    LiveSpeculativeFenceController {
        id: fenceCtrl
        model: binding.model
        projectionLayer: binding.projectionLayer
    }

    LiveListModelBinding {
        id: binding
        editorBackend: root.editorBackend
    }

    LiveClipboardController {
        id: clipboard
        selectionModel: binding.selectionModel
        blockModel: binding.model
    }

    LiveContextMenuHandler {
        id: ctxMenu
        selectionModel: binding.selectionModel
        clipboardController: clipboard
        blockTexts: {
            const out = []
            const count = binding.model ? binding.model.rowCount() : 0
            for (let i = 0; i < count; ++i) {
                const idx = binding.model.index(i, 0)
                out.push(binding.model.data(idx, binding.model.roleForName("text")))
            }
            return out
        }
        blockCount: binding.model ? binding.model.rowCount() : 0
    }

    ListView {
        id: listView
        objectName: "listView"
        anchors.fill: parent
        clip: true
        spacing: 12

        model: binding.model

        onCountChanged: {
            if (listView.count >= 1 && root.m_firstInsertPending) {
                root.m_firstInsertPending = false
                const targetPos = root.m_firstInsertLength
                root.m_firstInsertLength = 0
                // Two Qt.callLater levels: first to let the delegate
                // materialise, second to let blockText sync to the TextEdit
                // before we position the cursor.
                Qt.callLater(function() {
                    Qt.callLater(function() {
                        const item = listView.itemAtIndex(0)
                        if (!item) return
                        if (typeof item.focusAtPos === "function")
                            item.focusAtPos(targetPos)
                        else if (typeof item.focusAtEnd === "function")
                            item.focusAtEnd()
                    })
                })
            }
        }

        delegate: DelegateChooser {
            role: "kind"

            DelegateChoice {
                roleValue: "paragraph"
                ParagraphDelegate {
                    blockIndex: index
                    blockText: model.text
                    blockAnchor: model.blockAnchor
                    document: root.editorBackend ? root.editorBackend.document : null
                    selectionModel: binding.selectionModel
                    theme: root.theme
                    structuralKeyHandler: structuralKeys
                    modelBinding: binding
                    fenceController: fenceCtrl

                    isHole: model.isHole === true
                    holeId: model.holeId
                    projectionLayer: binding.projectionLayer
                }
            }
            DelegateChoice {
                roleValue: "heading"
                HeadingDelegate {
                    blockIndex: index
                    blockText: model.text
                    headingLevel: model.headingLevel
                    blockAnchor: model.blockAnchor
                    document: root.editorBackend ? root.editorBackend.document : null
                    selectionModel: binding.selectionModel
                    theme: root.theme
                    structuralKeyHandler: structuralKeys
                    modelBinding: binding
                    fenceController: fenceCtrl
                }
            }
            DelegateChoice {
                roleValue: "hr"
                HorizontalRuleDelegate {
                    blockIndex: index
                    selectionModel: binding.selectionModel
                    theme: root.theme
                    routeFocusToNeighbour: root.routeNeighbourFocus
                }
            }
            DelegateChoice {
                roleValue: "image"
                ImageDelegate {
                    blockIndex: index
                    imageSrc: model.imageSrc
                    imageAlt: model.imageAlt
                    imageTitle: model.imageTitle
                    selectionModel: binding.selectionModel
                    theme: root.theme
                    routeFocusToNeighbour: root.routeNeighbourFocus
                }
            }
            DelegateChoice {
                roleValue: "code_block"
                CodeBlockDelegate {
                    blockIndex: index
                    codeLanguage: model.codeLanguage
                    // Source-faithful text (Stage C-2/C-3): includes the
                    // surrounding fences. The TextEdit shows fences inline
                    // so the user can see and edit them directly.
                    blockText: model.text
                    blockAnchor: model.blockAnchor
                    document: root.editorBackend ? root.editorBackend.document : null
                    selectionModel: binding.selectionModel
                    theme: root.theme
                    structuralKeyHandler: structuralKeys
                    modelBinding: binding
                    fenceController: fenceCtrl
                }
            }
        }

        // ---- selection input layer ----
        // Spike-validated hit() pipeline. DO NOT modify the math. See
        // docs/specs/2026-04-29-cross-block-selection-spike-findings.md §3.
        MouseArea {
            anchors.fill: parent
            z: -1               // delegates sit above; they receive clicks first
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.IBeamCursor
            hoverEnabled: false
            preventStealing: true   // stop ListView's flickable from stealing drags

            function clampedLocalX(item, contentX) {
                return Math.max(0, Math.min(contentX - item.x, item.width - 1))
            }

            function blockTextOf(idx) {
                if (!binding.model) return ""
                if (idx < 0 || idx >= binding.model.rowCount()) return ""
                return binding.model.data(binding.model.index(idx, 0),
                                          binding.model.roleForName("text"))
            }

            function hit(mouseX, mouseY) {
                const lastIdx = listView.count - 1

                const clampedX = Math.max(0, Math.min(mouseX, width - 1))
                const clampedY = Math.max(0, Math.min(mouseY, height - 1))
                const cx = clampedX + listView.contentX
                const cy = clampedY + listView.contentY
                const probeX = listView.width / 2

                if (cy >= listView.contentHeight) {
                    const probe = listView.itemAt(probeX, listView.contentHeight - 1)
                    if (probe) {
                        const localY = Math.max(0, probe.height - 1)
                        return { block: (probe.blockIndex !== undefined ? probe.blockIndex : lastIdx),
                                 offset: probe.positionAt
                                     ? probe.positionAt(clampedLocalX(probe, cx), localY)
                                     : blockTextOf(lastIdx).length }
                    }
                    return { block: lastIdx, offset: blockTextOf(lastIdx).length }
                }
                if (cy < 0) {
                    return { block: 0, offset: 0 }
                }

                const item = listView.itemAt(probeX, cy)
                if (item) {
                    const localY = cy - item.y
                    const offset = item.positionAt
                        ? item.positionAt(clampedLocalX(item, cx), localY)
                        : 0
                    return { block: (item.blockIndex !== undefined ? item.blockIndex : 0), offset: offset }
                }

                let aboveItem = null, aboveDy = 0
                let belowItem = null, belowDy = 0
                for (let dy = 4; dy < 64; dy += 4) {
                    if (!aboveItem) {
                        const a = listView.itemAt(probeX, Math.max(0, cy - dy))
                        if (a) { aboveItem = a; aboveDy = dy }
                    }
                    if (!belowItem) {
                        const b = listView.itemAt(probeX, Math.min(listView.contentHeight - 1, cy + dy))
                        if (b) { belowItem = b; belowDy = dy }
                    }
                    if (aboveItem && belowItem) break
                }
                if (aboveItem && (!belowItem || aboveDy <= belowDy)) {
                    const localY = Math.max(0, aboveItem.height - 1)
                    const offset = aboveItem.positionAt
                        ? aboveItem.positionAt(clampedLocalX(aboveItem, cx), localY)
                        : blockTextOf(aboveItem.index).length
                    return { block: (aboveItem.blockIndex !== undefined ? aboveItem.blockIndex : 0), offset: offset }
                }
                if (belowItem) {
                    const offset = belowItem.positionAt
                        ? belowItem.positionAt(clampedLocalX(belowItem, cx), 0)
                        : 0
                    return { block: (belowItem.blockIndex !== undefined ? belowItem.blockIndex : 0), offset: offset }
                }
                return null
            }

            onPressed: (m) => {
                if (m.button === Qt.RightButton) {
                    const globalPos = mapToGlobal(m.x, m.y)
                    ctxMenu.popup(Qt.point(globalPos.x, globalPos.y))
                    return
                }
                const h = hit(m.x, m.y)
                if (h) {
                    binding.selectionModel.begin(h.block, h.offset)
                } else {
                    binding.selectionModel.clear()
                }
            }
            onPositionChanged: (m) => {
                if (!pressed || (m.buttons & Qt.LeftButton) === 0) return
                const h = hit(m.x, m.y)
                if (h) binding.selectionModel.extend(h.block, h.offset)
            }
        }
    }

    /// Route keyboard focus from a non-text delegate (HR, image) to the nearest
    /// text delegate. Searches backward first (preceding block), then forward
    /// (following block). Text delegates are identified by having `focusAtEnd`.
    /// `fromIndex` is the block index of the non-text delegate that was clicked.
    function routeNeighbourFocus(fromIndex) {
        const total = binding.model ? binding.model.rowCount() : 0
        // Preceding text delegate.
        for (let i = fromIndex - 1; i >= 0; --i) {
            const item = listView.itemAtIndex(i)
            if (item && typeof item.focusAtEnd === "function") {
                item.focusAtEnd()
                return
            }
        }
        // No preceding text — try following.
        for (let i = fromIndex + 1; i < total; ++i) {
            const item = listView.itemAtIndex(i)
            if (item && typeof item.focusAtStart === "function") {
                item.focusAtStart()
                return
            }
        }
    }

    // Ctrl+C copies the current selection.
    Shortcut {
        sequence: StandardKey.Copy
        onActivated: clipboard.copy()
    }

    // Ctrl+X cuts the current selection.
    Shortcut {
        sequence: StandardKey.Cut
        onActivated: clipboard.cut()
    }

    // Ctrl+V pastes clipboard text at the cursor / over the selection.
    Shortcut {
        sequence: StandardKey.Paste
        onActivated: clipboard.paste()
    }
}
