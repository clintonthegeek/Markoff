// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockEdit.h>
#include <markoff-foundation/BlockKind.h>

using namespace Markoff;

class TstD2Undo : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undoUndoesLastEdit();
    void redoRestoresEdit();
    void undoForBlock_picksRecentBlockEntry();
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

QTEST_GUILESS_MAIN(TstD2Undo)
#include "tst_d2_undo.moc"
