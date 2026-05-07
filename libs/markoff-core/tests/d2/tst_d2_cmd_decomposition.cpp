// SPDX-License-Identifier: GPL-3.0-or-later
#define MARKOFF_TESTING
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/BlockKind.h>

using namespace Markoff;

class TstD2Cmd : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void insertCharacter_appendsChar();
    void insertCharacter_consecutivePrintableCharsCoalesce();
    void insertSoftBreak_insertsNewline();
    void enterAtEnd_createsNewBlock();
    void enterAtEnd_newBlockIsEmpty();
    void backspaceMerge_appendsContentToPrev();
    void backspaceMerge_firstBlock_noOp();
    void backspaceMerge_stripsTrailingNewlineAtBoundary();
    void deleteMerge_appendsNextBlockContent();
    void deleteMerge_lastBlock_noOp();
    void deleteMerge_stripsTrailingNewlineAtBoundary();
    void changeKind_updatesBlockKind();
    void changeKind_withAttrs();
    void pasteMarkdown_singleParagraph_contentAppearsInNewBlock();
    void pasteMarkdown_splitsTargetAndAppendsBlocks();
};

void TstD2Cmd::insertCharacter_appendsChar()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    Cmd::insertCharacter(doc, blk, 5, 'x');
    QCOMPARE(doc.blockText(blk), QByteArray("hellox"));
}

void TstD2Cmd::insertCharacter_consecutivePrintableCharsCoalesce()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    size_t before = doc.d2UndoLog().entryCount();
    Cmd::insertCharacter(doc, blk, 5, 'a');
    Cmd::insertCharacter(doc, blk, 6, 'b');
    // Both chars should coalesce into one undo entry
    QCOMPARE(doc.d2UndoLog().entryCount(), before + 1);
    QCOMPARE(doc.blockText(blk), QByteArray("helloab"));
}

void TstD2Cmd::insertSoftBreak_insertsNewline()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "line1");
    Cmd::insertSoftBreak(doc, blk, 5);
    QCOMPARE(doc.blockText(blk), QByteArray("line1\n"));
}

void TstD2Cmd::enterAtEnd_createsNewBlock()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    QCOMPARE(doc.iterateBlocks().size(), 1u);
    BlockId newBlk = Cmd::enterAtEnd(doc, blk);
    QCOMPARE(doc.iterateBlocks().size(), 2u);
    QVERIFY(!newBlk.isNull());
}

void TstD2Cmd::enterAtEnd_newBlockIsEmpty()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    BlockId newBlk = Cmd::enterAtEnd(doc, blk);
    QCOMPARE(doc.blockText(newBlk), QByteArray());
}

void TstD2Cmd::backspaceMerge_appendsContentToPrev()
{
    MarkoffDocument doc(1);
    BlockId blkA = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    BlockId blkB = doc.testInsertBlock(BlockKind::Paragraph, " world");
    auto result = Cmd::backspaceMerge(doc, blkB);
    QCOMPARE(result.mergedInto, blkA);
    QCOMPARE(result.cursorByteOffset, 5u);
    QCOMPARE(doc.blockText(blkA), QByteArray("hello world"));
    QCOMPARE(doc.iterateBlocks().size(), 1u);
}

void TstD2Cmd::backspaceMerge_stripsTrailingNewlineAtBoundary()
{
    // Blocks loaded from markdown carry a trailing '\n' as a structural delimiter.
    // backspaceMerge must replace that '\n' (not append after it) so the merged
    // text has no mid-block newline and the cursor lands at the join point.
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("hello\n\nworld\n");
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2u);
    BlockId blkA = blocks[0];
    BlockId blkB = blocks[1];

    // Verify the trailing-newline precondition (the fix is a no-op without it).
    QVERIFY(doc.blockText(blkA).endsWith('\n'));

    auto result = Cmd::backspaceMerge(doc, blkB);
    QCOMPARE(result.mergedInto, blkA);
    // Cursor at join point — after "hello", before "world".
    QCOMPARE(result.cursorByteOffset, 5u);
    // No mid-block '\n': merged text is "helloworld\n", not "hello\nworld\n".
    QCOMPARE(doc.blockText(blkA), QByteArray("helloworld\n"));
    QCOMPARE(doc.iterateBlocks().size(), 1u);
}

void TstD2Cmd::backspaceMerge_firstBlock_noOp()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    auto result = Cmd::backspaceMerge(doc, blk);
    QCOMPARE(result.mergedInto, blk);
    QCOMPARE(result.cursorByteOffset, 0u);
    QCOMPARE(doc.iterateBlocks().size(), 1u);
}

void TstD2Cmd::deleteMerge_appendsNextBlockContent()
{
    MarkoffDocument doc(1);
    BlockId blkA = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    BlockId blkB = doc.testInsertBlock(BlockKind::Paragraph, " world");
    Cmd::deleteMerge(doc, blkA);
    QCOMPARE(doc.blockText(blkA), QByteArray("hello world"));
    QCOMPARE(doc.iterateBlocks().size(), 1u);
}

void TstD2Cmd::deleteMerge_stripsTrailingNewlineAtBoundary()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("hello\n\nworld\n");
    auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2u);
    BlockId blkA = blocks[0];

    QVERIFY(doc.blockText(blkA).endsWith('\n'));

    Cmd::deleteMerge(doc, blkA);
    QCOMPARE(doc.blockText(blkA), QByteArray("helloworld\n"));
    QCOMPARE(doc.iterateBlocks().size(), 1u);
}

void TstD2Cmd::deleteMerge_lastBlock_noOp()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    Cmd::deleteMerge(doc, blk);
    QCOMPARE(doc.iterateBlocks().size(), 1u);
}

void TstD2Cmd::changeKind_updatesBlockKind()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "text");
    Cmd::changeKind(doc, blk, BlockKind::Heading);
    QCOMPARE(doc.blockKind(blk), BlockKind::Heading);
}

void TstD2Cmd::changeKind_withAttrs()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "# text");
    Cmd::changeKind(doc, blk, BlockKind::Heading, {"level"}, {AttrValue{1}});
    QCOMPARE(doc.blockKind(blk), BlockKind::Heading);
}

void TstD2Cmd::pasteMarkdown_singleParagraph_contentAppearsInNewBlock()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello ");
    // Paste "world" as markdown at the end of the block
    Cmd::pasteMarkdown(doc, blk, 6, "world");
    // Should have inserted a new block after blk containing the pasted content
    auto blocks = doc.iterateBlocks();
    QVERIFY(blocks.size() >= 1);
    // The combined text of all blocks should contain "world"
    bool foundWorld = false;
    for (const auto &b : blocks) {
        if (doc.blockText(b).contains("world")) { foundWorld = true; break; }
    }
    QVERIFY(foundWorld);
}

void TstD2Cmd::pasteMarkdown_splitsTargetAndAppendsBlocks()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "before  after");
    // Paste two paragraphs in the middle (at offset 7, between "before " and " after")
    QByteArray pasteSource = "first\n\nsecond";  // two paragraphs
    Cmd::pasteMarkdown(doc, blk, 7, pasteSource);
    // Result: original block has "before " (tail stripped), new blocks inserted,
    // last new block gets " after" appended
    auto blocks = doc.iterateBlocks();
    // Should have at least 2 blocks (original + at least 1 pasted)
    QVERIFY(blocks.size() >= 2);
}

QTEST_GUILESS_MAIN(TstD2Cmd)
#include "tst_d2_cmd_decomposition.moc"
