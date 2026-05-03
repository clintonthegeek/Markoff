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

    model: binding ? binding.proxyModel : null
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
    //
    // Resolution order:
    //   1. Direct hit on a realized delegate at the click's content-Y.
    //   2. Walk outward from cy in both directions until we find a realized
    //      delegate, snapping to its top or bottom edge.
    // The walk uniformly handles three cases that previously needed special
    // branches: clicks above the first realized delegate, clicks in gaps
    // between realized delegates (when ListView has recycled them across a
    // tall row), and clicks below the last realized delegate (which
    // historically returned the wrong block when ListView's contentHeight
    // estimate omitted unrealized trailing rows — see commit history).
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

        // 1. Direct hit on a realized delegate.
        const direct = root.itemAt(probeX, cy)
        if (direct) return hitItem(direct, clampedLocalX(direct, cx), cy - direct.y)

        // 2. Walk outward to the nearest realized delegate. Walk radius is
        //    generous (viewport height) so we cross trailing empty space and
        //    multi-row delegate gaps without giving up.
        const maxWalk = Math.max(root.height, 1024)
        let aboveItem = null, belowItem = null, aboveDy = 0, belowDy = 0
        for (let dy = 4; dy <= maxWalk; dy += 4) {
            if (!aboveItem && cy - dy >= 0) {
                const a = root.itemAt(probeX, cy - dy)
                if (a) { aboveItem = a; aboveDy = dy }
            }
            if (!belowItem && cy + dy < root.contentHeight) {
                const b = root.itemAt(probeX, cy + dy)
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
            if (!binding || !binding.selectionView) return
            const r = root.hit(mouse.x, mouse.y)
            mouseArea._pressResult = r
            if (!r || r.blockIndex < 0) {
                root.forceActiveFocus()
                binding.selectionView.clear()
                return
            }
            binding.selectionView.begin(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
            // Route keyboard focus into the matched delegate's TextEdit so
            // typing reaches it (R4). The MouseArea has preventStealing:true
            // and consumes the press, so the TextEdit's native click-to-focus
            // path doesn't run; we focus it explicitly.
            const item = root.itemAtIndex(r.blockIndex)
            if (item && item.focusEditAt)
                item.focusEditAt(r.qtPos >= 0 ? r.qtPos : 0)
            else
                root.forceActiveFocus()
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
                // Re-confirm focus + caret position on the matched delegate
                // (the press's focusEditAt may have been pre-empted by the
                // MouseArea event chain on some platforms).
                const item = root.itemAtIndex(r.blockIndex)
                if (item && item.focusEditAt)
                    item.focusEditAt(r.qtPos >= 0 ? r.qtPos : 0)
            }
        }
    }
}
