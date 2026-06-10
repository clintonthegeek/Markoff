// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import org.markoff.live 1.0
import "KeyDispatch.js" as KeyDispatch

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
    // F1: capture model.inlineSpans on root so cells reach it through
    // `root.blockInlineSpans` instead of `model.inlineSpans` — inside
    // the Repeater delegate, the `model` identifier shadows to the
    // Repeater's per-item context (a number), not the ListView delegate's
    // row data. Same shadow pattern as `blockText` above.
    readonly property var blockInlineSpans: model ? model.inlineSpans : null
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
    property var parsedTable: {
        // Perf instrumentation — see PerfProbe.h. The JS Date.now()
        // resolution is 1ms; sub-ms parseTable runs round to 0, which is
        // itself a useful signal (means it's not the bottleneck).
        const _t0 = Date.now()
        const _r = parseTable(root.blockText)
        if (tableEditBinding)
            tableEditBinding.perfTime("qml.TableDelegate.parseTable", Date.now() - _t0)
        return _r
    }

    // E4 follow-up B1/B3: content-aware per-column widths.
    // QList<qreal> from TableEditBinding::computeColumnWidths via
    // Penelope's distributeColumnsAuto. Re-evaluates when:
    //   - parsedTable changes (cell content / row+col shape shifts)
    //   - cellGrid.width changes (delegate width tracks ListView resize)
    //   - the theme swaps (dark/light toggle changes the font slot)
    //   - liveBinding.fontScale changes (user-driven zoom)
    //
    // The last two are explicit `void` dependency anchors — QML's binding
    // tracker can't see the theme/fontScale reads that happen inside
    // computeColumnWidths' C++ body, so without these the binding misses
    // font-driven metric changes and widths go stale. Empty list when
    // there's nothing to render or cellGrid.width hasn't been computed
    // yet (binding's second fire after construction is the source of
    // truth per spec §7 risk 3).
    property var columnWidths: {
        // B3 dependency anchors (spec §3.4).
        if (root.liveBinding) {
            void root.liveBinding.theme
            void root.liveBinding.fontScale
        }
        if (!tableEditBinding || !root.parsedTable
            || !root.parsedTable.parseOk || cellGrid.width <= 0) return []
        return tableEditBinding.computeColumnWidths(root.parsedTable.headers,
                                                     root.parsedTable.body,
                                                     cellGrid.width)
    }

    // C2: per-delegate edit binding. Cells dispatch their user edits
    // through tableEditBinding.applyCellEdit; the binding does the
    // cell-relative → block-buffer translation in C++ (see
    // src/TableEditBinding.cpp) and calls d2ApplyBufferEdit.
    TableEditBinding {
        id: tableEditBinding
        binding: root.liveBinding
        modelIndex: root.modelIndex
    }

    // C3: last-known focused cell. Set whenever a cell's TextEdit
    // gains active focus; consulted by _restoreCellFocus to re-anchor
    // after `parsedTable` re-evaluates (driven by `model.text` —
    // e.g. Source-mode edits, remote D5 ops, or our own cell edits
    // that get a buffer-roundtrip).
    //
    // Shape: { r: int, c: int, cursorPosition: int } or null.
    property var _focusedCellMemo: null

    // C3: on every `parsedTable` change defer focus restoration
    // until after the binding cascade settles. Qt.callLater is
    // load-bearing here — when count changes, the Repeater destroys
    // and recreates cell delegates, and restoring focus before that
    // settles would either fail (target doesn't exist yet) or land
    // on a delegate scheduled for destruction. Defer one tick.
    // Per INVARIANTS.md #6: justified at landing.
    onParsedTableChanged: {
        if (_focusedCellMemo === null) return
        if (!root.parsedTable || !root.parsedTable.parseOk) return
        Qt.callLater(_restoreCellFocus)
    }

    function _restoreCellFocus() {
        if (!_focusedCellMemo) return
        if (!root.parsedTable || !root.parsedTable.parseOk) return
        if (!root.cursorState || root.blockAnchor === undefined) return

        const totalRows = root.parsedTable.body.length + 1
        const cols      = root.parsedTable.headers.length
        if (totalRows < 1 || cols < 1) return
        const r = Math.min(Math.max(_focusedCellMemo.r, 0), totalRows - 1)
        const c = Math.min(Math.max(_focusedCellMemo.c, 0), cols - 1)

        const ranges = root.parsedTable.cellCharRanges
        if (r < 0 || r >= ranges.length) return
        if (c < 0 || c >= ranges[r].length) return
        const range = ranges[r][c]
        const cellLen = range.end - range.start
        const cellQtPos = Math.min(
            Math.max(_focusedCellMemo.cursorPosition, 0), cellLen)
        const flatQtPos = range.start + cellQtPos

        // Route through the chokepoint rather than lifting focus on
        // the cell directly — establishFocus → tryResolvePending →
        // takeFocus places the cell caret and acquires focus inside
        // takeFocus's body, which is the only delegate-side path the
        // focus-path discipline check permits.
        root.cursorState.establishFocus(root.blockAnchor, flatQtPos)
    }

    // Shift+arrow extension. Used by D2 Left/Right cell-edge crossings,
    // Up/Down cross-row, and Tab/Shift+Tab when the user holds Shift.
    // Mirrors `LiveNavigationController::applyMotion` for paragraph
    // delegates: shift → extend (begin first if no anchor yet); plain
    // → existing motion path (establishFocus same-block / requestTextCaret
    // cross-block). For cross-block extends, also requestTextCaretAtRow
    // so focus follows the active end — `extend()` flips
    // `m_selectionExtended=true`, so `request()`'s anchor-clear is gated
    // off (cursor-authority decision §5.4) and the selection survives the
    // focus move.
    function _arrowMove(event, shift, isCrossBlock,
                        targetRow, targetFlatQtPos,
                        currentFlatQtPos) {
        if (!root.cursorState || root.blockAnchor === undefined) {
            event.accepted = true
            return
        }
        if (shift) {
            if (root.cursorState.anchorBlock() < 0)
                root.cursorState.begin(root.modelIndex, currentFlatQtPos)
            root.cursorState.extend(targetRow, targetFlatQtPos)
            // Move focus to the target cell (even for same-block) so
            // subsequent shift+arrow keys are received by THAT cell's
            // TextEdit — without this, focus stays on the originating
            // cell, its cursorPosition doesn't change, and the next
            // key recomputes the same target → extend short-circuits
            // (request() returns early when m_cursor == newCursor).
            // For cross-block exits this also brings the target block
            // into focus; `extend()` flipped m_selectionExtended=true
            // so §5.4's anchor-clear is gated off.
            root.cursorState.requestTextCaretAtRow(
                targetRow, targetFlatQtPos)
        } else if (isCrossBlock) {
            root.cursorState.requestTextCaretAtRow(
                targetRow, targetFlatQtPos)
        } else {
            root.cursorState.establishFocus(
                root.blockAnchor, targetFlatQtPos)
        }
        event.accepted = true
    }

    // C2 helper: compute a prefix/suffix diff between two cell text
    // snapshots. QML's TextEdit doesn't expose QTextDocument's
    // (qtPos, removed, added) contentsChange signal natively, so we
    // reconstruct the delta from onTextChanged + a stored
    // _previousText snapshot per cell. The prefix/suffix scan is
    // exact for any contiguous edit (typing, deletion, paste at a
    // single point); for non-contiguous edits the result is still a
    // valid final-state-equivalent op, which is what d2ApplyBufferEdit
    // accepts.
    function _diffEdit(oldStr, newStr) {
        let prefixLen = 0
        const minLen = Math.min(oldStr.length, newStr.length)
        while (prefixLen < minLen
               && oldStr.charAt(prefixLen) === newStr.charAt(prefixLen))
            ++prefixLen
        let suffixLen = 0
        while (suffixLen < (minLen - prefixLen)
               && oldStr.charAt(oldStr.length - 1 - suffixLen)
                  === newStr.charAt(newStr.length - 1 - suffixLen))
            ++suffixLen
        return {
            qtPos: prefixLen,
            removed: oldStr.length - prefixLen - suffixLen,
            added: newStr.length - prefixLen - suffixLen,
            addedText: newStr.substring(prefixLen, newStr.length - suffixLen),
        }
    }

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
                    // Bounds-safe lookup — during a structural change
                    // (column-count or row-count shrink) the Repeater
                    // destroys delegates whose indices fall outside the
                    // new dimensions, but their bindings re-evaluate
                    // one final time with stale (r, c) before tear-down.
                    // Without guards, body[r-1][c] / headers[c] would
                    // hit undefined[index] and crash QML's binding
                    // evaluator. Returning "" lets the destroying cell
                    // settle cleanly.
                    readonly property string cellText: {
                        if (tableEditBinding)
                            tableEditBinding.perfNote("qml.TableDelegate.cellText.eval")
                        if (!root.parsedTable || !root.parsedTable.parseOk) return ""
                        if (cellRect.isHeader) {
                            const hs = root.parsedTable.headers
                            return (cellRect.c >= 0 && cellRect.c < hs.length)
                                ? hs[cellRect.c] : ""
                        }
                        const bi = cellRect.r - 1
                        const body = root.parsedTable.body
                        if (bi < 0 || bi >= body.length) return ""
                        const row = body[bi]
                        return (cellRect.c >= 0 && cellRect.c < row.length)
                            ? row[cellRect.c] : ""
                    }

                    // E4 wrap B2: consume the C++ side's content-aware
                    // column widths. fillWidth: false lets preferredWidth
                    // be respected (GridLayout's default stretches the
                    // last column to absorb FP rounding — spec §7 risk 3,
                    // acceptable). The 60px floor used to live here as
                    // Layout.minimumWidth; it's now applied inside
                    // computeColumnWidths' per-column metric aggregation
                    // so empty/very-short cells still occupy a readable
                    // slot.
                    Layout.fillWidth: false
                    Layout.preferredWidth: (cellRect.c >= 0
                                            && cellRect.c < root.columnWidths.length)
                                           ? root.columnWidths[cellRect.c]
                                           : 60   // TableEditBinding::kMinColumnWidth
                    // E4 wrap dogfood fix (2026-05-23): without fillHeight,
                    // GridLayout sizes each cell Rectangle to its own
                    // implicitHeight, then vertically centres the shorter
                    // cells inside the row's max height — exposing the
                    // GridLayout's background above and below short cells
                    // in rows where a sibling wraps. fillHeight stretches
                    // every cell Rectangle to the row's full height; the
                    // TextEdit inside stays anchored.fill to track the new
                    // height (text stays top-aligned within the cell).
                    Layout.fillHeight: true
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
                        // C2: cells are editable; read-only is cosmetic
                        // here (spec §4.2) — the C++ gate is the contract.
                        readOnly: root.liveBinding ? root.liveBinding.readOnly : false
                        // E4 wrap B2: WordBoundary preferred; mid-word
                        // break only as a last resort (for hyper-long
                        // unbreakable runs — URLs, long identifiers).
                        // Cell width comes from the parent Rectangle's
                        // Layout.preferredWidth driven by columnWidths.
                        wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                        textFormat: TextEdit.PlainText
                        horizontalAlignment: {
                            // Bounds-safe during structural transitions:
                            // see cellText for the rationale.
                            const al = root.parsedTable
                                       ? (root.parsedTable.alignments || [])
                                       : []
                            return (cellRect.c >= 0 && cellRect.c < al.length)
                                ? al[cellRect.c] : Qt.AlignLeft
                        }

                        // C2: previous-text snapshot for diff-based delta
                        // computation. Initialised in Component.onCompleted
                        // (the initial text-binding fire seeds it). After
                        // each dispatch we snap forward before the
                        // d2ApplyBufferEdit cascade in case the parsedTable
                        // refresh re-binds cell.text on the way back.
                        property string _previousText: ""

                        Component.onCompleted: {
                            cellEdit._previousText = cellEdit.text
                        }

                        // C3: track the last focused cell so that a
                        // post-tokenize binding cascade (Source-mode
                        // edit, remote D5 op, our own buffer roundtrip)
                        // can re-anchor focus on the same (r, c) /
                        // cursor position.
                        onActiveFocusChanged: {
                            if (cellEdit.activeFocus) {
                                root._focusedCellMemo = {
                                    r: cellRect.r,
                                    c: cellRect.c,
                                    cursorPosition: cellEdit.cursorPosition,
                                }
                            }
                        }

                        onTextChanged: {
                            // Re-entrance guard. C1 always returns false;
                            // future C-phase work may flip it during
                            // model-driven refreshes that bypass
                            // QQuickTextEdit's same-string short-circuit.
                            if (tableEditBinding.isApplyingTextUpdate()) {
                                cellEdit._previousText = cellEdit.text
                                return
                            }
                            // Binding-driven update: cell.text was just
                            // refreshed to match parsedTable. In the
                            // common local-edit case QQuickTextEdit's
                            // same-string short-circuit means this branch
                            // never runs; on a future D5 remote edit it
                            // fires and we MUST skip (otherwise the
                            // remote op would be re-applied locally).
                            if (cellEdit.text === cellRect.cellText
                                && cellEdit._previousText !== cellEdit.text) {
                                cellEdit._previousText = cellEdit.text
                                return
                            }
                            if (!root.parsedTable || !root.parsedTable.parseOk) return
                            const ccr2 = root.parsedTable.cellCharRanges
                            if (cellRect.r < 0 || cellRect.r >= ccr2.length) return
                            const rowRanges = ccr2[cellRect.r]
                            if (!rowRanges) return
                            if (cellRect.c < 0 || cellRect.c >= rowRanges.length) return
                            const range = rowRanges[cellRect.c]
                            const diff = root._diffEdit(cellEdit._previousText,
                                                        cellEdit.text)
                            // Snap forward BEFORE dispatch so the
                            // d2ApplyBufferEdit-driven binding re-eval
                            // (which may set cell.text back to the same
                            // value — a no-op — or differ slightly if
                            // parsedTable tokenisation normalises) doesn't
                            // re-enter the diff branch with a stale prev.
                            cellEdit._previousText = cellEdit.text
                            tableEditBinding.applyCellEdit(
                                range.start, diff.qtPos, diff.removed,
                                diff.addedText)
                        }

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
                        // Cross-cell selections survive focus moves between
                        // cells. Without this, takeFocus on a target cell
                        // during shift+arrow extension clears the source
                        // cell's per-cell highlight that
                        // `_applyCrossBlockSelection` just set. Matches
                        // UnifiedInlineTextDelegate's setting.
                        persistentSelection: true

                        // F1: per-cell inline highlighter. Spans come from
                        // the block's `model.inlineSpans` (parser-side fix
                        // landed in the same change: pipe_table_cell ranges
                        // now go through the inline grammar, so cells get
                        // real bold/italic/code/wikilink spans). The C++
                        // helper filters spans to those whose char ranges
                        // fall within the cell's char range and re-projects
                        // `charOffset` (and `parentCharStart/End`) into the
                        // cell's local frame. `InlineHighlighterAttached`'s
                        // target is the cell's QQuickTextDocument, so the
                        // highlighter paints inside the cell document, not
                        // the block buffer.
                        //
                        // The bounds-safe `_cellCharRange` lookup mirrors
                        // `cellText`'s defensive pattern: during Repeater
                        // tear-down or initial construction, `parsedTable`
                        // may not yet contain entries for our (r, c).
                        readonly property var _cellCharRange: {
                            if (!root.parsedTable || !root.parsedTable.parseOk)
                                return null
                            const ccr = root.parsedTable.cellCharRanges
                            if (cellRect.r < 0 || cellRect.r >= ccr.length)
                                return null
                            const rr = ccr[cellRect.r]
                            if (!rr || cellRect.c < 0 || cellRect.c >= rr.length)
                                return null
                            return rr[cellRect.c]
                        }

                        InlineHighlighterAttached {
                            target: cellEdit.textDocument
                            spans: cellEdit._cellCharRange
                                ? tableEditBinding.inlineSpansForCell(
                                      root.blockInlineSpans,
                                      cellEdit._cellCharRange.start,
                                      cellEdit._cellCharRange.end)
                                : []
                            theme: root.liveBinding ? root.liveBinding.theme : null
                            fontScale: root.liveBinding ? root.liveBinding.fontScale : 1.0
                            caretPosition: cellEdit.activeFocus
                                           ? cellEdit.cursorPosition : -1
                            selectionStart: (cellEdit.activeFocus
                                             && cellEdit.selectionStart !== cellEdit.selectionEnd)
                                            ? cellEdit.selectionStart : -1
                            selectionEnd: (cellEdit.activeFocus
                                           && cellEdit.selectionStart !== cellEdit.selectionEnd)
                                          ? cellEdit.selectionEnd : -1
                        }

                        // D1: Tab / Shift+Tab move between cells. The
                        // BeforeItem priority intercepts the key before
                        // TextEdit's default tab-insertion handling.
                        // Out-of-bounds at the table edges exits to the
                        // previous / next block via the chokepoint.
                        // D1: Tab / Shift+Tab move between cells.
                        // D2: Arrow keys handle cell-edge crossings
                        // (Left/Right) and cross-row motion (Up/Down).
                        // The BeforeItem priority intercepts the key
                        // before TextEdit's default cursor handling.
                        Keys.priority: Keys.BeforeItem
                        Keys.onPressed: (event) => {
                            if (!root.parsedTable || !root.parsedTable.parseOk) return
                            if (!root.cursorState || root.blockAnchor === undefined) return

                            // ---- Ctrl-chords (Ctrl+C / X / V / A / Z / Y) ----
                            // Route through KeyDispatch so Ctrl+C inside a
                            // cell reaches LiveClipboardController.copy()
                            // instead of falling through to TextEdit's
                            // built-in (which only copies the within-cell
                            // selection and so produces nothing when the
                            // cross-block highlight is the visible range
                            // but the focused cell's TextEdit selection
                            // is empty / wrong). User dogfood 2026-05-22:
                            // "Shift+Down ends in a cell; Ctrl+C does
                            // nothing. Right-click → Copy works".
                            if (KeyDispatch.tryDispatchCtrlChord(event,
                                    { binding: root.liveBinding }))
                                return

                            // ---- D3: Enter (inert) / Esc (BlockSelected) ----
                            // Enter inside a cell would otherwise insert a
                            // literal `\n` into the block buffer and break
                            // the pipe-table parse. Swallow it. Multi-line
                            // cell content is a future-phase concern; for
                            // now cells are single-line.
                            if (event.key === Qt.Key_Return
                                || event.key === Qt.Key_Enter) {
                                event.accepted = true
                                return
                            }
                            // Esc transitions the cursor from cell-edit
                            // (TextCaret on the table block) to the
                            // block-as-a-unit selection state. Subsequent
                            // Backspace/Delete on the BlockSelected table
                            // deletes it via the existing block-only
                            // delete path (plan §E).
                            if (event.key === Qt.Key_Escape) {
                                root.cursorState.requestBlockSelected(root.blockAnchor)
                                event.accepted = true
                                return
                            }

                            // ---- E1: Backspace at first cell, cellQtPos 0 ----
                            // → BlockSelected{table}. From the user's POV
                            // this means: "I'm at the very start of the
                            // table; one Backspace selects the table; the
                            // next Backspace deletes it." Other cells:
                            // Backspace at cellQtPos 0 is inert (don't
                            // delete the closing `|`).
                            if (event.key === Qt.Key_Backspace
                                && cellEdit.cursorPosition === 0) {
                                if (cellRect.r === 0 && cellRect.c === 0) {
                                    root.cursorState.requestBlockSelected(
                                        root.blockAnchor)
                                }
                                event.accepted = true
                                return
                            }

                            // ---- E2: Delete at last cell, cellQtPos == cellLen ----
                            // Symmetric to E1: at the bottom-right cell's
                            // trailing edge, Delete promotes to
                            // BlockSelected{table}. Other cells: Delete
                            // at cellLen is inert.
                            if (event.key === Qt.Key_Delete
                                && cellEdit.cursorPosition === cellEdit.length) {
                                const lastBodyR = root.parsedTable.body.length
                                const lastC     = root.parsedTable.headers.length - 1
                                if (cellRect.r === lastBodyR
                                    && cellRect.c === lastC) {
                                    root.cursorState.requestBlockSelected(
                                        root.blockAnchor)
                                }
                                event.accepted = true
                                return
                            }

                            // ---- D1: Tab / Shift+Tab ----
                            if (event.key === Qt.Key_Tab
                                || event.key === Qt.Key_Backtab) {
                                const isBacktab =
                                    event.key === Qt.Key_Backtab
                                    || (event.modifiers & Qt.ShiftModifier) !== 0
                                const cols      = root.parsedTable.headers.length
                                const totalRows = root.parsedTable.body.length + 1
                                let nextR = cellRect.r
                                let nextC = cellRect.c
                                if (isBacktab) {
                                    nextC -= 1
                                    if (nextC < 0) { nextC = cols - 1; nextR -= 1 }
                                } else {
                                    nextC += 1
                                    if (nextC >= cols) { nextC = 0; nextR += 1 }
                                }
                                if (nextR < 0) {
                                    root.cursorState.requestTextCaretAtRow(
                                        root.modelIndex - 1, 0)
                                } else if (nextR >= totalRows) {
                                    root.cursorState.requestTextCaretAtRow(
                                        root.modelIndex + 1, 0)
                                } else {
                                    const ranges = root.parsedTable.cellCharRanges
                                    if (nextR >= ranges.length) return
                                    if (nextC >= ranges[nextR].length) return
                                    const range = ranges[nextR][nextC]
                                    root.cursorState.establishFocus(
                                        root.blockAnchor, range.start)
                                }
                                event.accepted = true
                                return
                            }

                            // ---- D2: Left / Right at cell edges ----
                            // Shift+Left/Right inside a cell (not at edge) is
                            // handled by TextEdit's built-in shift-extend; we
                            // only intervene at the cell boundary, where the
                            // extend needs to cross into the previous/next
                            // cell or out of the table block entirely.
                            if (event.key === Qt.Key_Left
                                || event.key === Qt.Key_Right) {
                                const isLeft = event.key === Qt.Key_Left
                                const atEdge = isLeft
                                    ? cellEdit.cursorPosition === 0
                                    : cellEdit.cursorPosition === cellEdit.length
                                if (!atEdge) return  // within-cell — pass to TextEdit
                                const shiftHeld =
                                    (event.modifiers & Qt.ShiftModifier) !== 0
                                const currentRanges =
                                    root.parsedTable.cellCharRanges
                                if (cellRect.r >= currentRanges.length) return
                                const curRowRanges = currentRanges[cellRect.r]
                                if (cellRect.c >= curRowRanges.length) return
                                const curRange = curRowRanges[cellRect.c]
                                const currentFlatQtPos =
                                    curRange.start + cellEdit.cursorPosition

                                const cols      = root.parsedTable.headers.length
                                const totalRows = root.parsedTable.body.length + 1
                                let nextR = cellRect.r
                                let nextC = cellRect.c
                                if (isLeft) {
                                    nextC -= 1
                                    if (nextC < 0) { nextC = cols - 1; nextR -= 1 }
                                } else {
                                    nextC += 1
                                    if (nextC >= cols) { nextC = 0; nextR += 1 }
                                }
                                if (nextR < 0) {
                                    root._arrowMove(event, shiftHeld,
                                        /*isCrossBlock=*/true,
                                        root.modelIndex - 1, 0,
                                        currentFlatQtPos)
                                } else if (nextR >= totalRows) {
                                    root._arrowMove(event, shiftHeld,
                                        /*isCrossBlock=*/true,
                                        root.modelIndex + 1, 0,
                                        currentFlatQtPos)
                                } else {
                                    if (nextR >= currentRanges.length) return
                                    if (nextC >= currentRanges[nextR].length) return
                                    const range = currentRanges[nextR][nextC]
                                    const cellLen = range.end - range.start
                                    // Land at end-of-prev (Left) or start-of-next (Right).
                                    const cellQtPos = isLeft ? cellLen : 0
                                    root._arrowMove(event, shiftHeld,
                                        /*isCrossBlock=*/false,
                                        root.modelIndex,
                                        range.start + cellQtPos,
                                        currentFlatQtPos)
                                }
                                return
                            }

                            // ---- D2: Up / Down cross-row ----
                            if (event.key === Qt.Key_Up
                                || event.key === Qt.Key_Down) {
                                // Cells are NoWrap single-line, so any
                                // Up/Down crosses the cell-row boundary —
                                // no within-cell vertical motion to honour.
                                const isUp = event.key === Qt.Key_Up
                                const shiftHeld =
                                    (event.modifiers & Qt.ShiftModifier) !== 0
                                const currentRanges =
                                    root.parsedTable.cellCharRanges
                                if (cellRect.r >= currentRanges.length) return
                                const curRowRanges = currentRanges[cellRect.r]
                                if (cellRect.c >= curRowRanges.length) return
                                const curRange = curRowRanges[cellRect.c]
                                const currentFlatQtPos =
                                    curRange.start + cellEdit.cursorPosition

                                const totalRows = root.parsedTable.body.length + 1
                                const nextR = isUp ? cellRect.r - 1
                                                   : cellRect.r + 1
                                if (nextR < 0) {
                                    root._arrowMove(event, shiftHeld,
                                        /*isCrossBlock=*/true,
                                        root.modelIndex - 1, 0,
                                        currentFlatQtPos)
                                } else if (nextR >= totalRows) {
                                    root._arrowMove(event, shiftHeld,
                                        /*isCrossBlock=*/true,
                                        root.modelIndex + 1, 0,
                                        currentFlatQtPos)
                                } else {
                                    if (nextR >= currentRanges.length) return
                                    if (cellRect.c >= currentRanges[nextR].length) return
                                    const range = currentRanges[nextR][cellRect.c]
                                    const cellLen = range.end - range.start
                                    // Same-column-x proxy: carry the source
                                    // cell's cursorPosition (all cells share
                                    // the table's body font, so the char-index
                                    // match yields the closest visual-x).
                                    const target = Math.min(
                                        cellEdit.cursorPosition, cellLen)
                                    root._arrowMove(event, shiftHeld,
                                        /*isCrossBlock=*/false,
                                        root.modelIndex,
                                        range.start + target,
                                        currentFlatQtPos)
                                }
                                return
                            }
                        }

                        // B4: round-trip cell-relative cursor moves back to
                        // the chokepoint as block-buffer flatQtPos. The
                        // chokepoint is doc-keyed and unconditionally drops
                        // re-entrant equal-state updates, so the cycle
                        // (takeFocus places the cell caret, this slot
                        // syncs the position back to LiveCursorState)
                        // settles in one round. This handler is what
                        // makes future cell-internal arrow keys, typing,
                        // and selection moves visible to the chokepoint —
                        // for fresh clicks the identity holds via takeFocus
                        // alone (its decode is the inverse), but cursor
                        // moves that bypass establishFocus need this path.
                        onCursorPositionChanged: {
                            // C3: keep _focusedCellMemo current so that a
                            // re-tokenize triggered by the imminent cell
                            // edit (or any subsequent buffer change)
                            // restores the post-move position, not the
                            // focus-acquisition value.
                            if (cellEdit.activeFocus) {
                                root._focusedCellMemo = {
                                    r: cellRect.r,
                                    c: cellRect.c,
                                    cursorPosition: cellEdit.cursorPosition,
                                }
                            }
                            // 2026-05-22 cursor-authority decision §5.3:
                            // only the focused cell reports cursor moves.
                            // Non-focused cells' onCursorPositionChanged
                            // fires on text-binding refreshes (after
                            // parsedTable re-tokenizes) and must not
                            // pollute the chokepoint.
                            if (!cellEdit.activeFocus) return
                            if (!root.liveBinding || !root.liveBinding.cursorState) return
                            if (!root.parsedTable || !root.parsedTable.parseOk) return
                            const ccr = root.parsedTable.cellCharRanges
                            if (cellRect.r < 0 || cellRect.r >= ccr.length) return
                            const rowRanges = ccr[cellRect.r]
                            if (!rowRanges) return
                            if (cellRect.c < 0 || cellRect.c >= rowRanges.length) return
                            const range = rowRanges[cellRect.c]
                            const cellLen = cellEdit.length
                            const clamped = Math.min(Math.max(cellEdit.cursorPosition, 0), cellLen)
                            const flatQtPos = range.start + clamped
                            root.liveBinding.cursorState.syncFromTextEdit(
                                root.blockAnchor, flatQtPos)
                        }
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

    // B4 hit-test: translate (delegate-local x, y) → flat block-buffer
    // qtPos by walking cell geometry until we find the (r, c) whose
    // bounding box contains the point, then asking the cell's TextEdit
    // for the intra-cell qtPos at the cell-relative (x, y). Returns
    // flatQtPos = cellCharRanges[r][c].start + cellQtPos. Returns -1
    // when the parse failed or the point falls outside every cell — in
    // which case LiveView's hit() degrades gracefully (focus on the
    // delegate root without cursor placement).
    function positionAt(x, y) {
        if (!root.parsedTable || !root.parsedTable.parseOk) return -1
        const totalRows = root.parsedTable.body.length + 1
        const cols      = root.parsedTable.headers.length
        for (let r = 0; r < totalRows; ++r) {
            for (let c = 0; c < cols; ++c) {
                const cell = _cellAt(r, c)
                if (!cell) continue
                const cellLocalX = x - cell.x
                const cellLocalY = y - cell.y
                if (cellLocalX < 0 || cellLocalX > cell.width)  continue
                if (cellLocalY < 0 || cellLocalY > cell.height) continue
                // 4-px inset matches the TextEdit's anchors.margins.
                const cellQtPos = cell.edit.positionAt(
                    cellLocalX - 4, cellLocalY - 4)
                const range = root.parsedTable.cellCharRanges[r][c]
                const cellLen = cell.edit.length
                const clamped = Math.min(Math.max(cellQtPos, 0), cellLen)
                return range.start + clamped
            }
        }
        return -1
    }

    // Total cell count = (header row + body rows) × columns. parseOk
    // ensures both halves of parsedTable are populated.
    function _cellCount() {
        if (!root.parsedTable || !root.parsedTable.parseOk) return 0
        return (root.parsedTable.body.length + 1)
               * root.parsedTable.headers.length
    }

    // Cross-block range highlight (cursor-authority follow-up, 2026-05-22).
    // When a cross-block selection passes through or into a table, each cell
    // renders the highlight on its portion of the block-flat range. Mirrors
    // UnifiedInlineTextDelegate's applySelection() but per-cell.
    //
    // PERF: only listens to selectionChanged (NOT cursorChanged). Every
    // extend() call emits selectionChanged via emitSelectionChanged(), so
    // drag-extend through the table re-applies. Subscribing to cursorChanged
    // additionally would fire on every within-block keystroke, redundantly
    // re-running this loop even when the table's range is unchanged — and
    // would create a feedback cascade because ed.select() drives the cell's
    // onCursorPositionChanged → syncFromTextEdit → cursorChanged → re-fire.
    //
    // Memoisation: track the last applied (rangeStart, rangeEnd) and bail
    // when unchanged. Drag-select fires selectionChanged ~60 Hz; many of
    // those report the same range for this block (e.g. dragging within the
    // post-table paragraph keeps the table fully selected).
    property var _lastAppliedRange: null
    function _applyCrossBlockSelection() {
        if (!root.parsedTable || !root.parsedTable.parseOk) return
        const cs = root.cursorState
        if (!cs) return

        const range = cs.rangeForBlock(root.modelIndex)
        const blockStart = (range && range.x >= 0) ? range.x : -1
        const blockEnd   = (range && range.x >= 0) ? range.y : -1

        // Bail if nothing changed for THIS block — drag-select inside an
        // adjacent block fires selectionChanged repeatedly with the same
        // (or no-range) result here.
        const last = root._lastAppliedRange
        if (last
                && last.start === blockStart
                && last.end   === blockEnd) {
            return
        }
        root._lastAppliedRange = { start: blockStart, end: blockEnd }

        const ccr = root.parsedTable.cellCharRanges
        const blockHasNoRange = (blockStart < 0)

        for (let r = 0; r < ccr.length; ++r) {
            const rowRanges = ccr[r]
            for (let c = 0; c < rowRanges.length; ++c) {
                const cell = _cellAt(r, c)
                if (!cell || !cell.edit) continue
                const ed = cell.edit
                if (blockHasNoRange) {
                    if (ed.selectionStart !== ed.selectionEnd)
                        ed.deselect()
                    continue
                }
                const cellRange = rowRanges[c]
                if (!cellRange) continue
                if (blockEnd <= cellRange.start || blockStart >= cellRange.end) {
                    if (ed.selectionStart !== ed.selectionEnd)
                        ed.deselect()
                    continue
                }
                const overlapStart = Math.max(blockStart, cellRange.start)
                                     - cellRange.start
                const overlapEnd   = Math.min(blockEnd, cellRange.end)
                                     - cellRange.start
                if (overlapStart === overlapEnd) {
                    if (ed.selectionStart !== ed.selectionEnd)
                        ed.deselect()
                    continue
                }
                // Suppress the focused cell's syncFromTextEdit echo while
                // we're driving cursorPosition programmatically. Without
                // this, ed.select() fires the cell's onCursorPositionChanged,
                // which routes through syncFromTextEdit → cursorChanged →
                // re-fires this loop. The chokepoint's same-block sync
                // accepts the position write because activeFocus is true
                // for the focused cell.
                if (!ed.selectionStart || ed.selectionStart !== overlapStart
                        || ed.selectionEnd !== overlapEnd) {
                    ed.select(overlapStart, overlapEnd)
                }
            }
        }
    }

    Connections {
        target: root.cursorState
        function onSelectionChanged() { root._applyCrossBlockSelection() }
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
        // E4 E1: when the cursor is BlockSelected (table-as-a-unit),
        // focus lives on the delegate root rather than any cell. The
        // delegate root's Keys.onPressed handler (below) routes
        // structural keys through structuralKeyHandler so the
        // generic block-only Delete/Backspace/Enter/Up/Down behavior
        // applies.
        if (root.isSelected) {
            root.forceActiveFocus()
            return
        }
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

    // E4 E1: root-level Keys handler for the BlockSelected state.
    // When the cursor is BlockSelected{table}, takeFocus puts focus
    // on the delegate root (not a cell). Structural keys are routed
    // to LiveStructuralKeyHandler, where Table's `isBlockOnly = true`
    // registers blockOnlyDelete/Enter/NavigateUp/NavigateDown for
    // each. Same pattern as `BlockOnlyDelegateBase.qml:29-42`.
    Keys.priority: Keys.BeforeItem
    Keys.onPressed: (event) => {
        if (!root.isSelected) { event.accepted = false; return }

        // Ctrl-modifier chords first (Ctrl+C / Ctrl+X / Ctrl+V / Ctrl+A
        // / Ctrl+Z / Ctrl+Y). When the table is BlockSelected, Ctrl+C
        // copies the table's markdown via LiveClipboardController's
        // BlockSelected branch. Ctrl+X cuts (copies + deletes via
        // blockOnlyDelete on the next key tick — currently not wired
        // for BlockSelected; cut acts as copy only).
        if (KeyDispatch.tryDispatchCtrlChord(event, { binding: root.liveBinding }))
            return

        const sh = root.liveBinding ? root.liveBinding.structuralKeyHandler : null
        if (!sh) { event.accepted = false; return }
        const k = event.key
        if (k !== Qt.Key_Delete && k !== Qt.Key_Backspace
                && k !== Qt.Key_Up && k !== Qt.Key_Down
                && k !== Qt.Key_Return && k !== Qt.Key_Enter) {
            event.accepted = false
            return
        }
        const handled = sh.tryHandle(k, event.modifiers, root.modelIndex,
            /*qtPos=*/-1, /*selectionEmpty=*/true, root.blockText)
        event.accepted = handled
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
