// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

Menu {
    id: root

    required property var binding

    property var _anchor: null

    function showForBlock(blockAnchor, globalPos) {
        root._anchor = blockAnchor
        root.popup(globalPos)
    }

    // Clipboard cluster
    MenuItem {
        text: binding && binding.actionController
              ? binding.actionController.cutAction.text : qsTr("Cut")
        enabled: binding && binding.actionController
                 && binding.actionController.cutAction.enabled
        onTriggered: if (binding && binding.actionController)
                         binding.actionController.cutAction.trigger()
    }
    MenuItem {
        text: binding && binding.actionController
              ? binding.actionController.copyAction.text : qsTr("Copy")
        enabled: binding && binding.actionController
                 && binding.actionController.copyAction.enabled
        onTriggered: if (binding && binding.actionController)
                         binding.actionController.copyAction.trigger()
    }
    MenuItem {
        text: binding && binding.actionController
              ? binding.actionController.pasteAction.text : qsTr("Paste")
        enabled: binding && binding.actionController
                 && binding.actionController.pasteAction.enabled
        onTriggered: if (binding && binding.actionController)
                         binding.actionController.pasteAction.trigger()
    }
    MenuItem {
        text: binding && binding.actionController
              ? binding.actionController.selectAllAction.text : qsTr("Select All")
        enabled: binding && binding.actionController
                 && binding.actionController.selectAllAction.enabled
        onTriggered: if (binding && binding.actionController)
                         binding.actionController.selectAllAction.trigger()
    }

    MenuSeparator {}

    MenuItem {
        text: binding && binding.actionController
              ? binding.actionController.undoAction.text : qsTr("Undo")
        enabled: binding && binding.actionController
                 && binding.actionController.undoAction.enabled
        onTriggered: if (binding && binding.actionController)
                         binding.actionController.undoAction.trigger()
    }
    MenuItem {
        text: binding && binding.actionController
              ? binding.actionController.redoAction.text : qsTr("Redo")
        enabled: binding && binding.actionController
                 && binding.actionController.redoAction.enabled
        onTriggered: if (binding && binding.actionController)
                         binding.actionController.redoAction.trigger()
    }

    MenuSeparator {}

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
}
