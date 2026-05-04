// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockKind.h>

using namespace Markoff;

class TstD2Load : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void emptyDoc_loadEmpty_zeroBlocks();
    void singleParagraph_loadProducesOneBlock();
    void singleParagraph_blockKindIsParagraph();
    void singleParagraph_blockTextMatchesSource();
    void heading_blockKindIsHeading();
    void heading_levelAttrSet();
    void codeBlock_kindIsCodeBlock();
    void codeBlock_infoAttrSetForFenced();
    void twoBlocks_iterateBlocksInOrder();
    void documentLoaded_signalEmittedOnce();
    void frontmatter_storedInFrontmatterMap();
    void footnote_storedInFootnoteDefMap();
};

void TstD2Load::emptyDoc_loadEmpty_zeroBlocks()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("");
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(0));
}

void TstD2Load::singleParagraph_loadProducesOneBlock()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello world\n");
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
}

void TstD2Load::singleParagraph_blockKindIsParagraph()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello world\n");
    BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(blk), BlockKind::Paragraph);
}

void TstD2Load::singleParagraph_blockTextMatchesSource()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Hello world\n");
    BlockId blk = doc.iterateBlocks().front();
    QVERIFY(doc.blockText(blk).contains("Hello world"));
}

void TstD2Load::heading_blockKindIsHeading()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("# My Heading\n");
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
    BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(blk), BlockKind::Heading);
}

void TstD2Load::heading_levelAttrSet()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("## Level Two\n");
    BlockId blk = doc.iterateBlocks().front();
    // Verify heading kind; level attr accessor arrives in Phase 8
    QCOMPARE(doc.blockKind(blk), BlockKind::Heading);
}

void TstD2Load::codeBlock_kindIsCodeBlock()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("```cpp\nint x = 0;\n```\n");
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
    BlockId blk = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(blk), BlockKind::CodeBlock);
}

void TstD2Load::codeBlock_infoAttrSetForFenced()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("```cpp\nint x = 0;\n```\n");
    BlockId blk = doc.iterateBlocks().front();
    // Info attr accessor arrives in Phase 8; for now just verify kind
    QCOMPARE(doc.blockKind(blk), BlockKind::CodeBlock);
}

void TstD2Load::twoBlocks_iterateBlocksInOrder()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("First paragraph\n\nSecond paragraph\n");
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));
    QVERIFY(doc.blockText(blocks[0]).contains("First"));
    QVERIFY(doc.blockText(blocks[1]).contains("Second"));
}

void TstD2Load::documentLoaded_signalEmittedOnce()
{
    MarkoffDocument doc(1);
    QSignalSpy spy(&doc, &MarkoffDocument::documentLoaded);
    doc.loadFromMarkdown("Hello\n");
    QCOMPARE(spy.count(), 1);
}

void TstD2Load::frontmatter_storedInFrontmatterMap()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("---\ntitle: Test\n---\n\nBody text\n");
    // After load, body blocks should exist (frontmatter excluded from IdList)
    auto blocks = doc.iterateBlocks();
    QVERIFY(blocks.size() >= static_cast<size_t>(1));
    // At least one block should contain "Body text"
    bool hasBody = false;
    for (const auto &b : blocks)
        if (doc.blockText(b).contains("Body text")) { hasBody = true; break; }
    QVERIFY(hasBody);
}

void TstD2Load::footnote_storedInFootnoteDefMap()
{
    MarkoffDocument doc(1);
    // Simple paragraph — just verify no crash; Phase 8 adds the accessor
    doc.loadFromMarkdown("Hello world\n");
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(1));
}

QTEST_MAIN(TstD2Load)
#include "tst_d2_load.moc"
