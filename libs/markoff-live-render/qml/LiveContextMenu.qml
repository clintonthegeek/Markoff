// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live.render 1.0

Menu {
    id: root

    required property var binding

    property var _anchor: null

    function showForBlock(blockAnchor, globalPos) {
        root._anchor = blockAnchor
        root.popup(globalPos)
    }

    MenuItem {
        text: qsTr("Undo in this block")
        enabled: root._anchor !== null
                 && root.binding !== null
                 && root.binding.document !== null
                 && root.binding.document.canUndoForBlock(root._anchor)
        onTriggered: {
            if (root.binding && root.binding.document && root._anchor)
                root.binding.document.undoForBlock(root._anchor)
        }
    }

    MenuSeparator {}

    MenuItem {
        text: qsTr("Undo")
        enabled: root.binding !== null && root.binding.document !== null
        onTriggered: if (root.binding && root.binding.document) root.binding.document.undoD2()
    }
    MenuItem {
        text: qsTr("Redo")
        enabled: root.binding !== null && root.binding.document !== null
        onTriggered: if (root.binding && root.binding.document) root.binding.document.redoD2()
    }
}
