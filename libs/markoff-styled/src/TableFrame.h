// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <Qt>

class QTextCursor;

namespace Markoff::Styled {

/// Parsed form of a GFM pipe-table block buffer. Delegate-local; the canonical
/// content remains the block buffer in MarkoffDocument.
struct ParsedTable {
    QStringList          header;       ///< M columns
    QList<QStringList>   body;         ///< N rows × M columns (padded to M)
    QList<Qt::Alignment> alignments;   ///< M entries from the |:--:| row
    bool                 ok = false;   ///< false → not a table; degrade to text
};

/// Tokenize a GFM pipe-table buffer (header row + `:---:` separator row + body
/// rows). Ragged body rows are padded with empty cells to the header column
/// count. `ok == false` if the buffer is not a valid table.
ParsedTable parsePipeTable(const QByteArray &buffer);

/// Insert a read-only QTextTable for `t` at `at`, tagged with `commentKey`
/// (stored in the frame format's OpaqueBlockKeyProperty so the binding can
/// match the frame to its model block). Header row is bold; per-cell block
/// alignment honors `t.alignments`. Returns the number of QTextDocument
/// characters the inserted table occupies.
int materializeTable(QTextCursor &at, const ParsedTable &t,
                     const QString &commentKey, qreal fontScale);

}  // namespace Markoff::Styled
