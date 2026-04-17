// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_TABLECONVERTER_H
#define MARKOFF_TABLECONVERTER_H

#include <QList>
#include <QString>

QT_BEGIN_NAMESPACE
class QTextDocument;
class QTextTable;
QT_END_NAMESPACE

namespace Markoff {

/// Converts pipe-delimited markdown table text into QTextTable frames
/// within a QTextDocument. Tracks created tables as TableRecords for
/// reparse reconciliation.
class TableConverter {
public:
    /// Describes a region of pipe-table text to convert.
    struct TableRegion {
        int startPos = 0;    ///< char offset in QTextDocument
        int endPos = 0;      ///< char offset end
        int rows = 0;        ///< total rows (header + data)
        int cols = 0;
        QStringList headers;
        QList<QStringList> dataRows;
        QList<Qt::Alignment> alignments;
    };

    /// Tracks a converted QTextTable for reconciliation.
    struct TableRecord {
        QTextTable *table = nullptr;
        int rows = 0;
        int cols = 0;
        QList<Qt::Alignment> alignments;
    };

    /// Convert pipe text regions to QTextTable frames in the document.
    /// Processes regions in reverse order so earlier positions aren't shifted.
    void convert(QTextDocument *doc, const QList<TableRegion> &regions);

    /// Reconcile parser output against existing tables.
    /// Returns true if any changes were made.
    bool reconcile(QTextDocument *doc, const QList<TableRegion> &regions);

    const QList<TableRecord> &records() const;
    void clear();

private:
    QList<TableRecord> m_records;
};

} // namespace Markoff

#endif // MARKOFF_TABLECONVERTER_H
