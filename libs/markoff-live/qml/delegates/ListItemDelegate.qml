// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls
import org.markoff.live 1.0

/// Per-item ListItem delegate. Marker is rendered as a non-editable
/// label (or task-list checkbox) to the left of the TextEdit, populated
/// from model.markerStyle / model.markerNumber / model.checked. Indent
/// is rendered as left padding (3 spaces of width per indent level).
/// Buffer holds content only; the marker is reconstructed at serialize.
Item {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: edit.implicitHeight

    property int modelIndex: index
    readonly property string blockText: model.text

    readonly property var liveBinding:
        ListView.view ? ListView.view.binding : null
    readonly property var selectionView:
        liveBinding ? liveBinding.selectionView : null

    readonly property int indentLevel: model.indentLevel || 0
    readonly property string markerStyle: model.markerStyle || ""
    readonly property int markerNumber: model.markerNumber || 0
    readonly property bool checked: model.checked || false

    readonly property string markerText: {
        if (markerStyle === "dot")   return markerNumber + "."
        if (markerStyle === "paren") return markerNumber + ")"
        if (markerStyle === "minus") return "-"
        if (markerStyle === "plus")  return "+"
        if (markerStyle === "star")  return "*"
        if (markerStyle === "task")  return checked ? "[x]" : "[ ]"
        return "•"
    }

    readonly property int indentPx: 8 + indentLevel * 24

    LiveEditBinding {
        id: editBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
        textDocument: edit.textDocument
        composing: edit.inputMethodComposing
        text: model.text
    }

    Text {
        id: markerLabel
        anchors {
            left: parent.left
            top: parent.top
        }
        leftPadding: root.indentPx
        topPadding: 2
        text: root.markerText
        font.family: "monospace"
        font.pixelSize: 14
        color: palette.text

        MouseArea {
            visible: root.markerStyle === "task"
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (!root.liveBinding || !root.liveBinding.document) return
                root.liveBinding.document.toggleListItemChecked(model.blockAnchor)
            }
        }
    }

    TextEdit {
        id: edit
        anchors.fill: parent
        leftPadding: markerLabel.implicitWidth + 12
        rightPadding: 8
        topPadding: 2
        bottomPadding: 2
        readOnly: false
        textFormat: TextEdit.PlainText
        wrapMode: TextEdit.Wrap
        font.pixelSize: 14
        color: palette.text
        selectByMouse: true
        persistentSelection: true

        InlineHighlighterAttached {
            target: edit.textDocument
            spans: model.inlineSpans
            theme: root.liveBinding ? root.liveBinding.theme : null
        }

        Keys.priority: Keys.BeforeItem
        Keys.onPressed: (event) => {
            const handler = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
            if (!handler) { event.accepted = false; return }
            const k = event.key
            if (k !== Qt.Key_Return && k !== Qt.Key_Enter
                    && k !== Qt.Key_Backspace && k !== Qt.Key_Delete
                    && k !== Qt.Key_Tab) {
                return
            }
            const handled = handler.tryHandle(k, event.modifiers, root.modelIndex,
                edit.cursorPosition, edit.selectionStart === edit.selectionEnd,
                model.text)
            event.accepted = handled
        }

        function applySelection() {
            const sv = root.selectionView
            if (!sv) { deselect(); return }
            const r = sv.rangeForBlock(model.index)
            if (!r || r.x < 0) { deselect(); return }
            select(r.x, Math.min(r.y, length))
        }

        Connections {
            target: root.selectionView
            function onSelectionChanged() { edit.applySelection() }
        }

        Connections {
            target: root.liveBinding ? root.liveBinding.cursorState : null
            function onCursorChanged() {
                const cs = root.liveBinding ? root.liveBinding.cursorState : null
                if (cs && cs.focusedAnchorRow === root.modelIndex && cs.focusedQtPos >= 0)
                    edit.cursorPosition = cs.focusedQtPos
            }
        }
    }

    function positionAt(x, y) {
        return edit.positionAt(x - edit.leftPadding, y - edit.topPadding)
    }

    function focusEditAt(qtPos) {
        edit.forceActiveFocus()
        if (qtPos >= 0 && qtPos <= edit.length)
            edit.cursorPosition = qtPos
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && cs.focusedAnchorRow === root.modelIndex)
            Qt.callLater(function() { focusEditAt(cs.focusedQtPos >= 0 ? cs.focusedQtPos : 0) })
    }
}
