// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/BlockAnchor.h>

using namespace Markoff;

class TstD2Undo : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undoUndoesLastEdit();
    void redoRestoresEdit();
    void undoForBlock_picksRecentBlockEntry();
    void canUndoForBlock_returns_false_when_no_history();
    void canUndoForBlock_returns_true_after_edit();
    void undoForBlock_via_anchor_undoes_edit();
    void undo_bumpsBlockEditSequence();
    void redo_bumpsBlockEditSequence();
};

void TstD2Undo::undoUndoesLastEdit()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    QCOMPARE(doc.blockText(blk), QByteArray("hello!"));
    doc.undoD2();
    QCOMPARE(doc.blockText(blk), QByteArray("hello"));
}

void TstD2Undo::redoRestoresEdit()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    doc.undoD2();
    doc.redoD2();
    QCOMPARE(doc.blockText(blk), QByteArray("hello!"));
}

void TstD2Undo::undoForBlock_picksRecentBlockEntry()
{
    MarkoffDocument doc(1);
    BlockId blkA = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    BlockId blkB = doc.testInsertBlock(BlockKind::Paragraph, "world");
    doc.applyBlockEdit(BlockEdit{blkA, 5, 0, "!"});
    doc.applyBlockEdit(BlockEdit{blkB, 5, 0, "?"});
    // Undo only blkA's last edit
    doc.undoForBlock(blkA);
    QCOMPARE(doc.blockText(blkA), QByteArray("hello"));
    QCOMPARE(doc.blockText(blkB), QByteArray("world?"));  // blkB unaffected
}

void TstD2Undo::canUndoForBlock_returns_false_when_no_history()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    BlockAnchor anchor = blk;
    QVERIFY(!doc.canUndoForBlock(anchor));
}

void TstD2Undo::canUndoForBlock_returns_true_after_edit()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    BlockAnchor anchor = blk;
    QVERIFY(doc.canUndoForBlock(anchor));
}

void TstD2Undo::undoForBlock_via_anchor_undoes_edit()
{
    MarkoffDocument doc(1);
    BlockId blkA = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    BlockId blkB = doc.testInsertBlock(BlockKind::Paragraph, "world");
    doc.applyBlockEdit(BlockEdit{blkA, 5, 0, "!"});
    doc.applyBlockEdit(BlockEdit{blkB, 5, 0, "?"});
    BlockAnchor anchorA = blkA;
    doc.undoForBlock(anchorA);
    QCOMPARE(doc.blockText(blkA), QByteArray("hello"));
    QCOMPARE(doc.blockText(blkB), QByteArray("world?"));
}

void TstD2Undo::undo_bumpsBlockEditSequence()
{
    // The undo dispatcher mutates the CRDT Buffer directly (BufferT branch),
    // bypassing applyBlockEdit/d2ApplyBufferEdit entirely — those are the
    // only other places blockEditSequence normally advances. Without this,
    // any consumer keyed on (BlockId, blockEditSequence) — namely
    // InlineParseCache — would see no change and keep serving spans parsed
    // from the pre-undo text (Corbomite Cluster K: phantom link styling
    // survived an undo until an unrelated edit finally bumped the sequence).
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    const quint64 seqBeforeUndo = doc.blockEditSequence(blk);
    doc.undoD2();
    QVERIFY(doc.blockEditSequence(blk) > seqBeforeUndo);
}

void TstD2Undo::redo_bumpsBlockEditSequence()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    doc.undoD2();
    const quint64 seqBeforeRedo = doc.blockEditSequence(blk);
    doc.redoD2();
    QVERIFY(doc.blockEditSequence(blk) > seqBeforeRedo);
}

QTEST_GUILESS_MAIN(TstD2Undo)
#include "tst_d2_undo.moc"
