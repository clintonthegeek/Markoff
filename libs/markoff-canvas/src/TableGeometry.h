// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include <QByteArray>

#include <markoff/canvas/TableTypes.h>

namespace Markoff::Canvas {

/// A cell's content byte range within its block's blockText(), end
/// exclusive. Absolute (block-relative) offsets, usable directly as
/// d2ApplyBufferEdit arguments (spec C4 — one coordinate space).
struct TableCellRange {
    int start = 0;
    int end   = 0;
};

/// One physical line of the table block's source, in file order: line 0 is
/// the header, line 1 is the alignment/delimiter row, lines 2.. are body
/// rows (P5.2 — before this, the delimiter row was parsed only to be
/// discarded; row/column mutation ops need it back, since inserting or
/// deleting a column has to touch every line at once, delimiter included).
/// `cells` holds exactly `cols` entries, same short-row-padded/long-row-
/// truncated GFM tolerance `ParsedTable::rows` has always had.
struct TableLine {
    int lineStart = 0;  //!< first byte of the line
    int lineEnd   = 0;  //!< exclusive of the line's own trailing '\n' (or
                         //!< end-of-buffer for the last physical line)
    std::vector<TableCellRange> cells;
};

/// rows[0] is the header row; rows[1..] are body rows (the alignment row
/// between them is consumed and discarded here too, same as before P5.2 —
/// `lines` below is where the delimiter row survives for callers that need
/// it). Each row has exactly `cols` cells (short rows padded with
/// zero-width ranges at the row's end byte, long rows truncated) — GFM
/// tolerance, mirrors TableDelegate.qml's parseTable padding rule.
struct ParsedTable {
    int cols = 0;
    std::vector<std::vector<TableCellRange>> rows;
    /// Every physical line, header + delimiter + body, in file order
    /// (P5.2). `lines[0]` is the header (same cells as `rows[0]`),
    /// `lines[1]` is the delimiter row, `lines[2..]` are the body rows
    /// (same cells as `rows[1..]`) — kept as a parallel structure rather
    /// than folding the delimiter row into `rows` so `rows`' existing
    /// row-major contract (P2.3 table selection, P5.1 navigation) is
    /// unchanged.
    std::vector<TableLine> lines;
    /// Per-column alignment parsed from the delimiter row (`lines[1]`).
    /// Size == cols.
    std::vector<TableAlign> alignment;
    bool ok = false;
};

/// Byte-oriented pipe-table tokenizer for a Table block's blockText().
///
/// T9 finding (spec §9): the two existing pipe-table parsers —
/// markoff-parser's `TableHandler` (`QTextDocument`/`QTextTable`-based, C3-
/// uncopyable here) and markoff-live's `TableDelegate.qml` `parseTable`
/// (QML JS, leaf-private, not reachable from C++ at all) — are both leaf-
/// private per the plan's anticipated gap. This is a from-scratch parse
/// against `blockText()` alone, reachable from markoff-core only.
///
/// Operates on raw UTF-8 bytes rather than QString/QChar: '|' and '\n' are
/// single-byte ASCII and never occur as UTF-8 continuation bytes, so the
/// byte offsets this produces are usable directly — no qtPosToByte round-
/// trip needed to get from "tokenizer position" to "buffer byte offset",
/// unlike the QString-based live-leaf equivalent.
ParsedTable parseTableBlock(const QByteArray &blockText);

/// Renders one delimiter-row cell's token bytes for `align` — `---`,
/// `:---`, `:---:`, `---:` for None/Left/Center/Right (P5.2). Content
/// only, no surrounding pipes/spaces — callers splice this into a cell's
/// own byte range or between two pipes as their edit needs. Fixed at the
/// minimum GFM-legal 3 dashes; this leaf does not attempt to keep the
/// delimiter row's visual column width matched to the header/body cells.
QByteArray alignmentCellText(TableAlign align);

/// Inverse of `alignmentCellText`: parses one delimiter-row cell's raw
/// bytes back into a `TableAlign`, tolerant of the padding spaces a real
/// table's delimiter row usually carries around the colons/dashes.
/// Unrecognized or empty content maps to `TableAlign::None`.
TableAlign parseAlignmentCell(const QByteArray &cellBytes);

}  // namespace Markoff::Canvas
