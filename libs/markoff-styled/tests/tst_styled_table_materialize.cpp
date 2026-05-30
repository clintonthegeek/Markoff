// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

#include <markoff/core/OpaqueBlockRenderer.h>  // OpaqueBlockKeyProperty

#include "../src/TableFrame.h"

using namespace Markoff::Styled;

namespace {
QTextTable *firstTable(QTextDocument *doc) {
    for (QTextFrame *f : doc->rootFrame()->childFrames())
        if (auto *t = qobject_cast<QTextTable *>(f)) return t;
    return nullptr;
}
}  // namespace

class TstStyledTableMaterialize : public QObject {
    Q_OBJECT
private slots:
    void builds_3x2_grid_with_text_and_alignment() {
        ParsedTable t = parsePipeTable(
            "| H1 | H2 |\n| :--- | ---: |\n| a | b |\n| c | d |");
        QVERIFY(t.ok);

        QTextDocument doc;
        QTextCursor c(&doc);
        const int span = materializeTable(c, t,
                                          QStringLiteral("markoff-table:42"), 1.0);
        QVERIFY(span > 0);

        QTextTable *tbl = firstTable(&doc);
        QVERIFY(tbl != nullptr);
        QCOMPARE(tbl->rows(), 3);     // header + 2 body
        QCOMPARE(tbl->columns(), 2);
        QCOMPARE(tbl->cellAt(0, 0).firstCursorPosition().block().text(),
                 QStringLiteral("H1"));
        QCOMPARE(tbl->cellAt(2, 1).firstCursorPosition().block().text(),
                 QStringLiteral("d"));
        // Frame tagged with the key the binding matches on.
        QCOMPARE(tbl->frameFormat().stringProperty(
                     Markoff::OpaqueBlockKeyProperty),
                 QStringLiteral("markoff-table:42"));
        // Column 1 is right-aligned on every row (Qt has no per-column align;
        // it's applied per cell-block).
        QCOMPARE(tbl->cellAt(0, 1).firstCursorPosition().blockFormat()
                     .alignment(), Qt::AlignRight);
        QCOMPARE(tbl->cellAt(2, 1).firstCursorPosition().blockFormat()
                     .alignment(), Qt::AlignRight);
        // Column 0 left-aligned.
        QCOMPARE(tbl->cellAt(1, 0).firstCursorPosition().blockFormat()
                     .alignment(), Qt::AlignLeft);
    }

    void empty_cell_is_blank_not_crashing() {
        ParsedTable t = parsePipeTable("| A | B |\n|---|---|\n| only |");
        QVERIFY(t.ok);
        QTextDocument doc;
        QTextCursor c(&doc);
        materializeTable(c, t, QStringLiteral("markoff-table:1"), 1.0);
        QTextTable *tbl = firstTable(&doc);
        QVERIFY(tbl != nullptr);
        QCOMPARE(tbl->cellAt(1, 0).firstCursorPosition().block().text(),
                 QStringLiteral("only"));
        QCOMPARE(tbl->cellAt(1, 1).firstCursorPosition().block().text(),
                 QString());
    }
};

QTEST_MAIN(TstStyledTableMaterialize)
#include "tst_styled_table_materialize.moc"
