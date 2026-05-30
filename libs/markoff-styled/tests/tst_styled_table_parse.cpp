// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest/QtTest>

#include "../src/TableFrame.h"

using namespace Markoff::Styled;

class TstStyledTableParse : public QObject {
    Q_OBJECT
private slots:
    void parses_3x2_with_alignment() {
        const QByteArray src =
            "| H1 | H2 |\n| :--- | ---: |\n| a | b |\n| c | d |";
        ParsedTable t = parsePipeTable(src);
        QVERIFY(t.ok);
        QCOMPARE(t.header.size(), 2);
        QCOMPARE(t.header.at(0), QStringLiteral("H1"));
        QCOMPARE(t.header.at(1), QStringLiteral("H2"));
        QCOMPARE(t.body.size(), 2);
        QCOMPARE(t.body.at(0).at(1), QStringLiteral("b"));
        QCOMPARE(t.body.at(1).at(0), QStringLiteral("c"));
        QCOMPARE(t.alignments.at(0), Qt::AlignLeft);
        QCOMPARE(t.alignments.at(1), Qt::AlignRight);
    }

    void center_alignment() {
        ParsedTable t = parsePipeTable("| H |\n| :---: |\n| x |");
        QVERIFY(t.ok);
        QCOMPARE(t.alignments.at(0), Qt::AlignHCenter);
    }

    void ragged_row_padded_to_header() {
        ParsedTable t = parsePipeTable("| A | B |\n|---|---|\n| only |");
        QVERIFY(t.ok);
        QCOMPARE(t.body.size(), 1);
        QCOMPARE(t.body.at(0).size(), 2);
        QCOMPARE(t.body.at(0).at(0), QStringLiteral("only"));
        QCOMPARE(t.body.at(0).at(1), QString());
    }

    void non_table_is_not_ok() {
        QVERIFY(!parsePipeTable("just a paragraph").ok);
        QVERIFY(!parsePipeTable("| no separator row |").ok);
        QVERIFY(!parsePipeTable("").ok);
        // Second line present but not a separator (no dashes).
        QVERIFY(!parsePipeTable("| A | B |\n| x | y |").ok);
    }
};

QTEST_MAIN(TstStyledTableParse)
#include "tst_styled_table_parse.moc"
