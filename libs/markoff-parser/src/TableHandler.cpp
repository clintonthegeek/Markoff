// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-parser/TableHandler.h>

#include <QTextDocument>
#include <QTextBlock>
#include <QTextTable>
#include <QTextTableFormat>
#include <QTextLength>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QRegularExpression>

namespace Markoff {

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

QStringList TableHandler::parseRow(const QString &line)
{
    QStringList cells;
    QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1Char('|')))
        trimmed = trimmed.mid(1);
    if (trimmed.endsWith(QLatin1Char('|')))
        trimmed.chop(1);
    const QStringList parts = trimmed.split(QLatin1Char('|'));
    for (const QString &part : parts)
        cells.append(part.trimmed());
    return cells;
}

Qt::Alignment TableHandler::parseAlignment(const QString &cell)
{
    QString trimmed = cell.trimmed();
    bool left = trimmed.startsWith(QLatin1Char(':'));
    bool right = trimmed.endsWith(QLatin1Char(':'));
    if (left && right) return Qt::AlignCenter;
    if (right) return Qt::AlignRight;
    return Qt::AlignLeft;
}

QList<ParsedTable> TableHandler::detectTables(QTextDocument *doc)
{
    QList<ParsedTable> tables;

    static const QRegularExpression pipeRowRe(
        QStringLiteral(R"(^\s*\|.*\|\s*$)"));
    static const QRegularExpression separatorRe(
        QStringLiteral(R"(^\s*\|[\s:]*-+[\s:]*(\|[\s:]*-+[\s:]*)*\|\s*$)"));

    QTextBlock block = doc->begin();
    while (block.isValid()) {
        if (!pipeRowRe.match(block.text()).hasMatch()) {
            block = block.next();
            continue;
        }

        QTextBlock sepBlock = block.next();
        if (!sepBlock.isValid() || !separatorRe.match(sepBlock.text()).hasMatch()) {
            block = block.next();
            continue;
        }

        ParsedTable table;
        table.firstBlock = block.blockNumber();
        table.headers = parseRow(block.text());

        QStringList sepCells = parseRow(sepBlock.text());
        for (const QString &cell : sepCells)
            table.alignments.append(parseAlignment(cell));
        while (table.alignments.size() < table.headers.size())
            table.alignments.append(Qt::AlignLeft);

        QTextBlock dataBlock = sepBlock.next();
        table.lastBlock = sepBlock.blockNumber();

        while (dataBlock.isValid() && pipeRowRe.match(dataBlock.text()).hasMatch()) {
            table.rows.append(parseRow(dataBlock.text()));
            table.lastBlock = dataBlock.blockNumber();
            dataBlock = dataBlock.next();
        }

        tables.append(table);
        block = dataBlock;
    }

    return tables;
}

// ---------------------------------------------------------------------------
// QTextTable conversion
// ---------------------------------------------------------------------------

