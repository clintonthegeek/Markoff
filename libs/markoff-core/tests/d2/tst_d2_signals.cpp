// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockEdit.h>
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/CrdtProxies.h>
#include <markoff-foundation/StructuralOp.h>
#include <markoff-foundation/UndoLog.h>

using namespace Markoff;

class TstD2Signals : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void d2DocumentChanged_firesOnApplyBlockEdit();
    void d2DocumentChanged_firesOnApplyStructural();
    void blockEditSequence_incrementsOnEdit();
    void d2EditSequence_sumsAcrossBlocks();
    void bufferProxy_firesOnApplyBlockEdit();
    void idListProxy_firesOnApplyStructural();
    void kindTagMapProxy_firesOnChangeKind();
    void d2EditSequence_incrementsOnStructural();
    void idListProxy_firesOnD2InsertBlock();
    void idListProxy_firesOnD2RemoveBlock();
};

void TstD2Signals::d2DocumentChanged_firesOnApplyBlockEdit()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    QSignalSpy spy(&doc, &MarkoffDocument::d2DocumentChanged);
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    // Signal is debounced via QTimer::singleShot(0); process the event loop once.
    QVERIFY(spy.wait(100));
    QCOMPARE(spy.count(), 1);
}

void TstD2Signals::d2DocumentChanged_firesOnApplyStructural()
{
    MarkoffDocument doc(1);
    QSignalSpy spy(&doc, &MarkoffDocument::d2DocumentChanged);
    StructuralOp op;
    op.payload = StructuralOp::InsertEntry{BlockId{}, BlockKind::Paragraph};
    doc.applyStructural(op);
    QVERIFY(spy.wait(100));
    QCOMPARE(spy.count(), 1);
}

void TstD2Signals::blockEditSequence_incrementsOnEdit()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    quint64 seq0 = doc.blockEditSequence(blk);
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    QVERIFY(doc.blockEditSequence(blk) > seq0);
}

void TstD2Signals::d2EditSequence_sumsAcrossBlocks()
{
    MarkoffDocument doc(1);
    BlockId blkA = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    BlockId blkB = doc.testInsertBlock(BlockKind::Paragraph, "world");
    quint64 seq0 = doc.d2EditSequence();
    doc.applyBlockEdit(BlockEdit{blkA, 5, 0, "!"});
    doc.applyBlockEdit(BlockEdit{blkB, 5, 0, "?"});
    QVERIFY(doc.d2EditSequence() > seq0 + 1);
}

void TstD2Signals::bufferProxy_firesOnApplyBlockEdit()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    auto *proxy = doc.bufferProxy(blk);
    QVERIFY(proxy != nullptr);
    QSignalSpy spy(proxy, &BufferProxy::inlineSpansChanged);
    doc.applyBlockEdit(BlockEdit{blk, 5, 0, "!"});
    QCOMPARE(spy.count(), 1);
}

void TstD2Signals::idListProxy_firesOnApplyStructural()
{
    MarkoffDocument doc(1);
    QSignalSpy spy(doc.idListProxy(), &IdListProxy::structureChanged);
    StructuralOp op;
    op.payload = StructuralOp::InsertEntry{BlockId{}, BlockKind::Paragraph};
    doc.applyStructural(op);
    QCOMPARE(spy.count(), 1);
}

void TstD2Signals::kindTagMapProxy_firesOnChangeKind()
{
    MarkoffDocument doc(1);
    BlockId blk = doc.testInsertBlock(BlockKind::Paragraph, "# hello");
    QSignalSpy spy(doc.kindTagMapProxy(), &SiblingMapProxy::mapChanged);
    StructuralOp op;
    op.payload = StructuralOp::ChangeKind{blk, BlockKind::Heading};
    doc.applyStructural(op);
    QCOMPARE(spy.count(), 1);
}

void TstD2Signals::d2EditSequence_incrementsOnStructural()
{
    MarkoffDocument doc(1);
    quint64 seq0 = doc.d2EditSequence();
    StructuralOp op;
    op.payload = StructuralOp::InsertEntry{BlockId{}, BlockKind::Paragraph};
    doc.applyStructural(op);
    QVERIFY(doc.d2EditSequence() > seq0);
}

void TstD2Signals::idListProxy_firesOnD2InsertBlock()
{
    MarkoffDocument doc(1);
    BlockId existing = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    QSignalSpy idSpy(doc.idListProxy(), &IdListProxy::structureChanged);
    QSignalSpy kindSpy(doc.kindTagMapProxy(), &SiblingMapProxy::mapChanged);

    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2InsertBlock(existing, BlockKind::Paragraph, t);

    QCOMPARE(idSpy.count(), 1);
    QCOMPARE(kindSpy.count(), 1);
}

void TstD2Signals::idListProxy_firesOnD2RemoveBlock()
{
    MarkoffDocument doc(1);
    BlockId existing = doc.testInsertBlock(BlockKind::Paragraph, "hello");
    QSignalSpy idSpy(doc.idListProxy(), &IdListProxy::structureChanged);
    QSignalSpy kindSpy(doc.kindTagMapProxy(), &SiblingMapProxy::mapChanged);

    UndoLog::Transaction t(doc.d2UndoLog());
    doc.d2RemoveBlock(existing, t);

    QCOMPARE(idSpy.count(), 1);
    QCOMPARE(kindSpy.count(), 1);
}

QTEST_MAIN(TstD2Signals)  // needs event loop for QTimer::singleShot
#include "tst_d2_signals.moc"
