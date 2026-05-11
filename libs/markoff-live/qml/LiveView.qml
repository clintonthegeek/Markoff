// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Qt.labs.qmlmodels 1.0

import "delegates"

/// Live render view: scrollable list of read-only block delegates with
/// mouse-driven cursor + selection, keyboard navigation, and action-controller shortcuts.
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
        DelegateChoice { roleValue: "list-item";  delegate: ListItemDelegate   {} }
        DelegateChoice { roleValue: "blockquote"; delegate: BlockquoteDelegate {} }
        DelegateChoice { roleValue: "math";       delegate: MathDelegate       {} }
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

    // ---- Wire navigationController.setListView on startup ----
    Component.onCompleted: {
        if (binding && binding.navigationController)
            binding.navigationController.setListView(root)
    }

    // ---- Zoom + theme shortcuts ----
    //
    // QQuickWindow has no addAction() method; QML Shortcut elements are the
    // correct window-scoped binding for QKeySequence triggers. The wired
    // QActions in LiveActionController carry the shortcut sequence (so a
    // future menu/toolbar can present them) and their slots; here we just
    // route the keystroke to action.trigger().
    //
    // Other QActions (cut/copy/paste/save/undo/redo/bold/italic/link) still
    // need analogous Shortcut wiring — currently no-ops in the standalone
    // test app; tracked separately.
    Shortcut {
        sequences: ["Ctrl+=", "Ctrl++", "Ctrl+Shift+="]
        enabled: !!root.binding && !!root.binding.actionController
        onActivated: root.binding.actionController.zoomInAction.trigger()
    }
    Shortcut {
        sequence: "Ctrl+-"
        enabled: !!root.binding && !!root.binding.actionController
        onActivated: root.binding.actionController.zoomOutAction.trigger()
    }
    Shortcut {
        sequence: "Ctrl+0"
        enabled: !!root.binding && !!root.binding.actionController
        onActivated: root.binding.actionController.zoomResetAction.trigger()
    }
    Shortcut {
        sequence: "Ctrl+Shift+D"
        enabled: !!root.binding && !!root.binding.actionController
        onActivated: root.binding.actionController.toggleDarkAction.trigger()
    }

    // ---- Remote cursor overlays (D5, geometry stub) ----
    Repeater {
        model: binding ? binding.remoteCursorsModel : null
        delegate: RemoteCursorOverlay {
            cursorColor: model.color
            cursorLabel: model.label
            liveBinding: root.binding
            x: 0; y: 0
        }
    }

    // ---- Ctrl+wheel zoom ----
    //
    // ListView (Flickable) consumes wheel events for scrolling before
    // PointerHandlers run, so WheelHandler must explicitly take grab from
    // the flickable when the Ctrl modifier is held. `acceptedModifiers`
    // gates the handler to only fire on Ctrl+wheel; plain wheel still
    // scrolls the view normally.
    WheelHandler {
        target: null
        acceptedModifiers: Qt.ControlModifier
        grabPermissions: PointerHandler.CanTakeOverFromAnything
                       | PointerHandler.ApprovesTakeOverByItems
        onWheel: (event) => {
            const steps = event.angleDelta.y / 120.0
            if (steps === 0) return
            const b = root.binding
            if (!b) return
            b.fontScale = b.fontScale * Math.pow(b.fontScaleStep, steps)
            event.accepted = true
        }
    }

    // ---- Mouse input ----
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.IBeamCursor
        preventStealing: true

        property var _pressResult: null

        // Triple-click tracking. Qt's MouseArea fires onClicked / onDoubleClicked
        // for the first two clicks; triple-click is detected by counting clicks
        // within multiClickResetTimer's interval on the same block.
        property int _clickCount: 0
        property int _clickBlock: -1
        Timer {
            id: multiClickResetTimer
            interval: 500
            onTriggered: { mouseArea._clickCount = 0; mouseArea._clickBlock = -1 }
        }

        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                const handler = binding ? binding.contextMenuHandler : null
                if (!handler) return
                const r = root.hit(mouse.x, mouse.y)
                let anchor = null
                if (r && r.blockIndex >= 0) {
                    const item = root.itemAtIndex(r.blockIndex)
                    if (item && item.blockAnchor !== undefined) anchor = item.blockAnchor
                }
                const gp = mapToGlobal(mouse.x, mouse.y)
                handler.popup(gp.x, gp.y, anchor)
                return
            }

            if (mouse.button !== Qt.LeftButton) return
            const r = root.hit(mouse.x, mouse.y)
            if (!r || r.blockIndex < 0) {
                mouseArea._clickCount = 0
                mouseArea._clickBlock = -1
                return
            }
            if (mouseArea._clickBlock !== r.blockIndex)
                mouseArea._clickCount = 0
            mouseArea._clickBlock = r.blockIndex
            mouseArea._clickCount++
            multiClickResetTimer.restart()

            if (mouseArea._clickCount >= 3) {
                // Triple-click: select whole block.
                const item = root.itemAtIndex(r.blockIndex)
                const text = (item && item.blockText !== undefined) ? item.blockText : ""
                if (binding && binding.selectionView) {
                    binding.selectionView.begin(r.blockIndex, 0)
                    binding.selectionView.extend(r.blockIndex, text.length)
                }
                mouseArea._clickCount = 0
                mouseArea._clickBlock = -1
            }
        }

        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton) return
            const r = root.hit(mouse.x, mouse.y)
            if (!r || r.blockIndex < 0 || r.qtPos < 0) return
            const item = root.itemAtIndex(r.blockIndex)
            if (!item || item.blockText === undefined) return
            const text = item.blockText
            // Word boundary at qtPos (Unicode word chars + underscore).
            const wordRe = /[\p{L}\p{N}_]/u
            let s = r.qtPos
            let e = r.qtPos
            while (s > 0 && wordRe.test(text.charAt(s - 1))) s--
            while (e < text.length && wordRe.test(text.charAt(e))) e++
            if (s === e) return  // not on a word
            if (binding && binding.selectionView) {
                binding.selectionView.begin(r.blockIndex, s)
                binding.selectionView.extend(r.blockIndex, e)
            }
            // Pre-arm triple-click counter so a third quick click selects the block.
            mouseArea._clickCount = 2
            mouseArea._clickBlock = r.blockIndex
            multiClickResetTimer.restart()
        }

        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton) return
            if (!binding || !binding.selectionView) return
            const r = root.hit(mouse.x, mouse.y)
            mouseArea._pressResult = r
            if (!r || r.blockIndex < 0) {
                root.forceActiveFocus()
                binding.selectionView.clear()
                return
            }
            binding.selectionView.begin(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
            const cs = binding ? binding.cursorState : null
            const item = root.itemAtIndex(r.blockIndex)
            if (cs && item && item.blockAnchor !== undefined)
                cs.establishFocus(item.blockAnchor, r.qtPos >= 0 ? r.qtPos : 0)
            else
                root.forceActiveFocus()
        }

        onPositionChanged: (mouse) => {
            if (mouse.button === Qt.RightButton) return
            if (!pressed || !binding || !binding.selectionView) return
            const r = root.hit(mouse.x, mouse.y)
            if (r && r.blockIndex >= 0)
                binding.selectionView.extend(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
        }

        onReleased: (mouse) => {
            if (mouse.button === Qt.RightButton) return
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
