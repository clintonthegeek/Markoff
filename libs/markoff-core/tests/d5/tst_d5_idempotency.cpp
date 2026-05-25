// SPDX-License-Identifier: GPL-3.0-or-later
//
// D5 idempotency test: re-delivering the same bundle must not double-apply.
//
// The CRDT's observed-timestamp check makes re-delivery a no-op. This test
// verifies that behaviour at the MarkoffDocument layer.
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

class TstD5Idempotency : public QObject {
    Q_OBJECT
private slots:
    void redeliveredBundle_doesNotDoubleApply() {
        MarkoffDocument a(1), b(2);

        // A→B routing so B gets A's initial content.
        QObject::connect(&a, &MarkoffDocument::localOpsProduced,
                         &b, [&b](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            b.applyRemoteOps(std::move(ops), std::move(meta));
        });

        // A creates initial "Hi\n" block; B receives it.
        BlockId blk;
        {
            UndoLog::Transaction t(a.d2UndoLog());
            blk = a.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(blk, 0, 0, QByteArrayLiteral("Hi\n"), t);
        }
        QCOMPARE(a.blockText(a.iterateBlocks().front()), QByteArray("Hi\n"));
        QCOMPARE(b.blockText(b.iterateBlocks().front()), QByteArray("Hi\n"));

        // Capture A's edit op (insert "!" at offset 2) without routing it yet.
        QList<MarkoffOp> capturedOps;
        MarkoffBundleMeta capturedMeta;
        // Disconnect the A→B route so we capture without auto-applying.
        disconnect(&a, &MarkoffDocument::localOpsProduced, nullptr, nullptr);
        QObject::connect(&a, &MarkoffDocument::localOpsProduced,
                         &a, [&](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            capturedOps = ops;
            capturedMeta = meta;
        });

        {
            UndoLog::Transaction t(a.d2UndoLog());
            a.d2ApplyBufferEdit(blk, 2, 0, QByteArrayLiteral("!"), t);
        }
        QVERIFY(!capturedOps.isEmpty());

        // First application: B should gain "!".
        b.applyRemoteOps(capturedOps, capturedMeta);
        const QByteArray firstApply = b.blockText(b.iterateBlocks().front());
        QCOMPARE(firstApply, QByteArray("Hi!\n"));

        // Re-delivery: must be idempotent — text must not change.
        b.applyRemoteOps(capturedOps, capturedMeta);
        const QByteArray secondApply = b.blockText(b.iterateBlocks().front());
        QCOMPARE(firstApply, secondApply);
    }

    void redeliveredIdListOp_doesNotDuplicateBlock() {
        MarkoffDocument a(1), b(2);

        QList<MarkoffOp> capturedOps;
        MarkoffBundleMeta capturedMeta;
        QObject::connect(&a, &MarkoffDocument::localOpsProduced,
                         &a, [&](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            capturedOps = ops;
            capturedMeta = meta;
        });

        // A creates a block (IdList + Buffer op).
        {
            UndoLog::Transaction t(a.d2UndoLog());
            BlockId newBlk = a.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(newBlk, 0, 0, QByteArrayLiteral("Hello\n"), t);
        }
        QVERIFY(!capturedOps.isEmpty());
        QCOMPARE(a.iterateBlocks().size(), size_t(1));

        // First delivery to B.
        b.applyRemoteOps(capturedOps, capturedMeta);
        QCOMPARE(b.iterateBlocks().size(), size_t(1));

        // Re-delivery must not duplicate the block.
        b.applyRemoteOps(capturedOps, capturedMeta);
        QCOMPARE(b.iterateBlocks().size(), size_t(1));
    }
};
QTEST_MAIN(TstD5Idempotency)
#include "tst_d5_idempotency.moc"
