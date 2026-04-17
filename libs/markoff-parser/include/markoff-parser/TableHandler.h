// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TABLEHANDLER_H
#define MARKOFF_TABLEHANDLER_H

#include <QString>
#include <QStringList>
#include <QList>

class QTextDocument;
class QTextTable;

namespace Markoff {

/// Parsed representation of a markdown pipe table
struct ParsedTable {
    QStringList headers;
    QList<Qt::Alignment> alignments;
    QList<QStringList> rows;
    int firstBlock = -1;
    int lastBlock = -1;
};

/// Handles detection, conversion, and serialization of markdown tables.
class TableHandler {
public:
    static QList<ParsedTable> detectTables(QTextDocument *doc);
    static QTextTable *convertToQTextTable(QTextDocument *doc,
                                            const ParsedTable &table);
    static QString serializeToMarkdown(QTextTable *table,
                                        const QList<Qt::Alignment> &alignments);
    static QStringList parseRow(const QString &line);
    static Qt::Alignment parseAlignment(const QString &cell);
};

} // namespace Markoff

#endif // MARKOFF_TABLEHANDLER_H
