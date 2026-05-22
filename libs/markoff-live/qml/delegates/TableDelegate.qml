// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.markoff.live 1.0

// E4: TableDelegate — first multi-cell interactive atomic block (L8).
//
// Phase A (A6) shipped a skeleton showing raw pipe-table source as
// monospaced text — used to verify the dispatch path.
//
// Phase B replaces the body with the ParsedTable-driven N×M cell
// grid. B1 (this revision): adds the parseTable JS tokenizer that
// derives headers/alignments/body/cellCharRanges from the block
// buffer. B2: grid rendering. B3: chokepoint registration with
// cell-aware takeFocus. B4: cell focus syncs to LiveCursorState.
//
// Spec: docs/specs/2026-05-22-e4-tables-design.md §4.2, §5.3, §6.
Rectangle {
    id: root
    width: ListView.view ? ListView.view.width : 600
    implicitHeight: bodyColumn.implicitHeight + 16
    color: (root.liveBinding && root.liveBinding.theme)
           ? root.liveBinding.themeColorFor(Theme.CodeBlockBackground)
           : "#f4f4f4"
    radius: 4

    property int modelIndex: index
    readonly property string blockText: model ? model.text : ""
    property var blockAnchor: undefined

    readonly property var liveBinding:
        ListView.view ? ListView.view.binding : null
    readonly property var cursorState:
        liveBinding ? liveBinding.cursorState : null

    readonly property bool isSelected:
        cursorState !== null
        && cursorState.cursorKind === "BlockSelected"
        && cursorState.focusedAnchorRow === root.modelIndex

    // ParsedTable derived from the block buffer. Recomputed on every
    // model.text change.
    //
    // Shape: {
    //   headers: [string],                       // M entries
    //   alignments: [Qt::Alignment],             // M entries
    //   body: [[string]],                        // N rows × M cells
    //   cellCharRanges: [[{start, end}]],        // (N+1) rows (header + body),
    //                                            // M cells each, char positions
    //                                            // within model.text
    //   alignmentRowRange: {start, end},         // |---|---| line char range
    //   parseOk: bool,                           // false if shape malformed
    //   parseError: string                       // diagnostic when parseOk=false
    // }
    //
    // Spec §4.2; tokenizer rules per §6.3 (p1 — padding preserved
    // bytewise, cell content includes leading/trailing whitespace).
    property var parsedTable: parseTable(root.blockText)

    function parseTable(src) {
        const empty = { headers: [], alignments: [], body: [],
                        cellCharRanges: [],
                        alignmentRowRange: { start: 0, end: 0 },
                        parseOk: false,
                        parseError: "empty" }
        if (!src || src.length === 0) return empty

        // 1. Split into lines, preserving each line's start position in src.
        //    A line ends at '\n' (which is consumed); the trailing fragment
        //    after the last '\n' (if any) is also a line.
        const lines = []   // [{ text, start, end }]   end is exclusive of '\n'
        let lineStart = 0
        for (let i = 0; i < src.length; ++i) {
            if (src.charAt(i) === "\n") {
                lines.push({ text: src.substring(lineStart, i),
                             start: lineStart,
                             end: i })
                lineStart = i + 1
            }
        }
        if (lineStart < src.length) {
            lines.push({ text: src.substring(lineStart),
                         start: lineStart,
                         end: src.length })
        }

        // Need at least header + alignment row.
        if (lines.length < 2) {
            return Object.assign({}, empty, { parseError: "need header + alignment row" })
        }

        // 2. Tokenize one line into cells. Returns { cells: [string],
        //    cellCharRanges: [{start, end}] } where start/end are absolute
        //    char positions in src. Cells are the content between pipes;
        //    leading/trailing whitespace is preserved (p1 padding policy).
        function tokenizeLine(line) {
            // Find unescaped '|' positions in line.text. (Escape handling
            // for '\|' is a known B limitation; not handled yet.)
            const pipes = []
            for (let j = 0; j < line.text.length; ++j) {
                if (line.text.charAt(j) === "|") pipes.push(j)
            }
            // A valid pipe-table line has at least 2 pipes (one on each side).
            if (pipes.length < 2) return null
            // Cells are between adjacent pipes; first cell is between
            // pipes[0] and pipes[1], etc. (We don't emit "cells" for the
            // empty leading/trailing slots outside the outer pipes.)
            const cells = []
            const ranges = []
            for (let k = 0; k < pipes.length - 1; ++k) {
                const cellStartInLine = pipes[k] + 1
                const cellEndInLine   = pipes[k + 1]
                cells.push(line.text.substring(cellStartInLine, cellEndInLine))
                ranges.push({ start: line.start + cellStartInLine,
                              end:   line.start + cellEndInLine })
            }
            return { cells: cells, cellCharRanges: ranges }
        }

        // 3. Header is line 0.
        const header = tokenizeLine(lines[0])
        if (!header) {
            return Object.assign({}, empty, { parseError: "header line not pipe-delimited" })
        }
        const M = header.cells.length
        if (M < 1) {
            return Object.assign({}, empty, { parseError: "header has no cells" })
        }

        // 4. Alignment row is line 1.
        const alignment = tokenizeLine(lines[1])
        if (!alignment) {
            return Object.assign({}, empty, { parseError: "alignment row not pipe-delimited" })
        }

        // Parse alignment markers. Each cell content (after trimming) must
        // match /^:?-+:?$/. Empty / whitespace-only → invalid. Mismatched
        // column count → invalid.
        if (alignment.cells.length !== M) {
            return Object.assign({}, empty, { parseError: "alignment column count != header" })
        }
        const alignRe = /^\s*(:?)-+(:?)\s*$/
        const alignments = []
        for (let c = 0; c < M; ++c) {
            const m = alignment.cells[c].match(alignRe)
            if (!m) {
                return Object.assign({}, empty,
                    { parseError: "alignment cell " + c + " malformed: " + alignment.cells[c] })
            }
            const left  = m[1] === ":"
            const right = m[2] === ":"
            if (left && right)      alignments.push(Qt.AlignHCenter)
            else if (right)         alignments.push(Qt.AlignRight)
            else                    alignments.push(Qt.AlignLeft)   // left+default
        }

        // 5. Body rows.
        const body = []
        const cellCharRanges = [ header.cellCharRanges ]   // row 0 = header
        for (let r = 2; r < lines.length; ++r) {
            const rowLine = lines[r]
            // Skip a blank line at the very end (trailing-newline residue).
            if (rowLine.text.length === 0) continue
            const row = tokenizeLine(rowLine)
            if (!row) {
                return Object.assign({}, empty,
                    { parseError: "body row " + (r - 1) + " not pipe-delimited" })
            }
            // Pad short rows with empty cells (GFM tolerance — see §6.4).
            // Truncate long rows to the column count.
            const cells  = row.cells.slice(0, M)
            const ranges = row.cellCharRanges.slice(0, M)
            while (cells.length < M)  cells.push("")
            while (ranges.length < M) ranges.push({ start: rowLine.end, end: rowLine.end })
            body.push(cells)
            cellCharRanges.push(ranges)
        }

        return {
            headers: header.cells,
            alignments: alignments,
            body: body,
            cellCharRanges: cellCharRanges,
            alignmentRowRange: { start: lines[1].start, end: lines[1].end },
            parseOk: true,
            parseError: ""
        }
    }

    // Body. Phase B2: N×M GridLayout of read-only TextEdit cells. The
    // header row (r === 0) is bold and uses the CodeBlockBackground
    // accent; body rows use the EditorBackground. Grid lines are 1-px
    // Rectangle borders coloured by Theme.Quote (per E2.6 slot-reuse
    // convention; future split into a dedicated grid-line slot if a
    // dogfood pass calls for it).
    //
    // When parseTable fails, we fall back to a diagnostic Text so the
    // failure mode is visible during dogfood rather than swallowed by a
    // zero-size grid.
    Column {
        id: bodyColumn
        anchors { left: parent.left; right: parent.right
                  top: parent.top; margins: 8 }
        spacing: 2

        GridLayout {
            id: cellGrid
            visible: root.parsedTable && root.parsedTable.parseOk
            anchors { left: parent.left; right: parent.right }
            columns: visible ? root.parsedTable.headers.length : 1
            rowSpacing: 0
            columnSpacing: 0

            Repeater {
                id: cellRepeater
                model: cellGrid.visible
                       ? (root.parsedTable.body.length + 1)
                         * root.parsedTable.headers.length
                       : 0

                delegate: Rectangle {
                    id: cellRect
                    property alias edit: cellEdit   // B3: cell-addressable focus
                    readonly property int cols: root.parsedTable.headers.length
                    readonly property int r: Math.floor(index / cols)   // 0 = header row
                    readonly property int c: index % cols
                    readonly property bool isHeader: r === 0
                    readonly property string cellText:
                        isHeader ? root.parsedTable.headers[c]
                                 : root.parsedTable.body[r - 1][c]

                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    implicitHeight: cellEdit.implicitHeight + 8

                    color: (root.liveBinding && root.liveBinding.theme)
                           ? root.liveBinding.themeColorFor(
                               isHeader ? Theme.CodeBlockBackground
                                        : Theme.EditorBackground)
                           : (isHeader ? "#f0f0f0" : "#ffffff")
                    border.color: (root.liveBinding && root.liveBinding.theme)
                                  ? root.liveBinding.themeColorFor(Theme.Quote)
                                  : "#888888"
                    border.width: 1

                    TextEdit {
                        id: cellEdit
                        anchors.fill: parent
                        anchors.margins: 4
                        text: cellRect.cellText
                        readOnly: true
                        wrapMode: TextEdit.NoWrap
                        textFormat: TextEdit.PlainText
                        horizontalAlignment: root.parsedTable.alignments[cellRect.c]

                        readonly property var theme:
                            root.liveBinding ? root.liveBinding.theme : null
                        readonly property real fontScale:
                            root.liveBinding ? root.liveBinding.fontScale : 1.0

                        font.pixelSize: theme
                            ? root.liveBinding.themePixelSizeFor(Theme.TextDefault) * fontScale
                            : 14 * fontScale
                        font.family: theme
                            ? root.liveBinding.themeFamilyFor(Theme.TextDefault)
                            : ""
                        font.bold: cellRect.isHeader
                        color: (root.liveBinding && root.liveBinding.theme)
                               ? root.liveBinding.themeColorFor(Theme.TextDefault)
                               : "#222222"
                        selectByMouse: false
                    }
                }
            }
        }

        Text {
            visible: !(root.parsedTable && root.parsedTable.parseOk)
            text: "[E4 TableDelegate — parseError: "
                  + (root.parsedTable ? root.parsedTable.parseError : "null") + "]"
            font.family: "monospace"
            font.pixelSize: 12
            color: (root.liveBinding && root.liveBinding.theme)
                   ? root.liveBinding.themeColorFor(Theme.Quote)
                   : "#888888"
        }
    }

    // Selection ring for BlockSelected state.
    Rectangle {
        visible: root.isSelected
        anchors.fill: parent
        anchors.margins: -2
        border.color: (root.liveBinding && root.liveBinding.theme)
                      ? root.liveBinding.themeColorFor(Theme.SelectionBackground)
                      : "#b0d0ff"
        border.width: 2
        color: "transparent"
        radius: 5
    }

    // Hit-test stub. Phase B4 replaces with cell-aware translation.
    function positionAt(x, y) { return -1 }

    // Total cell count = (header row + body rows) × columns. parseOk
    // ensures both halves of parsedTable are populated.
    function _cellCount() {
        if (!root.parsedTable || !root.parsedTable.parseOk) return 0
        return (root.parsedTable.body.length + 1)
               * root.parsedTable.headers.length
    }

    function _cellAt(r, c) {
        const cols = root.parsedTable.headers.length
        const idx  = r * cols + c
        if (idx < 0 || idx >= _cellCount()) return null
        return cellRepeater.itemAt(idx)
    }

    // Find the column in row `r` whose horizontal pixel span contains
    // `localX` (delegate-local x). Falls back to column 0 if no cell is
    // present yet (geometry not realised) or `localX` is out of range
    // (e.g. left of column 0 → 0; right of last → last col).
    function _columnAtLocalX(r, localX) {
        const cols = root.parsedTable.headers.length
        if (cols < 1) return 0
        // Honour both ends of the range when out of bounds.
        const first = _cellAt(r, 0)
        if (first && localX < first.x) return 0
        for (let c = 0; c < cols; ++c) {
            const cell = _cellAt(r, c)
            if (!cell) continue
            if (localX < cell.x + cell.width) return c
        }
        return cols - 1
    }

    // B3 takeFocus: decode block-buffer qtPos into (cell, cellQtPos) and
    // hand focus to that cell's TextEdit at the decoded position. When
    // `pendingVisualLineHint` is set (cross-block arrow-down / arrow-up),
    // ignore qtPos and place focus by visual-x semantics instead:
    //   FirstLine → header row (r = 0)        at column nearest desiredVisualX
    //   LastLine  → last body row (r = N)     at column nearest desiredVisualX
    // Falls back to cell (0, 0) qtPos 0 if qtPos doesn't land in any cell.
    function takeFocus(qtPos: int) {
        if (!root.parsedTable || !root.parsedTable.parseOk) {
            root.forceActiveFocus()
            return
        }

        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs) {
            const hint = cs.pendingVisualLineHint
            const desiredX = cs.desiredVisualX
            if (hint !== 0 && desiredX >= 0) {
                const lastBodyR = root.parsedTable.body.length   // header=0, body=1..N
                const targetR   = (hint === 1) ? 0 : lastBodyR
                const targetC   = _columnAtLocalX(targetR, desiredX)
                const cell      = _cellAt(targetR, targetC)
                if (cell && cell.edit) {
                    cell.edit.cursorPosition = 0
                    cell.edit.forceActiveFocus()
                    return
                }
            }
        }

        // Default decode: walk cellCharRanges for the (r, c) whose
        // [start, end) contains qtPos (end-inclusive at the last cell to
        // cover end-of-buffer placements).
        const ranges = root.parsedTable.cellCharRanges
        for (let r = 0; r < ranges.length; ++r) {
            const rowRanges = ranges[r]
            for (let c = 0; c < rowRanges.length; ++c) {
                const range = rowRanges[c]
                if (qtPos >= range.start && qtPos <= range.end) {
                    const cell = _cellAt(r, c)
                    if (cell && cell.edit) {
                        const cellLen = cell.edit.length
                        const cellQtPos = Math.min(
                            Math.max(qtPos - range.start, 0), cellLen)
                        cell.edit.cursorPosition = cellQtPos
                        cell.edit.forceActiveFocus()
                        return
                    }
                }
            }
        }

        // Out-of-range qtPos: fall back to (0, 0) qtPos 0.
        const fallback = _cellAt(0, 0)
        if (fallback && fallback.edit) {
            fallback.edit.cursorPosition = 0
            fallback.edit.forceActiveFocus()
        } else {
            root.forceActiveFocus()
        }
    }

    Component.onCompleted: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        blockAnchor = model.blockAnchor
        if (cs) cs.delegateAvailable(blockAnchor, model.kind, root)
    }

    Component.onDestruction: {
        const cs = root.liveBinding ? root.liveBinding.cursorState : null
        if (cs && blockAnchor !== undefined) cs.delegateGoingAway(blockAnchor, root)
    }
}
