import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import CrossBlockSpike

ApplicationWindow {
    id: root
    width: 720
    height: 520
    visible: true
    title: "cross-block selection spike"

    SelectionModel { id: selModel }

    // The block texts. In the real renderer these come from the AST.
    readonly property var blockTexts: [
        "First paragraph. Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.",
        "Second paragraph. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat.",
        "Third paragraph. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur.",
        "Fourth paragraph. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.",
        "Fifth paragraph. Sed ut perspiciatis unde omnis iste natus error sit voluptatem accusantium doloremque laudantium."
    ]

    function blockTextsAsList(): variant {
        // Return as a JS array; SelectionModel.collectSelectedText takes a QStringList,
        // and Qt auto-converts JS arrays of strings to QStringList.
        return root.blockTexts.slice()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: "#eef"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                Label {
                    text: selModel.hasSelection
                        ? `Selection: (${selModel.anchorBlock},${selModel.anchorOffset}) → (${selModel.activeBlock},${selModel.activeOffset})`
                        : "No selection. Drag across paragraphs. Ctrl+C to copy."
                    Layout.fillWidth: true
                }
                Button {
                    text: "Copy"
                    enabled: selModel.hasSelection
                    onClicked: {
                        selModel.copySelectionToClipboard(root.blockTexts.slice())
                        console.log("Copied:", JSON.stringify(selModel.collectSelectedText(root.blockTexts.slice())))
                    }
                }
                Button {
                    text: "Clear"
                    enabled: selModel.hasSelection
                    onClicked: selModel.clear()
                }
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 12

            model: root.blockTexts.length

            delegate: TextEdit {
                id: textEdit
                required property int index

                width: ListView.view.width - 24
                x: 12

                text: root.blockTexts[index]
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: false  // selection is driven externally by SelectionModel.

                font.pixelSize: 16

                /// Apply selection from the model whenever it changes.
                Connections {
                    target: selModel
                    function onSelectionChanged() {
                        const r = selModel.rangeForBlock(textEdit.index)
                        if (r.x === -1) {
                            textEdit.deselect()
                        } else {
                            // Clamp the "to end of block" sentinel
                            // (INT32_MAX) against this delegate's actual length.
                            // TextEdit.select silently no-ops if end is out of range,
                            // so we MUST clamp to textEdit.length.
                            const end = Math.min(r.y, textEdit.length)
                            textEdit.select(r.x, end)
                        }
                    }
                }
            }

            // ---- selection input layer ----
            //
            // A single MouseArea over the whole ListView handles press / drag / release
            // and translates them into model updates. TextEdit's own selectByMouse is
            // off, so this MouseArea has clean ownership of all left-button input.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.IBeamCursor
                hoverEnabled: false
                preventStealing: true   // stop ListView's flickable from stealing drags

                /// Hit-test (mouseX, mouseY) → {block, offset} or null.
                ///
                /// Critically, when the mouse leaves the window during a drag,
                /// mouseY can be < 0 or > height. Native text editors keep
                /// extending the selection in that case (to first / last visual
                /// line). We mirror that by clamping out-of-bounds drags to the
                /// document edges.
                function hit(mouseX, mouseY) {
                    const lastIdx = listView.count - 1

                    // Out-of-window mouse positions: clamp into the viewport.
                    // After clamping, the rest of the pipeline (itemAt + positionAt)
                    // produces "snap to the viewport edge with X-tracking" — the
                    // same behavior native text editors give when you drag past the
                    // window edge while still pressing.
                    const clampedX = Math.max(0, Math.min(mouseX, width - 1))
                    const clampedY = Math.max(0, Math.min(mouseY, height - 1))

                    const cx = clampedX + listView.contentX
                    const cy = clampedY + listView.contentY

                    // localX clamped into [0, item.width - 1]. TextEdit.positionAt
                    // returns 0 for any out-of-bounds x (both sides), so an
                    // unclamped localX past the right edge would collapse to
                    // start-of-line rather than extend to end-of-line. Clamping
                    // gives symmetric, native behavior.
                    function clampedLocalX(item, contentX) {
                        return Math.max(0, Math.min(contentX - item.x, item.width - 1))
                    }

                    // Items have horizontal padding (x=12, width=listView.width-24),
                    // so itemAt(cx, cy) returns null whenever cx is in the 12px
                    // left or right viewport margin. Always probe with an x that's
                    // guaranteed inside the items' horizontal range — the actual
                    // mouse cx is only used by positionAt (via clampedLocalX) for
                    // column tracking.
                    const probeX = listView.width / 2

                    // Below the document content (in-viewport whitespace below
                    // the last paragraph): snap to the last block, X-tracked on
                    // its bottom visual line.
                    if (cy >= listView.contentHeight) {
                        const probe = listView.itemAt(probeX, listView.contentHeight - 1)
                        if (probe) {
                            const localY = Math.max(0, probe.height - 1)
                            return { block: probe.index, offset: probe.positionAt(clampedLocalX(probe, cx), localY) }
                        }
                        return { block: lastIdx, offset: root.blockTexts[lastIdx].length }
                    }
                    if (cy < 0) {
                        return { block: 0, offset: 0 }
                    }

                    const item = listView.itemAt(probeX, cy)
                    if (item) {
                        const localY = cy - item.y
                        return { block: item.index, offset: item.positionAt(clampedLocalX(item, cx), localY) }
                    }

                    // In-content but in spacing between delegates: walk up/down
                    // a few pixels to find the bordering items. Once found, use
                    // positionAt(localX, ...) so lateral mouse movement tracks
                    // column-by-column along the bordering visual line. The
                    // "above" item's BOTTOM line and the "below" item's TOP
                    // line are the two candidates; pick whichever is closer to
                    // the actual mouse Y (mid-gap symmetry).
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
                        // Last text line: use a y just inside the bottom of the item.
                        const localY = Math.max(0, aboveItem.height - 1)
                        return { block: aboveItem.index, offset: aboveItem.positionAt(clampedLocalX(aboveItem, cx), localY) }
                    }
                    if (belowItem) {
                        // First text line: y=0 selects offset on the first line.
                        return { block: belowItem.index, offset: belowItem.positionAt(clampedLocalX(belowItem, cx), 0) }
                    }
                    return null
                }

                onPressed: (m) => {
                    const h = hit(m.x, m.y)
                    if (h) {
                        selModel.begin(h.block, h.offset)
                    } else {
                        selModel.clear()
                    }
                }
                onPositionChanged: (m) => {
                    if (!pressed) return
                    const h = hit(m.x, m.y)
                    if (h) selModel.extend(h.block, h.offset)
                }
                // (no special onReleased handling — selection persists)
            }
        }
    }

    // Shortcut: Ctrl+C copies the current selection.
    Shortcut {
        sequence: StandardKey.Copy
        onActivated: {
            if (!selModel.hasSelection) return
            selModel.copySelectionToClipboard(root.blockTexts.slice())
            console.log("Ctrl+C copied:", JSON.stringify(selModel.collectSelectedText(root.blockTexts.slice())))
        }
    }
}
