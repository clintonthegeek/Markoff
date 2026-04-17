// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TABLESERIALIZER_H
#define MARKOFF_TABLESERIALIZER_H

#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QTextTable;
QT_END_NAMESPACE

namespace Markoff {

/// Static utility: converts a QTextTable into auto-formatted pipe markdown.
class TableSerializer {
public:
    TableSerializer() = delete;

    /// Serialize a QTextTable to pipe-delimited markdown with optional alignments.
    /// Row 0 is treated as the header. The separator row is emitted after it.
    static QString serialize(const QTextTable *table,
                             const QList<Qt::Alignment> &alignments = {});

    /// Parse a separator line (e.g. "| :--- | :---: | ---: |") into alignments.
    /// Returns Qt::AlignLeft, Qt::AlignCenter, Qt::AlignRight, or Qt::Alignment{}
    /// (none/default) for each column.
    static QList<Qt::Alignment> parseAlignments(const QString &separatorLine);
};

} // namespace Markoff

#endif // MARKOFF_TABLESERIALIZER_H
