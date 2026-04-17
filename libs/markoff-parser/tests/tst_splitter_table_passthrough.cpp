// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-parser/MarkdownSplitter.h>
#include <markoff-parser/TreeSitterParser.h>

using namespace Markoff;

class TestSplitterTablePassthrough : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tableStaysInTextSegment();
    void tableWithSurroundingText();
    void imageStillSplitsOut();
};

void TestSplitterTablePassthrough::tableStaysInTextSegment()
{
    // A standalone pipe table should produce a single Text segment,
    // not a Table segment.
    TreeSitterParser parser;
    auto segments = MarkdownSplitter::split(
        QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |"), parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("| A | B |")));
    QVERIFY(segments[0].text.contains(QStringLiteral("| 1 | 2 |")));
}

void TestSplitterTablePassthrough::tableWithSurroundingText()
{
    // Text + table + text should produce a single Text segment
    // (the table is not a splitting boundary).
    TreeSitterParser parser;
    QString md = QStringLiteral(
        "# Heading\n\nBefore the table.\n\n"
        "| X | Y |\n|---|---|\n| a | b |\n\n"
        "After the table.");
    auto segments = MarkdownSplitter::split(md, parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("Heading")));
    QVERIFY(segments[0].text.contains(QStringLiteral("| X | Y |")));
    QVERIFY(segments[0].text.contains(QStringLiteral("After the table")));
}

void TestSplitterTablePassthrough::imageStillSplitsOut()
{
    // Images should still be split out as separate segments
    // (regression check — only tables changed).
    TreeSitterParser parser;
    QString md = QStringLiteral(
        "Text before.\n\n"
        "![alt text](image.png)\n\n"
        "Text after.");
    auto segments = MarkdownSplitter::split(md, parser);
    QCOMPARE(segments.size(), 3);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("Text before")));
    QCOMPARE(segments[1].type, MarkdownSegment::Image);
    QVERIFY(segments[1].text.contains(QStringLiteral("image.png")));
    QCOMPARE(segments[2].type, MarkdownSegment::Text);
    QVERIFY(segments[2].text.contains(QStringLiteral("Text after")));
}

QTEST_MAIN(TestSplitterTablePassthrough)
#include "tst_splitter_table_passthrough.moc"