QTextTable *TableHandler::convertToQTextTable(QTextDocument *doc,
                                               const ParsedTable &table)
{
    if (!doc) return nullptr;

    int numCols = table.headers.size();
    int numDataRows = table.rows.size();
    int numRows = 1 + qMax(numDataRows, 1); // header + at least 1 data row

    // Find the text range covering firstBlock..lastBlock
    QTextBlock firstBlock = doc->findBlockByNumber(table.firstBlock);
    QTextBlock lastBlock = doc->findBlockByNumber(table.lastBlock);
    if (!firstBlock.isValid() || !lastBlock.isValid())
        return nullptr;

    QTextCursor cursor(doc);
    cursor.setPosition(firstBlock.position());
    cursor.setPosition(lastBlock.position() + lastBlock.length() - 1,
                       QTextCursor::KeepAnchor);

    cursor.beginEditBlock();

    // Remove the selected pipe text
    cursor.removeSelectedText();

    // If the removal left an empty block before the cursor, clean it up
    // by merging with the previous block (the removeSelectedText can leave
    // a trailing empty block).
    // Actually, after removeSelectedText the cursor is at the position where
    // the table was. We just insert the table there.

    // Set up table format
    QTextTableFormat fmt;
    fmt.setBorderCollapse(true);
    fmt.setCellPadding(4);
    fmt.setCellSpacing(0);
    fmt.setBorder(0);

    QList<QTextLength> constraints;
    qreal pct = 100.0 / numCols;
    for (int c = 0; c < numCols; ++c)
        constraints.append(QTextLength(QTextLength::PercentageLength, pct));
    fmt.setColumnWidthConstraints(constraints);

    // Insert the table
    QTextTable *tt = cursor.insertTable(numRows, numCols, fmt);

    // Populate header cells (row 0) with bold text
    QTextCharFormat boldFmt;
    boldFmt.setFontWeight(QFont::Bold);

    for (int c = 0; c < numCols; ++c) {
        QTextTableCell cell = tt->cellAt(0, c);
        QTextCursor cellCursor = cell.firstCursorPosition();
        cellCursor.insertText(table.headers[c], boldFmt);
    }

    // Populate data cells (rows 1+)
    for (int r = 0; r < numDataRows; ++r) {
        for (int c = 0; c < numCols && c < table.rows[r].size(); ++c) {
            QTextTableCell cell = tt->cellAt(r + 1, c);
            QTextCursor cellCursor = cell.firstCursorPosition();
            cellCursor.insertText(table.rows[r][c]);
        }
    }

    cursor.endEditBlock();

    return tt;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

QString TableHandler::serializeToMarkdown(QTextTable *table,
                                           const QList<Qt::Alignment> &alignments)
{
    if (!table) return {};

    int rows = table->rows();
    int cols = table->columns();

    // Read all cell text
    QList<QStringList> cellText;
    for (int r = 0; r < rows; ++r) {
        QStringList rowText;
        for (int c = 0; c < cols; ++c) {
            QTextTableCell cell = table->cellAt(r, c);
            QTextCursor start = cell.firstCursorPosition();
            QTextCursor end = cell.lastCursorPosition();
            QTextBlock b = start.block();
            QString text;
            while (b.isValid() && b.position() <= end.block().position()) {
                if (!text.isEmpty()) text += QLatin1Char(' ');
                text += b.text();
                b = b.next();
            }
            rowText.append(text);
        }
        cellText.append(rowText);
    }

    // Compute column widths (minimum 3 for separator dashes)
    QList<int> colWidths(cols, 3);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (cellText[r][c].length() > colWidths[c])
                colWidths[c] = cellText[r][c].length();
        }
    }

    QString md;

    // Header row (row 0)
    md += QLatin1Char('|');
    for (int c = 0; c < cols; ++c) {
        md += QLatin1Char(' ')
              + cellText[0][c].leftJustified(colWidths[c])
              + QStringLiteral(" |");
    }
    md += QLatin1Char('\n');

    // Separator row
    md += QLatin1Char('|');
    for (int c = 0; c < cols; ++c) {
        Qt::Alignment align = c < alignments.size() ? alignments[c] : Qt::AlignLeft;
        QString sep(colWidths[c], QLatin1Char('-'));
        if (align == Qt::AlignCenter) {
            sep[0] = QLatin1Char(':');
            sep[sep.size() - 1] = QLatin1Char(':');
        } else if (align == Qt::AlignRight) {
            sep[sep.size() - 1] = QLatin1Char(':');
        }
        md += sep + QLatin1Char('|');
    }
    md += QLatin1Char('\n');

    // Data rows (row 1+)
    for (int r = 1; r < rows; ++r) {
        md += QLatin1Char('|');
        for (int c = 0; c < cols; ++c) {
            md += QLatin1Char(' ')
                  + cellText[r][c].leftJustified(colWidths[c])
                  + QStringLiteral(" |");
        }
        md += QLatin1Char('\n');
    }

    return md;
}

} // namespace Markoff
