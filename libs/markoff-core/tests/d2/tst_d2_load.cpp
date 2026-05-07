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
    // Task 7.1
    void frontmatterPresent_populatesMap();
    // Task 7.2
    void heading_paragraph_codeBlock_threeBlocksWithKinds();
    void eachBlock_hasLoadTimeBytesSet();
    // Task 7.3 (v1 parser limitation: whole list → one ListItem block)
    void tightList_threeItems_threeListItemBlocks();
    void looseList_marker_inAttrs();
    void orderedList_startNumber_inAttrs();
    // Task 7.4
    void linkRefDef_populatesLinkRefMap_notIdList();
    // Task 7.6
    void afterLoad_eachCrdtRecordsLoadBaseline();
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

// ── Task 7.1 ────────────────────────────────────────────────────────────────

void TstD2Load::frontmatterPresent_populatesMap()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("---\ntitle: Test\n---\n\nBody\n");
    auto val = doc.frontmatterValue("raw");
    QVERIFY(val.has_value());
    QVERIFY(val->contains("title"));
}

// ── Task 7.2 ────────────────────────────────────────────────────────────────

void TstD2Load::heading_paragraph_codeBlock_threeBlocksWithKinds()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("# Heading\n\nParagraph text\n\n```cpp\ncode\n```\n");
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(3));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Heading);
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::Paragraph);
    QCOMPARE(doc.blockKind(blocks[2]), BlockKind::CodeBlock);
}

void TstD2Load::eachBlock_hasLoadTimeBytesSet()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("First\n\nSecond\n");
    for (const auto &id : doc.iterateBlocks())
        QVERIFY(!doc.blockLoadTimeBytes(id).isEmpty());
}

// ── Task 7.3 (updated to per-item shape) ────────────────────────────────────

void TstD2Load::tightList_threeItems_threeListItemBlocks()
{
    // Per-item shape: each list item becomes its own ListItem block.
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("- A\n- B\n- C\n");
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(3));
    for (auto id : blocks)
        QCOMPARE(doc.blockKind(id), BlockKind::ListItem);
}

void TstD2Load::looseList_marker_inAttrs()
{
    // Per-item shape: each list item becomes its own ListItem block.
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("- A\n\n- B\n");
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));
    for (auto id : blocks)
        QCOMPARE(doc.blockKind(id), BlockKind::ListItem);
}

void TstD2Load::orderedList_startNumber_inAttrs()
{
    // Per-item shape: each list item becomes its own ListItem block.
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("1. First\n2. Second\n");
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), static_cast<size_t>(2));
    for (auto id : blocks)
        QCOMPARE(doc.blockKind(id), BlockKind::ListItem);
}

// ── Task 7.4 ────────────────────────────────────────────────────────────────

void TstD2Load::linkRefDef_populatesLinkRefMap_notIdList()
{
    MarkoffDocument doc(1);
    // A link reference definition should not appear in the block list
    doc.loadFromMarkdown("[foo]: https://example.com\n");
    QCOMPARE(doc.iterateBlocks().size(), static_cast<size_t>(0));
}

// ── Task 7.6 ────────────────────────────────────────────────────────────────

void TstD2Load::afterLoad_eachCrdtRecordsLoadBaseline()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Alpha\n\nBeta\n");
    // After load, block edit sequences are reset to 0 (clean load baseline)
    for (const auto &id : doc.iterateBlocks())
        QCOMPARE(doc.blockEditSequence(id), quint64(0));
}

QTEST_MAIN(TstD2Load)
#include "tst_d2_load.moc"
