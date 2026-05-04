// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-parser/Document.h>

using Markoff::Document;
using Markoff::TopLevelBlock;
using Kind = TopLevelBlock::Kind;

class TestDocumentTopLevelBlocks : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void empty();
    void frontmatterOnly();
    void singleParagraph();
    void threeParagraphs();
    void atxHeadingsAllLevels();
    void setextHeadings();
    void fencedCodeBlock();
    void indentedCodeBlock();
    void blockQuote();
    void bulletList();
    void numberedList();
    void thematicBreak();
    void htmlBlock();
    void linkReferenceDefinition();
    void pipeTable();
    void mixedDocumentInOrder();
    void noTrailingNewline();
    void markerProducesParagraph();
    void markerRunProducesMultiple();
};

static QList<TopLevelBlock> blocksOf(const QString &source)
{
    return Document::fromMarkdown(source)->topLevelBlocks();
}

void TestDocumentTopLevelBlocks::empty()
{
    QCOMPARE(blocksOf(QString()).size(), 0);
}

void TestDocumentTopLevelBlocks::frontmatterOnly()
{
    // Frontmatter is stripped by the parse pipeline before tree-sitter
    // sees the body, so a frontmatter-only document parses to an empty
    // body and yields no top-level blocks.
    auto blocks = blocksOf(QStringLiteral("---\ntitle: x\n---\n"));
    QCOMPARE(blocks.size(), 0);
}

