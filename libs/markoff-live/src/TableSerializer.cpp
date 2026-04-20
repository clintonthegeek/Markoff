// SPDX-License-Identifier: GPL-3.0-or-later
#include "TableSerializer.h"
#include <QTextTable>
#include <QTextCursor>

namespace Markoff {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static QString cellText(const QTextTable *table, int row, int col)
{
    QTextTableCell cell = table->cellAt(row, col);
    QTextCursor c = cell.firstCursorPosition();
    c.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    return c.selectedText();
}

static QString separatorCell(int width, Qt::Alignment align)
{
    // width is the content area (between the padding spaces).
    // Minimum 3 dashes.
    int dashCount = qMax(3, width);

    if (align == Qt::AlignCenter) {
        // :---:  (colon + dashes + colon = width)
        return QLatin1Char(':')
               + QString(dashCount - 2, QLatin1Char('-'))
               + QLatin1Char(':');
    }
    if (align == Qt::AlignLeft) {
        // :---  (colon + dashes = width)
        return QLatin1Char(':')
               + QString(dashCount - 1, QLatin1Char('-'));
    }
    if (align == Qt::AlignRight) {
        // ---:  (dashes + colon = width)
        return QString(dashCount - 1, QLatin1Char('-'))
               + QLatin1Char(':');
    }
    // No alignment
    return QString(dashCount, QLatin1Char('-'));
}

// ---------------------------------------------------------------------------
// serialize
// ---------------------------------------------------------------------------

QString TableSerializer::serialize(const QTextTable *table,
                                   const QList<Qt::Alignment> &alignments)
{
    if (!table) return {};

    const int rows = table->rows();
    const int cols = table->columns();
    if (rows == 0 || cols == 0) return {};

    // 1. Read all cell text into a 2D grid.
    QList<QStringList> grid;
    grid.reserve(rows);
    for (int r = 0; r < rows; ++r) {
        QStringList row;
        row.reserve(cols);
        for (int c = 0; c < cols; ++c)
            row.append(cellText(table, r, c));
        grid.append(row);
    }

    // 2. Compute column widths (max content width per col, min 3 for separator).
    QList<int> widths(cols, 3);
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            int len = grid[r][c].size();
            if (len > widths[c])
                widths[c] = len;
        }
    }

    // 3. Build lines.
    auto formatRow = [&](const QStringList &cells) -> QString {
        QString line;
        line.reserve(cols * 10);
        for (int c = 0; c < cols; ++c) {
            line += QStringLiteral("| ");
            QString content = (c < cells.size()) ? cells[c] : QString();
            line += content;
            // Pad to column width
            int pad = widths[c] - content.size();
            if (pad > 0)
                line += QString(pad, QLatin1Char(' '));
            line += QLatin1Char(' ');
        }
        line += QLatin1Char('|');
        return line;
    };

    QStringList lines;
    lines.reserve(rows + 1); // rows + separator

    // Header (row 0)
    lines.append(formatRow(grid[0]));

    // Separator row
    {
        QString sep;
        for (int c = 0; c < cols; ++c) {
            sep += QStringLiteral("| ");
            Qt::Alignment align = (c < alignments.size()) ? alignments[c] : Qt::Alignment{};
            QString marker = separatorCell(widths[c], align);
            sep += marker;
            // Pad marker to column width + (we already have the leading space)
            // marker length should equal widths[c] already, but pad if shorter
            int pad = widths[c] - marker.size();
            if (pad > 0)
                sep += QString(pad, QLatin1Char(' '));
            sep += QLatin1Char(' ');
        }
        sep += QLatin1Char('|');
        lines.append(sep);
    }

    // Data rows (row 1+)
    for (int r = 1; r < rows; ++r)
        lines.append(formatRow(grid[r]));

    return lines.join(QLatin1Char('\n'));
}

// ---------------------------------------------------------------------------
// parseAlignments
// ---------------------------------------------------------------------------

QList<Qt::Alignment> TableSerializer::parseAlignments(const QString &separatorLine)
{
    QList<Qt::Alignment> result;

    const QStringList parts = separatorLine.split(QLatin1Char('|'));
    for (const QString &part : parts) {
        QString cell = part.trimmed();
        if (cell.isEmpty()) continue;

        // Must contain at least some dashes to be a separator cell
        bool hasDash = false;
        for (QChar ch : cell) {
            if (ch == QLatin1Char('-')) { hasDash = true; break; }
        }
        if (!hasDash) continue;

        bool startsColon = cell.startsWith(QLatin1Char(':'));
        bool endsColon = cell.endsWith(QLatin1Char(':'));

        if (startsColon && endsColon)
            result.append(Qt::AlignCenter);
        else if (startsColon)
            result.append(Qt::AlignLeft);
        else if (endsColon)
            result.append(Qt::AlignRight);
        else
            result.append(Qt::Alignment{});
    }

    return result;
}

} // namespace Markoff
