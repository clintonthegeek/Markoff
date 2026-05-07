// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

class TstD2Gc : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void onSaveSucceeded_advancesWatermark();
    void onSaveSucceeded_refusesIfTransactionOpen();
    void compact_dispatchedToAllCrdts();
    void undoLog_dropsEntriesWhoseOpsAreCollapsed();
    void orphanedBuffer_disposedAfterDeleteOpCompacted();
    void orphanedBuffer_keptIfDeleteOpStillUndoable();
};

// ── onSaveSucceeded_advancesWatermark ────────────────────────────────────────

void TstD2Gc::onSaveSucceeded_advancesWatermark()
{
    MarkoffDocument doc(1);
    doc.testInsertBlock(BlockKind::Paragraph, "hello");
    // triggerGc wraps onSaveSucceeded; no open transaction, so returns true.
    bool result = doc.triggerGc();
    QVERIFY(result);
}

// ── onSaveSucceeded_refusesIfTransactionOpen ─────────────────────────────────

void TstD2Gc::onSaveSucceeded_refusesIfTransactionOpen()
{
    MarkoffDocument doc(1);
    doc.testInsertBlock(BlockKind::Paragraph, "hello");
    // Hold a transaction open — GC must refuse
    {
        UndoLog::Transaction t(doc.d2UndoLog());
        bool result = doc.triggerGc();
        QVERIFY(!result);
        // t destructs here, closing the transaction
    }
    // Now GC should succeed
    QVERIFY(doc.triggerGc());
}

// ── compact_dispatchedToAllCrdts ─────────────────────────────────────────────

void TstD2Gc::compact_dispatchedToAllCrdts()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("# Heading\n\nParagraph\n");
    // Make some edits so there are undo entries
    auto blocks = doc.iterateBlocks();
    QVERIFY(!blocks.empty());
    doc.applyBlockEdit(BlockEdit{blocks[0], 0, 0, "x"});
    QVERIFY(doc.d2UndoLog().entryCount() > 0);
    // GC compacts everything and drains UndoLog
    QVERIFY(doc.triggerGc());
    QCOMPARE(doc.d2UndoLog().entryCount(), size_t(0));
}

// ── undoLog_dropsEntriesWhoseOpsAreCollapsed ─────────────────────────────────

void TstD2Gc::undoLog_dropsEntriesWhoseOpsAreCollapsed()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    doc.applyBlockEdit(BlockEdit{blk, 6, 0, " world"});
    QVERIFY(doc.d2UndoLog().entryCount() >= 2);
    QVERIFY(doc.triggerGc());
    QCOMPARE(doc.d2UndoLog().entryCount(), size_t(0));
}

// ── orphanedBuffer_disposedAfterDeleteOpCompacted ────────────────────────────
// After GC runs, removed blocks whose buffers are not referenced in the undo
// log are disposed. GC first compacts (clears) the undo log, then disposeOrphans
// removes buffers for non-live, non-referenced blocks. Because compact clears all
// undo entries, the removed block's buffer becomes eligible and is disposed.

void TstD2Gc::orphanedBuffer_disposedAfterDeleteOpCompacted()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Paragraph one\n\nParagraph two\n");
    auto blocksBefore = doc.iterateBlocks();
    QVERIFY(blocksBefore.size() >= 2);

    // Remove the second block via d2RemoveBlock
    BlockId removed = blocksBefore[1];
    {
        UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2RemoveBlock(removed, t);
    }

    // Block no longer live
    auto liveAfterRemove = doc.iterateBlocks();
    bool stillLive = false;
    for (auto id : liveAfterRemove)
        if (id == removed) stillLive = true;
    QVERIFY(!stillLive);

    // Undo log has entries for the remove op
    QVERIFY(doc.d2UndoLog().entryCount() > 0);

    // GC compacts the undo log and disposes the orphaned buffer
    QVERIFY(doc.triggerGc());

    // After GC: no undo entries remain
    QCOMPARE(doc.d2UndoLog().entryCount(), size_t(0));

    // Buffer disposed — blockText returns empty QByteArray for unknown block id
    QCOMPARE(doc.blockText(removed), QByteArray());
}

// ── orphanedBuffer_keptIfDeleteOpStillUndoable ───────────────────────────────
// Before GC runs, the buffer for a removed block is still in the blockBuffers
// map. The undo log's remove entry is still live, so undoD2() can restore the
// block. This test verifies the pre-GC state: buffer intact, undo works.

void TstD2Gc::orphanedBuffer_keptIfDeleteOpStillUndoable()
{
    MarkoffDocument doc(1);
    doc.loadFromMarkdown("Only paragraph\n");
    auto blocks = doc.iterateBlocks();
    QVERIFY(!blocks.empty());
    BlockId blk = blocks[0];

    // Record the original content
    QByteArray origText = doc.blockText(blk);
    QVERIFY(!origText.isEmpty());

    {
        UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2RemoveBlock(blk, t);
    }

    // BEFORE GC: undo log still has entries; buffer is still in blockBuffers
    QVERIFY(doc.d2UndoLog().entryCount() > 0);

    // GC hasn't run — undo restores the block to the live IdList
    doc.undoD2();
    auto blocksAfterUndo = doc.iterateBlocks();
    bool found = false;
    for (auto id : blocksAfterUndo)
        if (id == blk) found = true;
    QVERIFY(found);

    // Buffer content is still accessible
    QCOMPARE(doc.blockText(blk), origText);
}

QTEST_GUILESS_MAIN(TstD2Gc)
#include "tst_d2_gc.moc"