void TestDocumentTopLevelBlocks::singleParagraph()
{
    const QString src = QStringLiteral("Just one paragraph.\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::Paragraph);
    QCOMPARE(blocks[0].byteStart, 0);
    QVERIFY(blocks[0].byteEnd > 0);
    QVERIFY(blocks[0].byteEnd <= src.toUtf8().size());
}

void TestDocumentTopLevelBlocks::threeParagraphs()
{
    const QString src = QStringLiteral("alpha\n\nbeta\n\ngamma\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 3);
    for (const auto &b : blocks)
        QCOMPARE(b.kind, Kind::Paragraph);
    // In document order, non-overlapping, monotonically increasing.
    QVERIFY(blocks[0].byteEnd <= blocks[1].byteStart);
    QVERIFY(blocks[1].byteEnd <= blocks[2].byteStart);
    QVERIFY(blocks[2].byteEnd <= src.toUtf8().size());
}

void TestDocumentTopLevelBlocks::atxHeadingsAllLevels()
{
    const QString src = QStringLiteral(
        "# h1\n\n## h2\n\n### h3\n\n#### h4\n\n##### h5\n\n###### h6\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 6);
    for (int i = 0; i < 6; ++i) {
        QCOMPARE(blocks[i].kind, Kind::AtxHeading);
        QCOMPARE(blocks[i].headingLevel, i + 1);
    }
}

void TestDocumentTopLevelBlocks::setextHeadings()
{
    const QString src = QStringLiteral("Title One\n=========\n\nTitle Two\n---------\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].kind, Kind::SetextHeading);
    QCOMPARE(blocks[0].headingLevel, 1);
    QCOMPARE(blocks[1].kind, Kind::SetextHeading);
    QCOMPARE(blocks[1].headingLevel, 2);
}

void TestDocumentTopLevelBlocks::fencedCodeBlock()
{
    const QString src = QStringLiteral("```python\nprint('hi')\nx = 1\n```\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::FencedCodeBlock);
    QCOMPARE(blocks[0].codeLanguage, QStringLiteral("python"));
    QVERIFY(blocks[0].codeText.contains(QStringLiteral("print('hi')")));
    QVERIFY(blocks[0].codeText.contains(QStringLiteral("x = 1")));
    // codeText should NOT contain the fences themselves.
    QVERIFY(!blocks[0].codeText.contains(QStringLiteral("```")));
}

void TestDocumentTopLevelBlocks::indentedCodeBlock()
{
    // Need a blank line above the indented code block to dissociate
    // it from the preceding paragraph.
    const QString src = QStringLiteral("para\n\n    code line one\n    code line two\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].kind, Kind::Paragraph);
    QCOMPARE(blocks[1].kind, Kind::IndentedCodeBlock);
    QVERIFY(!blocks[1].codeText.isEmpty());
}

void TestDocumentTopLevelBlocks::blockQuote()
{
    auto blocks = blocksOf(QStringLiteral("> hello world\n"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::BlockQuote);
}

void TestDocumentTopLevelBlocks::bulletList()
{
    auto blocks = blocksOf(QStringLiteral("- a\n- b\n- c\n"));
    QCOMPARE(blocks.size(), 1);
    // v1: the grammar does not surface a tight/loose attribute on the
    // `list` node, so we default to ListTight. Either tight or loose
    // is acceptable.
    QVERIFY(blocks[0].kind == Kind::ListTight || blocks[0].kind == Kind::ListLoose);
}

void TestDocumentTopLevelBlocks::numberedList()
{
    auto blocks = blocksOf(QStringLiteral("1. a\n2. b\n"));
    QCOMPARE(blocks.size(), 1);
    QVERIFY(blocks[0].kind == Kind::ListTight || blocks[0].kind == Kind::ListLoose);
}

void TestDocumentTopLevelBlocks::thematicBreak()
{
    auto blocks = blocksOf(QStringLiteral("---\n"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::ThematicBreak);
}

void TestDocumentTopLevelBlocks::htmlBlock()
{
    auto blocks = blocksOf(QStringLiteral("<div>foo</div>\n"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::HtmlBlock);
}

void TestDocumentTopLevelBlocks::linkReferenceDefinition()
{
    auto blocks = blocksOf(QStringLiteral("[label]: http://example.com\n"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::LinkReferenceDefinition);
}

void TestDocumentTopLevelBlocks::pipeTable()
{
    const QString src = QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |\n");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].kind, Kind::Table);
}

void TestDocumentTopLevelBlocks::mixedDocumentInOrder()
{
    const QString src = QStringLiteral(
        "# Title\n"
        "\n"
        "Intro paragraph.\n"
        "\n"
        "```js\nconst x = 1;\n```\n"
        "\n"
        "> A quote.\n"
        "\n"
        "- list item\n"
        "\n"
        "---\n"
        "\n"
        "Final paragraph.\n");
    auto blocks = blocksOf(src);
    QVERIFY(blocks.size() >= 7);

    // Verify document order: each block's start must be >= the previous
    // block's end (or equal — adjacent blocks may share a boundary).
    for (int i = 1; i < blocks.size(); ++i) {
        QVERIFY2(blocks[i].byteStart >= blocks[i - 1].byteEnd,
                 qPrintable(QStringLiteral("Block %1 overlaps preceding").arg(i)));
    }

    // First block must be the heading.
    QCOMPARE(blocks[0].kind, Kind::AtxHeading);
    QCOMPARE(blocks[0].headingLevel, 1);

    // Verify we see at least one of each expected kind, in order.
    bool sawParagraph = false, sawCode = false, sawQuote = false;
    bool sawList = false, sawHr = false;
    for (const auto &b : blocks) {
        if (b.kind == Kind::Paragraph)        sawParagraph = true;
        if (b.kind == Kind::FencedCodeBlock)  sawCode      = true;
        if (b.kind == Kind::BlockQuote)       sawQuote     = true;
        if (b.kind == Kind::ListTight ||
            b.kind == Kind::ListLoose)        sawList      = true;
        if (b.kind == Kind::ThematicBreak)    sawHr        = true;
    }
    QVERIFY(sawParagraph);
    QVERIFY(sawCode);
    QVERIFY(sawQuote);
    QVERIFY(sawList);
    QVERIFY(sawHr);
}

void TestDocumentTopLevelBlocks::noTrailingNewline()
{
    // Document ending without a trailing newline: the last block's
    // byteEnd should equal the body's UTF-8 size.
    const QString src = QStringLiteral("alpha\n\nbeta");
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[1].byteEnd, src.toUtf8().size());
}

void TestDocumentTopLevelBlocks::markerProducesParagraph()
{
    // U+200B ZWSP at end of "hello\n\n" must produce a 2-block parse:
    // paragraph "hello", paragraph "<ZWSP>". This contract is what the
    // marker-paragraph design relies on (spec §3, premise M2).
    const QString src = QStringLiteral("hello\n\n") + QChar(0x200B);
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].kind, Kind::Paragraph);
    QCOMPARE(blocks[1].kind, Kind::Paragraph);
    QCOMPARE(blocks[1].byteEnd - blocks[1].byteStart, 3); // ZWSP is 3 UTF-8 bytes
}

void TestDocumentTopLevelBlocks::markerRunProducesMultiple()
{
    // Two consecutive marker-only paragraphs (separated by \n\n) parse
    // as two distinct paragraph blocks. The MarkerScrubber's run-collapse
    // mode (premise M6) targets exactly this shape.
    const QString src = QStringLiteral("hello\n\n")
                       + QChar(0x200B) + QStringLiteral("\n\n")
                       + QChar(0x200B);
    auto blocks = blocksOf(src);
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[1].kind, Kind::Paragraph);
    QCOMPARE(blocks[2].kind, Kind::Paragraph);
}

QTEST_GUILESS_MAIN(TestDocumentTopLevelBlocks)
#include "tst_document_top_level_blocks.moc"
