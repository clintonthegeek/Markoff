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

    LiveStructuralKeyHandler {
        id: structuralKeys
        document: root.editorBackend ? root.editorBackend.document : null
        model: binding.model
    }

    LiveListModelBinding {
        id: binding
        editorBackend: root.editorBackend
    }

    LiveContextMenuHandler {
        id: ctxMenu
        selectionModel: binding.selectionModel
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
                }
            }
            DelegateChoice {
                roleValue: "hr"
                HorizontalRuleDelegate {
                    blockIndex: index
                    selectionModel: binding.selectionModel
                    theme: root.theme
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
                }
            }
            DelegateChoice {
                roleValue: "code_block"
                CodeBlockDelegate {
                    blockIndex: index
                    codeLanguage: model.codeLanguage
                    codeText: model.codeText
                    blockAnchor: model.blockAnchor
                    document: root.editorBackend ? root.editorBackend.document : null
                    selectionModel: binding.selectionModel
                    theme: root.theme
                    structuralKeyHandler: structuralKeys
                    modelBinding: binding
                }
            }
        }

        // ---- selection input layer ----
        // Spike-validated hit() pipeline. DO NOT modify the math. See
        // docs/specs/2026-04-29-cross-block-selection-spike-findings.md §3.
        MouseArea {
            anchors.fill: parent
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

    // Ctrl+C copies the current selection.
    Shortcut {
        sequence: StandardKey.Copy
        onActivated: {
            if (!binding.selectionModel.hasSelection) return
            const out = []
            const count = binding.model.rowCount()
            for (let i = 0; i < count; ++i) {
                const idx = binding.model.index(i, 0)
                out.push(binding.model.data(idx, binding.model.roleForName("text")))
            }
            binding.selectionModel.copySelectionToClipboard(out)
        }
    }
}
