// SPDX-License-Identifier: GPL-3.0-or-later
//
// D5 convergence tests: two MarkoffDocuments syncing via localOpsProduced.
//
// NOTE: Two documents built via independent loadFromMarkdown() calls have
// divergent CRDT histories (each uses its own replica_id for seed insertions),
// so they cannot converge via op exchange alone. Instead, these tests build
// initial document state by routing A's D2 ops to B before any concurrent
// edits occur. This is the correct collab model: one replica authors the
// initial content; others receive it via the sync channel.
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/Cmd/D2.h>

using namespace Markoff;

namespace {

void wireRouter(MarkoffDocument *from, MarkoffDocument *to)
{
    QObject::connect(from, &MarkoffDocument::localOpsProduced,
                     to,   [to](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
        to->applyRemoteOps(std::move(ops), std::move(meta));
    });
}

// Assert that two documents have the same block count and text per block.
void assertConverged(MarkoffDocument *a, MarkoffDocument *b)
{
    const auto blocksA = a->iterateBlocks();
    const auto blocksB = b->iterateBlocks();
    QCOMPARE(blocksA.size(), blocksB.size());
    for (size_t i = 0; i < blocksA.size(); ++i) {
        QCOMPARE(a->blockText(blocksA[i]), b->blockText(blocksB[i]));
    }
}

} // namespace

class TstD5TwoDocConvergence : public QObject {
    Q_OBJECT
private slots:
    // Replica A creates a two-block document via collab ops. Replica B receives
    // those ops. Then each replica makes one additional edit in a different block.
    void identicalLoad_thenSerialEdits_converges() {
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));

        // Wire both directions before any content is created.
        wireRouter(&a, &b);
        wireRouter(&b, &a);

        // A creates block 0: "Hello\n"
        BlockId blk0;
        {
            UndoLog::Transaction t(a.d2UndoLog());
            blk0 = a.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(blk0, 0, 0, QByteArrayLiteral("Hello\n"), t);
        }
        // A creates block 1: "World\n"
        BlockId blk1;
        {
            UndoLog::Transaction t(a.d2UndoLog());
            blk1 = a.d2InsertBlock(blk0, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(blk1, 0, 0, QByteArrayLiteral("World\n"), t);
        }

        // Both docs should now have two blocks with the same content.
        assertConverged(&a, &b);

        // A edits block 0 (appends "!"), B edits block 1 (appends "?").
        {
            UndoLog::Transaction t(a.d2UndoLog());
            a.d2ApplyBufferEdit(blk0, 5, 0, QByteArrayLiteral("!"), t);
        }
        {
            UndoLog::Transaction t(b.d2UndoLog());
            const auto bBlocks = b.iterateBlocks();
            QVERIFY(bBlocks.size() >= 2);
            b.d2ApplyBufferEdit(bBlocks[1], 5, 0, QByteArrayLiteral("?"), t);
        }
        assertConverged(&a, &b);
    }

    // Interleaved edits from both replicas to the same block converge.
    void interleavedEdits_converges() {
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));
        wireRouter(&a, &b);
        wireRouter(&b, &a);

        // A creates initial "Line\n" block.
        BlockId blk;
        {
            UndoLog::Transaction t(a.d2UndoLog());
            blk = a.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(blk, 0, 0, QByteArrayLiteral("Line\n"), t);
        }
        assertConverged(&a, &b);

        // Interleave 10 single-char inserts, alternating A and B.
        for (int i = 0; i < 10; ++i) {
            const QByteArray ch = QByteArray(1, char('A' + i));
            MarkoffDocument *who = (i % 2 == 0) ? &a : &b;
            // Find the corresponding block in whichever doc we're editing.
            const auto blocks = who->iterateBlocks();
            QVERIFY(!blocks.empty());
            const QByteArray current = who->blockText(blocks.front());
            const quint32 insertAt = current.endsWith('\n')
                ? quint32(current.size() - 1) : quint32(current.size());
            UndoLog::Transaction t(who->d2UndoLog());
            who->d2ApplyBufferEdit(blocks.front(), insertAt, 0, ch, t);
        }
        assertConverged(&a, &b);
    }

    // A inserts a new block; B receives it via collab op.
    void structuralInsert_converges() {
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));
        wireRouter(&a, &b);
        wireRouter(&b, &a);

        // A creates initial "First\n" block.
        BlockId firstBlk;
        {
            UndoLog::Transaction t(a.d2UndoLog());
            firstBlk = a.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(firstBlk, 0, 0, QByteArrayLiteral("First\n"), t);
        }
        assertConverged(&a, &b);

        // A inserts "Second\n" block after firstBlk.
        {
            UndoLog::Transaction t(a.d2UndoLog());
            BlockId newBlk = a.d2InsertBlock(firstBlk, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(newBlk, 0, 0, QByteArrayLiteral("Second\n"), t);
        }
        assertConverged(&a, &b);
    }

    // A removes a block; B receives the removal.
    void structuralRemove_converges() {
        MarkoffDocument a(quint16(1));
        MarkoffDocument b(quint16(2));
        wireRouter(&a, &b);
        wireRouter(&b, &a);

        // A creates two blocks: "First\n" and "Second\n".
        BlockId blk0, blk1;
        {
            UndoLog::Transaction t(a.d2UndoLog());
            blk0 = a.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(blk0, 0, 0, QByteArrayLiteral("First\n"), t);
        }
        {
            UndoLog::Transaction t(a.d2UndoLog());
            blk1 = a.d2InsertBlock(blk0, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(blk1, 0, 0, QByteArrayLiteral("Second\n"), t);
        }
        QVERIFY(a.iterateBlocks().size() >= 2);
        assertConverged(&a, &b);

        // A removes the second block.
        {
            UndoLog::Transaction t(a.d2UndoLog());
            a.d2RemoveBlock(blk1, t);
        }
        assertConverged(&a, &b);
    }
};
QTEST_MAIN(TstD5TwoDocConvergence)
#include "tst_d5_two_doc_convergence.moc"
