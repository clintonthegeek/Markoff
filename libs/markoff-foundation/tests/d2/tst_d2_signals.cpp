// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockEdit.h>
#include <markoff-foundation/BlockKind.h>

using namespace Markoff;

class TstD2Signals : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void d2DocumentChanged_firesOnApplyBlockEdit();
    void blockEditSequence_incrementsOnEdit();
    void d2EditSequence_sumsAcrossBlocks();
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

QTEST_MAIN(TstD2Signals)  // needs event loop for QTimer::singleShot
#include "tst_d2_signals.moc"
