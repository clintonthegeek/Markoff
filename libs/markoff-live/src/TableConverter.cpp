// SPDX-License-Identifier: GPL-3.0-or-later
#include "TableConverter.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>
#include <QTextTableFormat>

#include <algorithm>

namespace Markoff {

// ---------------------------------------------------------------------------
// convert
// ---------------------------------------------------------------------------

void TableConverter::convert(QTextDocument *doc,
                             const QList<TableRegion> &regions)
{
    if (!doc || regions.isEmpty())
        return;

    // Process in reverse order so earlier character offsets stay valid.
    QList<TableRegion> sorted = regions;
    std::sort(sorted.begin(), sorted.end(),
              [](const TableRegion &a, const TableRegion &b) {
                  return a.startPos > b.startPos;
              });

    for (const TableRegion &region : sorted) {
        if (region.rows < 1 || region.cols < 1)
            continue;

        const int maxPos = doc->characterCount() - 1;
        const int start = qBound(0, region.startPos, maxPos);
        const int end = qBound(start, region.endPos, maxPos);

        QTextCursor cursor(doc);
        cursor.setPosition(start);
        cursor.setPosition(end, QTextCursor::KeepAnchor);

        cursor.beginEditBlock();
        cursor.removeSelectedText();

        QTextTableFormat fmt;
        fmt.setBorderCollapse(true);
        fmt.setCellPadding(8);
        fmt.setCellSpacing(0);
        fmt.setBorder(1);

        QTextTable *table = cursor.insertTable(region.rows, region.cols, fmt);

        // Populate header cells (row 0).
        for (int c = 0; c < region.cols && c < region.headers.size(); ++c) {
            table->cellAt(0, c).firstCursorPosition()
                .insertText(region.headers[c]);
        }

        // Populate data cells (rows 1+).
        for (int r = 0; r < region.dataRows.size(); ++r) {
            const QStringList &row = region.dataRows[r];
            for (int c = 0; c < region.cols && c < row.size(); ++c) {
                table->cellAt(r + 1, c).firstCursorPosition()
                    .insertText(row[c]);
            }
        }

        cursor.endEditBlock();

        TableRecord rec;
        rec.table = table;
        rec.rows = region.rows;
        rec.cols = region.cols;
        rec.alignments = region.alignments;
        m_records.prepend(rec); // prepend because we're processing in reverse
    }
}

// ---------------------------------------------------------------------------
// reconcile
// ---------------------------------------------------------------------------

bool TableConverter::reconcile(QTextDocument *doc,
                               const QList<TableRegion> &/*regions*/)
{
    if (!doc)
        return false;

    // Collect existing QTextTable frames from the document.
    QList<QTextTable *> existingTables;
    const auto frames = doc->rootFrame()->childFrames();
    for (QTextFrame *frame : frames) {
        if (auto *table = qobject_cast<QTextTable *>(frame))
            existingTables.append(table);
    }

    // Fast path: if counts match and all pointers match, nothing changed.
    if (existingTables.size() == m_records.size()) {
        bool allMatch = true;
        for (int i = 0; i < m_records.size(); ++i) {
            if (m_records[i].table != existingTables[i]) {
                allMatch = false;
                break;
            }
        }
        if (allMatch)
            return false;
    }

    // Something changed (table added or removed by a user operation).
    // Rebuild records from the document's actual QTextTable frames.
    // Never call convert() here — the regions parameter has pipe-text
    // coordinates from allMarkdown(), which do NOT map to document
    // positions when QTextTable frames are present. If tree-sitter
    // splits a table into multiple regions (e.g. with empty rows),
    // that is a parsing artifact — the document's frames are the
    // source of truth.
    m_records.clear();
    for (QTextTable *table : existingTables) {
        TableRecord rec;
        rec.table = table;
        rec.rows = table->rows();
        rec.cols = table->columns();
        m_records.append(rec);
    }

    return true;
}

// ---------------------------------------------------------------------------
// records / clear
// ---------------------------------------------------------------------------

const QList<TableConverter::TableRecord> &TableConverter::records() const
{
    return m_records;
}

void TableConverter::clear()
{
    m_records.clear();
}

} // namespace Markoff
