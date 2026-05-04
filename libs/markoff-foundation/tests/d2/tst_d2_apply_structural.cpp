// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/StructuralOp.h>
#include <markoff-foundation/BlockKind.h>

using namespace Markoff;

class TstD2ApplyStructural : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void insertEntry_appendsBlock();
    void removeEntry_dropsBlockFromIteration();
    void changeKind_updatesKindTagMap();
};

void TstD2ApplyStructural::insertEntry_appendsBlock()
{
    MarkoffDocument doc(1);
    StructuralOp op;
    op.payload = StructuralOp::InsertEntry{BlockId{}, BlockKind::Paragraph};
    doc.applyStructural(op);
    QCOMPARE(doc.iterateBlocks().size(), size_t(1));
}

void TstD2ApplyStructural::removeEntry_dropsBlockFromIteration()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    QCOMPARE(doc.iterateBlocks().size(), size_t(1));
    StructuralOp op;
    op.payload = StructuralOp::RemoveEntry{blk};
    doc.applyStructural(op);
    QCOMPARE(doc.iterateBlocks().size(), size_t(0));
}

void TstD2ApplyStructural::changeKind_updatesKindTagMap()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "# hello");
    QCOMPARE(doc.blockKind(blk), BlockKind::Paragraph);
    StructuralOp op;
    op.payload = StructuralOp::ChangeKind{blk, BlockKind::Heading};
    doc.applyStructural(op);
    QCOMPARE(doc.blockKind(blk), BlockKind::Heading);
}

QTEST_GUILESS_MAIN(TstD2ApplyStructural)
#include "tst_d2_apply_structural.moc"
