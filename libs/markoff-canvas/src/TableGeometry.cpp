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

    // lines[1] is the alignment row — consumed, not rendered as a body row
    // (no alignment UI, plan T9 scope).
    for (size_t r = 2; r < lines.size(); ++r) {
        const Line &rowLine = lines[r];
        if (rowLine.start == rowLine.end)
            continue;  // trailing blank-line residue

        std::vector<TableCellRange> cells;
        if (const auto row = tokenizeLine(blockText, rowLine))
            cells = *row;
        // Pad short rows / truncate long rows to the header's column count
        // (GFM tolerance, mirrors the live leaf's parseTable).
        cells.resize(size_t(cols), TableCellRange{rowLine.end, rowLine.end});
        result.rows.push_back(std::move(cells));
    }

    result.ok = true;
    return result;
}

}  // namespace Markoff::Canvas
