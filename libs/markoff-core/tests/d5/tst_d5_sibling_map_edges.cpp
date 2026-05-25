// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/AttrNames.h>
using namespace Markoff;

class TstD5SiblingMapEdges : public QObject {
    Q_OBJECT
private slots:
    void opForDeletedBlock_appliesAsOrphan() {
        // a deletes a block; b sets its kind concurrently.
        // After cross-delivery, both should have 1 block (the removed one is gone)
        // and should have converged.
        MarkoffDocument a(1), b(2);
        // Build shared initial state via a → b
        QList<MarkoffOp> aCollected;
        MarkoffBundleMeta aCollectedMeta;
        QObject::connect(&a, &MarkoffDocument::localOpsProduced,
                         &a, [&](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            aCollected = ops; aCollectedMeta = meta;
        });
        // Insert 2 blocks
        BlockId b0, b1;
        {
            UndoLog::Transaction t(a.d2UndoLog());
            b0 = a.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
        }
        b.applyRemoteOps(aCollected, aCollectedMeta);
        {
            UndoLog::Transaction t(a.d2UndoLog());
            b1 = a.d2InsertBlock(b0, BlockKind::Paragraph, t);
        }
        b.applyRemoteOps(aCollected, aCollectedMeta);

        // Disconnect before the concurrent ops
        QObject::disconnect(&a, nullptr, nullptr, nullptr);

        // Capture ops independently
        QList<MarkoffOp> aOps, bOps;
        MarkoffBundleMeta aMeta, bMeta;
        QObject::connect(&a, &MarkoffDocument::localOpsProduced,
                         &a, [&](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            aOps = ops; aMeta = meta;
        });
        QObject::connect(&b, &MarkoffDocument::localOpsProduced,
                         &b, [&](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            bOps = ops; bMeta = meta;
        });

        // a removes the second block
        {
            UndoLog::Transaction t(a.d2UndoLog());
            a.d2RemoveBlock(b1, t);
        }
        // b sets kind on the (now-to-be-deleted) second block
        Cmd::changeKind(b, b.iterateBlocks().back(), BlockKind::Heading,
                        { AttrNames::Level }, { AttrValue{2} });

        // Cross-deliver
        b.applyRemoteOps(aOps, aMeta);
        a.applyRemoteOps(bOps, bMeta);

        // Both converge: 1 block (b1 was removed)
        QCOMPARE(a.iterateBlocks().size(), size_t(1));
        QCOMPARE(b.iterateBlocks().size(), size_t(1));
        // No crash, no assertion failure — the orphan KindTagMap entry is harmless
    }
};
QTEST_MAIN(TstD5SiblingMapEdges)
#include "tst_d5_sibling_map_edges.moc"
