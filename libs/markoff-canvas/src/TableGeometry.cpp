// SPDX-License-Identifier: GPL-3.0-or-later
#include "TableGeometry.h"

#include <optional>

namespace Markoff::Canvas {

namespace {

struct Line {
    int start = 0;
    int end   = 0;  //!< exclusive of the '\n'
};

std::vector<Line> splitLines(const QByteArray &src)
{
    std::vector<Line> lines;
    int lineStart = 0;
    for (int i = 0; i < src.size(); ++i) {
        if (src[i] == '\n') {
            lines.push_back({lineStart, i});
            lineStart = i + 1;
        }
    }
    if (lineStart < src.size())
        lines.push_back({lineStart, int(src.size())});
    return lines;
}

/// Cells between adjacent unescaped '|' in `line` (escape handling for
/// '\|' is a known limitation, same as the live leaf's tokenizer). A valid
/// pipe-table line has at least two pipes (one on each side); returns
/// nullopt otherwise.
std::optional<std::vector<TableCellRange>> tokenizeLine(const QByteArray &src, Line line)
{
    std::vector<int> pipes;
    for (int j = line.start; j < line.end; ++j) {
        if (src[j] == '|')
            pipes.push_back(j);
    }
    if (pipes.size() < 2)
        return std::nullopt;

    std::vector<TableCellRange> ranges;
    ranges.reserve(pipes.size() - 1);
    for (size_t k = 0; k + 1 < pipes.size(); ++k)
        ranges.push_back({pipes[k] + 1, pipes[k + 1]});
    return ranges;
}

/// `cols`-sized cell list for `line`, tokenized and padded/truncated to
/// match the header's column count — the same GFM tolerance every row
/// (including, as of P5.2, the delimiter row) gets.
std::vector<TableCellRange> tokenizeLinePadded(const QByteArray &src, Line line, int cols)
{
    std::vector<TableCellRange> cells;
    if (const auto row = tokenizeLine(src, line))
        cells = *row;
    cells.resize(size_t(cols), TableCellRange{line.end, line.end});
    return cells;
}

}  // namespace

ParsedTable parseTableBlock(const QByteArray &blockText)
{
    ParsedTable result;

    const std::vector<Line> lines = splitLines(blockText);
    if (lines.size() < 2)  // need header + alignment row
        return result;

    const auto header = tokenizeLine(blockText, lines[0]);
    if (!header || header->empty())
        return result;

    const int cols = int(header->size());
    result.cols = cols;
    result.rows.push_back(*header);
    result.lines.push_back(TableLine{lines[0].start, lines[0].end, *header});

    // lines[1] is the alignment row — no longer discarded outright (P5.2):
    // its cells and per-column alignment survive in `lines`/`alignment`,
    // it just still isn't folded into `rows` (unchanged row-major contract
    // for the render/navigation callers that predate P5.2).
    const std::vector<TableCellRange> alignCells = tokenizeLinePadded(blockText, lines[1], cols);
    result.lines.push_back(TableLine{lines[1].start, lines[1].end, alignCells});
    result.alignment.reserve(size_t(cols));
    for (const TableCellRange &c : alignCells) {
        result.alignment.push_back(
            parseAlignmentCell(blockText.mid(c.start, c.end - c.start)));
    }

    for (size_t r = 2; r < lines.size(); ++r) {
        const Line &rowLine = lines[r];
        if (rowLine.start == rowLine.end)
            continue;  // trailing blank-line residue

        const std::vector<TableCellRange> cells = tokenizeLinePadded(blockText, rowLine, cols);
        result.rows.push_back(cells);
        result.lines.push_back(TableLine{rowLine.start, rowLine.end, cells});
    }

    result.ok = true;
    return result;
}

QByteArray alignmentCellText(TableAlign align)
{
    switch (align) {
    case TableAlign::Left:   return ":---";
    case TableAlign::Center: return ":---:";
    case TableAlign::Right:  return "---:";
    case TableAlign::None:   break;
    }
    return "---";
}

TableAlign parseAlignmentCell(const QByteArray &cellBytes)
{
    const QByteArray trimmed = cellBytes.trimmed();
    if (trimmed.isEmpty())
        return TableAlign::None;
    const bool left  = trimmed.startsWith(':');
    const bool right = trimmed.endsWith(':');
    if (left && right)  return TableAlign::Center;
    if (right)           return TableAlign::Right;
    if (left)            return TableAlign::Left;
    return TableAlign::None;
}

}  // namespace Markoff::Canvas
