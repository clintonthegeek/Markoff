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

    // Scrollbar: fixes keyboard-only / pointer-only navigation gap (TODO 2026-05-02).
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    // Wire hit tester to this ListView once the component is ready.
    Component.onCompleted: {
        if (binding && binding.hitTester)
            binding.hitTester.listView = root
    }

    delegate: DelegateChooser {
        role: "kind"
        DelegateChoice { roleValue: "paragraph";  delegate: ParagraphDelegate  {} }
        DelegateChoice { roleValue: "heading";    delegate: HeadingDelegate    {} }
        DelegateChoice { roleValue: "code-block"; delegate: CodeBlockDelegate  {} }
        DelegateChoice { roleValue: "hr";         delegate: HorizontalRuleDelegate {} }
        DelegateChoice { roleValue: "image";      delegate: ImageDelegate      {} }
    }

    // ---- Keyboard: Ctrl-C copy ----
    Keys.onPressed: (event) => {
        if (!binding) { event.accepted = false; return }
        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C) {
            const sv = binding.selectionView
            if (sv && sv.hasSelection) {
                // Collect block texts from instantiated delegates.
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
            if (!binding || !binding.hitTester || !binding.selectionView) return
            const r = binding.hitTester.hit(mouse.x, mouse.y, root.width)
            mouseArea._pressResult = r
            if (!r || r.blockIndex < 0) {
                binding.selectionView.clear()
                return
            }
            binding.selectionView.begin(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
        }

        onPositionChanged: (mouse) => {
            if (!pressed || !binding || !binding.hitTester || !binding.selectionView) return
            const r = binding.hitTester.hit(mouse.x, mouse.y, root.width)
            if (r && r.blockIndex >= 0)
                binding.selectionView.extend(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
        }

        onReleased: (mouse) => {
            // On a click (no drag), collapse selection to caret.
            const press = mouseArea._pressResult
            mouseArea._pressResult = null
            if (!press || press.blockIndex < 0 || !binding || !binding.selectionView) return
            const r = binding.hitTester.hit(mouse.x, mouse.y, root.width)
            if (!r || r.blockIndex < 0) return
            const sameBlock = (r.blockIndex === press.blockIndex)
            const smallDrift = Math.abs((r.qtPos || 0) - (press.qtPos || 0)) <= 2
            if (sameBlock && smallDrift) {
                // Treat as a simple click: clear selection (leave cursor via selectionView.begin).
                binding.selectionView.begin(r.blockIndex, r.qtPos >= 0 ? r.qtPos : 0)
            }
        }
    }
}
