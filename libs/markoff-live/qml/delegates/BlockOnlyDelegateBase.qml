// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Shared base for block-only (non-text) delegates: HR and Image.
// Owns isSelected binding, Keys.onPressed guard, takeFocus, and
// chokepoint registration (delegateAvailable / delegateGoingAway).
// Subclasses inject their content layer via the default property alias.
Item {
    id: root

    // --- Properties the delegate exposes / the view binds ---
    property int modelIndex: index
    readonly property string blockText: model ? model.text : ""
    property var blockAnchor: undefined  // captured at Component.onCompleted; stays valid through onDestruction

    readonly property var liveBinding: ListView.view ? ListView.view.binding : null
    readonly property var cursorState: liveBinding ? liveBinding.cursorState : null

    readonly property bool isSelected:
        cursorState !== null
        && cursorState.cursorKind === "BlockSelected"
        && cursorState.focusedAnchorRow === root.modelIndex

    // --- Content slot: subclasses inject their visual layer ---
    default property alias content: contentArea.data

    // --- Focus and key handling ---
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (!root.isSelected) { event.accepted = false; return }
        const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
        if (!handler) { event.accepted = false; return }
        const k = event.key
        if (k !== Qt.Key_Delete && k !== Qt.Key_Backspace
                && k !== Qt.Key_Up && k !== Qt.Key_Down
                && k !== Qt.Key_Return && k !== Qt.Key_Enter) {
            event.accepted = false; return
        }
        const handled = handler.tryHandle(k, event.modifiers, root.modelIndex,
            -1, true, blockText)
        event.accepted = handled
    }

    function takeFocus(qtPos: int) {
        root.forceActiveFocus()
    }

    // --- Chokepoint registration ---
    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        blockAnchor = model.blockAnchor
        if (cs) cs.delegateAvailable(blockAnchor, model.kind, root)
    }

    Component.onDestruction: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && blockAnchor !== undefined) cs.delegateGoingAway(blockAnchor)
    }

    // --- Layout: content goes here ---
    Item {
        id: contentArea
        anchors.fill: parent
    }
}
