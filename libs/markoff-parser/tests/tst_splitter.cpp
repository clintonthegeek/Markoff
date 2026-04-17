// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QFile>
#include <QTextStream>
#include <markoff-parser/MarkdownSplitter.h>
#include <markoff-parser/TreeSitterParser.h>

#ifndef SHOWCASE_PATH
#define SHOWCASE_PATH "/home/clinton/dev/Corbomite/libs/markoff-parser/tests/showcase.md"
#endif

using namespace Markoff;

class TestSplitter : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testNoBlocks();
    void testSingleTable();
    void testSingleCodeBlock();
    void testTableBetweenText();
    void testMultipleBlocks();
    void testEmptyDocument();
    void testBlockAtStart();
    void testBlockAtEnd();
    void testShowcaseFile();
};

void TestSplitter::testNoBlocks()
{
    TreeSitterParser parser;
    auto segments = MarkdownSplitter::split(
        QStringLiteral("# Hello\n\nSome text here."), parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("Hello")));
}

void TestSplitter::testSingleTable()
{
    // Tables stay in text segments (converted to QTextTable by the editor)
    TreeSitterParser parser;
    auto segments = MarkdownSplitter::split(
        QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |"), parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("| A | B |")));
}

void TestSplitter::testSingleCodeBlock()
{
    // Code blocks are NOT split out — they stay as text, rendered
    // by the highlighter with syntax coloring (like the old Editor).
    TreeSitterParser parser;
    auto segments = MarkdownSplitter::split(
        QStringLiteral("```python\nprint('hi')\n```"), parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("python")));
}

void TestSplitter::testTableBetweenText()
{
    // Tables stay in text — the whole document is a single Text segment
    TreeSitterParser parser;
    QString md = QStringLiteral(
        "# Title\n\nBefore table.\n\n"
        "| A | B |\n|---|---|\n| 1 | 2 |\n\n"
        "After table.");
    auto segments = MarkdownSplitter::split(md, parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("Title")));
    QVERIFY(segments[0].text.contains(QStringLiteral("| A | B |")));
    QVERIFY(segments[0].text.contains(QStringLiteral("After table")));
}

void TestSplitter::testMultipleBlocks()
{
    // Both code blocks and tables stay in text segments
    TreeSitterParser parser;
    QString md = QStringLiteral(
        "Intro\n\n"
        "| A |\n|---|\n| 1 |\n\n"
        "Middle\n\n"
        "```js\nconsole.log('x')\n```\n\n"
        "End");
    auto segments = MarkdownSplitter::split(md, parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("Intro")));
    QVERIFY(segments[0].text.contains(QStringLiteral("| A |")));
    QVERIFY(segments[0].text.contains(QStringLiteral("console.log")));
    QVERIFY(segments[0].text.contains(QStringLiteral("End")));
}

void TestSplitter::testEmptyDocument()
{
    TreeSitterParser parser;
    auto segments = MarkdownSplitter::split(QString(), parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
}

void TestSplitter::testBlockAtStart()
{
    // Table at start stays in text — single segment
    TreeSitterParser parser;
    QString md = QStringLiteral("| A |\n|---|\n| 1 |\n\nText after.");
    auto segments = MarkdownSplitter::split(md, parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("| A |")));
    QVERIFY(segments[0].text.contains(QStringLiteral("Text after")));
}

void TestSplitter::testBlockAtEnd()
{
    // Table at end stays in text — single segment
    TreeSitterParser parser;
    QString md = QStringLiteral("Text before.\n\n| A |\n|---|\n| 1 |");
    auto segments = MarkdownSplitter::split(md, parser);
    QCOMPARE(segments.size(), 1);
    QCOMPARE(segments[0].type, MarkdownSegment::Text);
    QVERIFY(segments[0].text.contains(QStringLiteral("Text before")));
    QVERIFY(segments[0].text.contains(QStringLiteral("| A |")));
}

void TestSplitter::testShowcaseFile()
{
    TreeSitterParser parser;
    QFile f(QStringLiteral(SHOWCASE_PATH));
    QVERIFY2(f.open(QIODevice::ReadOnly), "Cannot open showcase.md");
    QString md = QTextStream(&f).readAll();
    auto segments = MarkdownSplitter::split(md, parser);

    // showcase.md has code blocks, tables, and 1 image.
    // Tables and code blocks stay in text; only images are split out.
    // So we expect: text, image, text (3 segments).
    for (int i = 0; i < segments.size(); ++i) {
        QString typeName = segments[i].type == MarkdownSegment::Text ? QStringLiteral("Text")
            : segments[i].type == MarkdownSegment::Image ? QStringLiteral("Image")
            : QStringLiteral("Other");
        QString preview = segments[i].text.left(50).replace(QLatin1Char('\n'), QStringLiteral("\\n"));
        qDebug() << i << typeName << "len:" << segments[i].text.size() << preview;
    }
    QVERIFY2(segments.size() >= 3,
             qPrintable(QStringLiteral("Expected >=3 segments, got %1").arg(segments.size())));

    // Tables should be embedded in text segments, not split out
    for (const auto &seg : segments) {
        QVERIFY2(seg.type != MarkdownSegment::Table,
                 "Tables should stay in text segments, not be split out");
    }

    // Last segment should contain "Frontmatter" (near end of file)
    QVERIFY(segments.last().text.contains(QStringLiteral("Frontmatter")));
}

QTEST_MAIN(TestSplitter)
#include "tst_splitter.moc"
