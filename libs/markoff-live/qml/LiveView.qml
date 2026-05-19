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
Item {
    id: root

    required property var binding   // LiveListModelBinding *

    readonly property alias listView: listView
    readonly property alias findBar:  findBar

    FindBar {
        id: findBar
        liveBinding: root.binding
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
    }

    ListView {
        id: listView

        anchors.top: findBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        // Expose `binding` so delegates can access it via `ListView.view.binding`.
        // Delegates use `ListView.view.binding` (not `root.binding`) because they
        // access the nearest ListView ancestor via the attached ListView.view property.
        property var binding: root.binding

        // Set to true after the first initial-focus seed fires, so onCountChanged
        // does not re-fire the seed on subsequent structural changes (enter, paste, etc.).
        property bool _initialFocusSeeded: false

        model: root.binding ? root.binding.model : null
        clip: true
        spacing: 2
        focus: true

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: DelegateChooser {
            role: "delegateClass"
            DelegateChoice { roleValue: "text-inline"; delegate: UnifiedInlineTextDelegate {} }
            DelegateChoice { roleValue: "code-block";  delegate: CodeBlockDelegate         {} }
            DelegateChoice { roleValue: "math";        delegate: MathDelegate              {} }
            DelegateChoice { roleValue: "hr";          delegate: HorizontalRuleDelegate    {} }
            DelegateChoice { roleValue: "image";       delegate: ImageDelegate             {} }
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
            const lastIdx = listView.count - 1
            if (lastIdx < 0) return null

            const clampedX = Math.max(0, Math.min(mouseX, listView.width - 1))
            const clampedY = Math.max(0, Math.min(mouseY, listView.height - 1))
            const cx = clampedX + listView.contentX
            const cy = clampedY + listView.contentY
            const probeX = listView.width / 2

            function clampedLocalX(item, contentCx) {
                return Math.max(0, Math.min(contentCx - item.x, item.width - 1))
            }

            function hitItem(item, localX, localY) {
                const qtPos = (typeof item.positionAt === "function")
                              ? item.positionAt(localX, localY) : -1
                return { blockIndex: item.modelIndex, qtPos: qtPos }
            }

            // 1. Direct hit on a realized delegate.
            const direct = listView.itemAt(probeX, cy)
            if (direct) return hitItem(direct, clampedLocalX(direct, cx), cy - direct.y)

            // 2. Walk outward to the nearest realized delegate. Walk radius is
            //    generous (viewport height) so we cross trailing empty space and
            //    multi-row delegate gaps without giving up.
            const maxWalk = Math.max(listView.height, 1024)
            let aboveItem = null, belowItem = null, aboveDy = 0, belowDy = 0
            for (let dy = 4; dy <= maxWalk; dy += 4) {
                if (!aboveItem && cy - dy >= 0) {
                    const a = listView.itemAt(probeX, cy - dy)
                    if (a) { aboveItem = a; aboveDy = dy }
                }
                if (!belowItem && cy + dy < listView.contentHeight) {
                    const b = listView.itemAt(probeX, cy + dy)
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
            if (root.binding && root.binding.navigationController)
                root.binding.navigationController.setListView(listView)
        }

        // ---- Seed initial focus through the chokepoint on first model population ----
        //
        // Without this, ListView.focus = true delivers focus to the delegate root
        // but not the text-bearing TextEdit descendant — the path every other
        // event recovers via establishFocus/takeFocus, but startup didn't.
        // Discipline-log entry 2026-05-16 (closed by tier-4b).
        //
        // Why not Component.onCompleted: at that point the model is empty
        // (documentLoaded fired before the binding was wired), so
        // requestTextCaretAtRow(0, 0) early-returns.
        //
        // Why not guard on cursorKind === "none": the TextEdit's
        // onCursorPositionChanged fires during delegate creation and calls
        // syncFromTextEdit, which sets cursorKind to TextCaret before the
        // actual window focus lands on the TextEdit. The _initialFocusSeeded
        // flag guards against re-firing on subsequent structural changes
        // (enter, paste, etc.) while ensuring we fire exactly once on load.
        onCountChanged: {
            if (!listView._initialFocusSeeded && count > 0 && root.binding && root.binding.cursorState) {
                listView._initialFocusSeeded = true
                root.binding.cursorState.requestTextCaretAtRow(0, 0)
            }
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
        Shortcut {
            sequence: "Ctrl+F"
            enabled: !!root.binding && !!root.binding.findController
            onActivated: root.binding.showFindBar()
        }

        // ---- Remote cursor overlays (D5, geometry stub) ----
        Repeater {
            model: root.binding ? root.binding.remoteCursorsModel : null
            delegate: RemoteCursorOverlay {
                cursorColor: model.color
                cursorLabel: model.label
                liveBinding: root.binding
                x: 0; y: 0
            }
        }

        // ---- Ctrl+wheel zoom ----
        //
        // ListView (Flickable) consumes wheel events for scrolling. WheelHandler
        // with `target: null` + `grabPermissions: CanTakeOverFromAnything` does
        // not reliably win the grab against Flickable's built-in wheelEvent on
        // Linux (Wayland in particular). A MouseArea with `acceptedButtons:
        // Qt.NoButton` + explicit `wheel.accepted = (modifiers & Control)` is the
        // proven pattern: it sees the wheel first, consumes it on Ctrl+wheel, and
        // forwards plain wheels back to the Flickable for scrolling.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            propagateComposedEvents: true
            z: 10
            onWheel: (wheel) => {
                if (!(wheel.modifiers & Qt.ControlModifier)) {
                    wheel.accepted = false
                    return
                }
                const steps = wheel.angleDelta.y / 120.0
                if (steps === 0) { wheel.accepted = true; return }
                const b = root.binding
                if (!b) { wheel.accepted = false; return }
                b.fontScale = b.fontScale * Math.pow(b.fontScaleStep, steps)
                wheel.accepted = true
            }
        }

        // ---- Mouse input ----
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.IBeamCursor
            hoverEnabled: true
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
                    const handler = root.binding ? root.binding.contextMenuHandler : null
                    if (!handler) return
                    const r = listView.hit(mouse.x, mouse.y)
                    let anchor = null
                    if (r && r.blockIndex >= 0) {
                        const item = listView.itemAtIndex(r.blockIndex)
                        if (item && item.blockAnchor !== undefined) anchor = item.blockAnchor
                    }
                    const gp = mapToGlobal(mouse.x, mouse.y)
                    handler.popup(gp.x, gp.y, anchor)
                    return
                }

                if (mouse.button !== Qt.LeftButton) return

                // Ctrl+click: activate link at the clicked position.
                if ((mouse.modifiers & Qt.ControlModifier) && root.binding) {
                    const r = listView.hit(mouse.x, mouse.y)
                    if (r && r.blockIndex >= 0 && r.qtPos >= 0) {
                        const item = listView.itemAtIndex(r.blockIndex)
                        if (item && item.blockAnchor !== undefined)
                            root.binding.activateLinkAt(item.blockAnchor, r.qtPos,
                                                   mouse.modifiers)
                    }
                    return
                }

                const r = listView.hit(mouse.x, mouse.y)
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
                    const item = listView.itemAtIndex(r.blockIndex)
                    const text = (item && item.blockText !== undefined) ? item.blockText : ""
                    if (root.binding && root.binding.cursorState) {
                        root.binding.cursorState.begin(r.blockIndex, 0)
                        root.binding.cursorState.extend(r.blockIndex, text.length)
                    }
                    mouseArea._clickCount = 0
                    mouseArea._clickBlock = -1
                }
            }

            onDoubleClicked: (mouse) => {
                if (mouse.button !== Qt.LeftButton) return
                const r = listView.hit(mouse.x, mouse.y)
                if (!r || r.blockIndex < 0 || r.qtPos < 0) return
                const item = listView.itemAtIndex(r.blockIndex)
                if (!item || item.blockText === undefined) return
                const text = item.blockText
                // Word boundary at qtPos (Unicode word chars + underscore).
                const wordRe = /[\p{L}\p{N}_]/u
                let s = r.qtPos
                let e = r.qtPos
                while (s > 0 && wordRe.test(text.charAt(s - 1))) s--
                while (e < text.length && wordRe.test(text.charAt(e))) e++
                if (s === e) return  // not on a word
                if (root.binding && root.binding.cursorState) {
                    root.binding.cursorState.begin(r.blockIndex, s)
                    root.binding.cursorState.extend(r.blockIndex, e)
                }
                // Pre-arm triple-click counter so a third quick click selects the block.
                mouseArea._clickCount = 2
                mouseArea._clickBlock = r.blockIndex
                multiClickResetTimer.restart()
            }

            onPressed: (mouse) => {
                if (mouse.button === Qt.RightButton) return
                if (!root.binding || !root.binding.cursorState) return
                const r = listView.hit(mouse.x, mouse.y)
                mouseArea._pressResult = r
                if (!r || r.blockIndex < 0) {
                    listView.forceActiveFocus()
                    root.binding.cursorState.clearSelection()
                    return
                }
                root.binding.cursorState.begin(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
                const cs = root.binding ? root.binding.cursorState : null
                const item = listView.itemAtIndex(r.blockIndex)
                if (cs && item && item.blockAnchor !== undefined)
                    cs.establishFocus(item.blockAnchor, r.qtPos >= 0 ? r.qtPos : 0)
                else
                    listView.forceActiveFocus()
            }

            onPositionChanged: (mouse) => {
                if (mouse.button === Qt.RightButton) return

                // Ctrl-hover: update cursor shape and notify link service.
                if (!pressed && root.binding && (mouse.modifiers & Qt.ControlModifier)) {
                    const r = listView.hit(mouse.x, mouse.y)
                    if (r && r.blockIndex >= 0 && r.qtPos >= 0) {
                        const item = listView.itemAtIndex(r.blockIndex)
                        if (item && item.blockAnchor !== undefined) {
                            const gp = mapToGlobal(mouse.x, mouse.y)
                            const hit = root.binding.hoverLinkAt(item.blockAnchor, r.qtPos,
                                                            mouse.modifiers,
                                                            Qt.point(gp.x, gp.y))
                            mouseArea.cursorShape = hit ? Qt.PointingHandCursor : Qt.IBeamCursor
                            return
                        }
                    }
                    // Ctrl held but no link under cursor — clear hover.
                    root.binding.clearLinkHover()
                    mouseArea.cursorShape = Qt.IBeamCursor
                    return
                }

                // Non-Ctrl hover: clear any stale link hover and reset cursor.
                if (!pressed && root.binding) {
                    root.binding.clearLinkHover()
                    mouseArea.cursorShape = Qt.IBeamCursor
                    return
                }

                if (!pressed || !root.binding || !root.binding.cursorState) return
                const r = listView.hit(mouse.x, mouse.y)
                if (r && r.blockIndex >= 0)
                    root.binding.cursorState.extend(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
            }

            onExited: {
                if (root.binding)
                    root.binding.clearLinkHover()
                mouseArea.cursorShape = Qt.IBeamCursor
            }

            onReleased: (mouse) => {
                if (mouse.button === Qt.RightButton) return
                const press = mouseArea._pressResult
                mouseArea._pressResult = null
                if (!press || press.blockIndex < 0 || !root.binding || !root.binding.cursorState) return
                const r = listView.hit(mouse.x, mouse.y)
                if (!r || r.blockIndex < 0) return
                const sameBlock = (r.blockIndex === press.blockIndex)
                const smallDrift = Math.abs((r.qtPos || 0) - (press.qtPos || 0)) <= 2
                if (sameBlock && smallDrift) {
                    // Simple click: reset to caret (no drag selection).
                    root.binding.cursorState.begin(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
                }
            }
        }
    }
}
