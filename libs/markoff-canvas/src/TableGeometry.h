// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include <QByteArray>

namespace Markoff::Canvas {

/// A cell's content byte range within its block's blockText(), end
/// exclusive. Absolute (block-relative) offsets, usable directly as
/// d2ApplyBufferEdit arguments (spec C4 — one coordinate space).
struct TableCellRange {
    int start = 0;
    int end   = 0;
};

/// rows[0] is the header row; rows[1..] are body rows (the alignment row
/// between them is consumed and discarded, same as the live leaf's
/// parseTable). Each row has exactly `cols` cells (short rows padded with
/// zero-width ranges at the row's end byte, long rows truncated) — GFM
/// tolerance, mirrors TableDelegate.qml's parseTable padding rule.
struct ParsedTable {
    int cols = 0;
    std::vector<std::vector<TableCellRange>> rows;
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

}  // namespace Markoff::Canvas
