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

namespace {
    /// Concatenate every segment's text with a single '\n' between
    /// segments. Per the splitter invariant, the result must equal
    /// the source exactly.
    QString joinIdentity(const QList<MarkdownSegment> &segs)
    {
        QString out;
        for (int i = 0; i < segs.size(); ++i) {
            if (i > 0) out += QLatin1Char('\n');
            out += segs[i].text;
        }
        return out;
    }
}

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
    void testJoinIdentity_empty();
    void testJoinIdentity_textOnly();
    void testJoinIdentity_blockOnly();
    void testJoinIdentity_blockAtStart();
    void testJoinIdentity_blockAtEnd();
    void testJoinIdentity_leadingBlanks();
    void testJoinIdentity_trailingNewline();
    void testJoinIdentity_trailingBlanks();
    void testJoinIdentity_twoBlocksAdjacent();
    void testJoinIdentity_twoBlocksOneBlank();
    void testJoinIdentity_twoBlocksEightBlanks();
    void testJoinIdentity_threeBlocksMixedGaps();
    void testJoinIdentity_textWithBlockAndBlanks();
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

void TestSplitter::testJoinIdentity_empty()
{
    TreeSitterParser parser;
    const QString src;
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_textOnly()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("line1\nline2\n\nline4\n\n\nline7");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_blockOnly()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![alt](img.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_blockAtStart()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![alt](img.png)\n\nafter");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_blockAtEnd()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("before\n\n\n![alt](img.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_leadingBlanks()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("\n\n![alt](img.png)\n\nafter");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_trailingNewline()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![alt](img.png)\n");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_trailingBlanks()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("A\n\n\n\n");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_twoBlocksAdjacent()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![a](a.png)\n![b](b.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_twoBlocksOneBlank()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral("![a](a.png)\n\n![b](b.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_twoBlocksEightBlanks()
{
    // The banner case the user called out: 8 blank lines between blocks
    // in the source must survive the whole stack end-to-end.
    TreeSitterParser parser;
    const QString src = QStringLiteral(
        "![a](a.png)\n\n\n\n\n\n\n\n\n![b](b.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_threeBlocksMixedGaps()
{
    // a and b adjacent (single '\n'); b and c separated by three blanks.
    TreeSitterParser parser;
    const QString src = QStringLiteral(
        "![a](a.png)\n![b](b.png)\n\n\n\n![c](c.png)");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

void TestSplitter::testJoinIdentity_textWithBlockAndBlanks()
{
    TreeSitterParser parser;
    const QString src = QStringLiteral(
        "# Title\n\n\nIntro paragraph.\n\n\n\n![a](a.png)\n\nOutro.");
    QCOMPARE(joinIdentity(MarkdownSplitter::split(src, parser)), src);
}

QTEST_MAIN(TestSplitter)
#include "tst_splitter.moc"
