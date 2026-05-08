// SPDX-License-Identifier: GPL-3.0-or-later
//
// D5 three-doc convergence test.
//
// Three MarkoffDocuments are wired in a mesh. Replica A authors the initial
// content and all three receive it. Then each replica appends one character
// concurrently and the mesh propagates all ops synchronously. All three must
// converge to the same block count and text.
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff;

namespace {
void wireMesh(QList<MarkoffDocument*> docs) {
    for (auto *from : docs) {
        for (auto *to : docs) {
            if (from == to) continue;
            QObject::connect(from, &MarkoffDocument::localOpsProduced,
                             to, [to](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
                to->applyRemoteOps(std::move(ops), std::move(meta));
            });
        }
    }
}
}

class TstD5ThreeDocConvergence : public QObject {
    Q_OBJECT
private slots:
    void threeWayEdits_converge() {
        MarkoffDocument a(1), b(2), c(3);

        // Wire full mesh before any content.
        wireMesh({ &a, &b, &c });

        // A creates the initial "X\n" block; B and C receive it.
        BlockId blk;
        {
            UndoLog::Transaction t(a.d2UndoLog());
            blk = a.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
            a.d2ApplyBufferEdit(blk, 0, 0, QByteArrayLiteral("X\n"), t);
        }

        // Verify all three have the initial block.
        QCOMPARE(a.iterateBlocks().size(), size_t(1));
        QCOMPARE(b.iterateBlocks().size(), size_t(1));
        QCOMPARE(c.iterateBlocks().size(), size_t(1));

        // Each replica inserts one char at the end (before '\n').
        // Since the mesh is synchronous, each op immediately propagates to the
        // other two replicas.  All three converge after each step.
        for (auto *doc : { &a, &b, &c }) {
            UndoLog::Transaction t(doc->d2UndoLog());
            const auto blocks = doc->iterateBlocks();
            const QByteArray cur = doc->blockText(blocks.front());
            const quint32 insertAt = cur.endsWith('\n')
                ? quint32(cur.size() - 1) : quint32(cur.size());
            doc->d2ApplyBufferEdit(blocks.front(), insertAt, 0,
                                   QByteArray(1, char('a' + int(doc->replicaId()))), t);
        }

        QCOMPARE(a.iterateBlocks().size(), b.iterateBlocks().size());
        QCOMPARE(b.iterateBlocks().size(), c.iterateBlocks().size());
        QCOMPARE(a.blockText(a.iterateBlocks().front()),
                 b.blockText(b.iterateBlocks().front()));
        QCOMPARE(b.blockText(b.iterateBlocks().front()),
                 c.blockText(c.iterateBlocks().front()));
    }
};
QTEST_MAIN(TstD5ThreeDocConvergence)
#include "tst_d5_three_doc_convergence.moc"
