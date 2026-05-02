// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import Qt.labs.qmlmodels 1.0

import "delegates"

/// Live render view: scrollable list of read-only block delegates with
/// mouse-driven cursor + selection, keyboard navigation, and Ctrl-C copy.
///
/// Usage:
///   LiveListModelBinding { id: binding; document: ctxDocument }
///   LiveView { anchors.fill: parent; binding: binding }
ListView {
    id: root

    required property var binding   // LiveListModelBinding *

    model: binding ? binding.model : null
    clip: true
    spacing: 2
    focus: true

    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    delegate: DelegateChooser {
        role: "kind"
        DelegateChoice { roleValue: "paragraph";  delegate: ParagraphDelegate  {} }
        DelegateChoice { roleValue: "heading";    delegate: HeadingDelegate    {} }
        DelegateChoice { roleValue: "code-block"; delegate: CodeBlockDelegate  {} }
        DelegateChoice { roleValue: "hr";         delegate: HorizontalRuleDelegate {} }
        DelegateChoice { roleValue: "image";      delegate: ImageDelegate      {} }
    }

    // ---- Hit-test (ported from .spike/cross-block-selection/Main.qml) ----
    // Returns {blockIndex, qtPos} or null on miss.
    // blockIndex is the delegate's modelIndex; qtPos is -1 for non-text blocks.
    function hit(mouseX, mouseY) {
        const lastIdx = root.count - 1
        if (lastIdx < 0) return null

        const clampedX = Math.max(0, Math.min(mouseX, root.width - 1))
        const clampedY = Math.max(0, Math.min(mouseY, root.height - 1))
        const cx = clampedX + root.contentX
        const cy = clampedY + root.contentY
        const probeX = root.width / 2

        function clampedLocalX(item, contentCx) {
            return Math.max(0, Math.min(contentCx - item.x, item.width - 1))
        }

        function hitItem(item, localX, localY) {
            const qtPos = (typeof item.positionAt === "function")
                          ? item.positionAt(localX, localY) : -1
            return { blockIndex: item.modelIndex, qtPos: qtPos }
        }

        if (cy >= root.contentHeight) {
            const probe = root.itemAt(probeX, root.contentHeight - 1)
            if (probe) return hitItem(probe, clampedLocalX(probe, cx), probe.height - 1)
            return { blockIndex: lastIdx, qtPos: -1 }
        }
        if (cy < 0) return { blockIndex: 0, qtPos: 0 }

        const item = root.itemAt(probeX, cy)
        if (item) return hitItem(item, clampedLocalX(item, cx), cy - item.y)

        // In gap between delegates: walk to find nearest border.
        let aboveItem = null, belowItem = null, aboveDy = 0, belowDy = 0
        for (let dy = 4; dy < 64; dy += 4) {
            if (!aboveItem) {
                const a = root.itemAt(probeX, Math.max(0, cy - dy))
                if (a) { aboveItem = a; aboveDy = dy }
            }
            if (!belowItem) {
                const b = root.itemAt(probeX, Math.min(root.contentHeight - 1, cy + dy))
                if (b) { belowItem = b; belowDy = dy }
            }
            if (aboveItem && belowItem) break
        }
        if (aboveItem && (!belowItem || aboveDy <= belowDy))
            return hitItem(aboveItem, clampedLocalX(aboveItem, cx), aboveItem.height - 1)
        if (belowItem)
            return hitItem(belowItem, clampedLocalX(belowItem, cx), 0)
        return null
    }

    // ---- Keyboard: Ctrl-C copy ----
    Keys.onPressed: (event) => {
        if (!binding) { event.accepted = false; return }
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C) {
            const sv = binding.selectionView
            if (sv && sv.hasSelection) {
                const texts = []
                for (let i = 0; i < root.count; ++i) {
                    const it = root.itemAtIndex(i)
                    texts.push(it ? (it.blockText || "") : "")
                }
                sv.copyToClipboard(texts)
                event.accepted = true
                return
            }
        }
        event.accepted = false
    }

    // ---- Mouse input ----
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.IBeamCursor
        preventStealing: true

        property var _pressResult: null

        onPressed: (mouse) => {
            root.forceActiveFocus()
            if (!binding || !binding.selectionView) return
            const r = root.hit(mouse.x, mouse.y)
            mouseArea._pressResult = r
            if (!r || r.blockIndex < 0) {
                binding.selectionView.clear()
                return
            }
            binding.selectionView.begin(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
        }

        onPositionChanged: (mouse) => {
            if (!pressed || !binding || !binding.selectionView) return
            const r = root.hit(mouse.x, mouse.y)
            if (r && r.blockIndex >= 0)
                binding.selectionView.extend(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
        }

        onReleased: (mouse) => {
            const press = mouseArea._pressResult
            mouseArea._pressResult = null
            if (!press || press.blockIndex < 0 || !binding || !binding.selectionView) return
            const r = root.hit(mouse.x, mouse.y)
            if (!r || r.blockIndex < 0) return
            const sameBlock = (r.blockIndex === press.blockIndex)
            const smallDrift = Math.abs((r.qtPos || 0) - (press.qtPos || 0)) <= 2
            if (sameBlock && smallDrift) {
                // Simple click: reset to caret (no drag selection).
                binding.selectionView.begin(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
            }
        }
    }
}
