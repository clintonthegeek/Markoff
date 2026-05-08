// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/AttrNames.h>
using namespace Markoff;

namespace {
void route(MarkoffDocument *from, MarkoffDocument *to) {
    QObject::connect(from, &MarkoffDocument::localOpsProduced,
                     to, [to](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
        to->applyRemoteOps(std::move(ops), std::move(meta));
    });
}
}

class TstD5SiblingMapLww : public QObject {
    Q_OBJECT
private slots:
    void concurrentKindChange_converges() {
        // Two docs wired together — concurrent kind changes must converge.
        // NOTE: we can't use loadFromMarkdown on both independently (divergent
        // CRDT histories). Use D2 ops to build shared state.
        MarkoffDocument a(1), b(2);
        // Build initial state via a, then route to b.
        QObject::connect(&a, &MarkoffDocument::localOpsProduced,
                         &b, [&b](QList<MarkoffOp> ops, MarkoffBundleMeta meta) {
            b.applyRemoteOps(std::move(ops), std::move(meta));
        });
        // Insert a block into a (b receives it)
        {
            UndoLog::Transaction t(a.d2UndoLog());
            a.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
        }
        // Now wire bidirectional (after initial state is shared)
        route(&a, &b);
        route(&b, &a);

        const auto aBlocks = a.iterateBlocks();
        const auto bBlocks = b.iterateBlocks();
        QVERIFY(!aBlocks.empty());
        QCOMPARE(aBlocks.size(), bBlocks.size());

        // Concurrent kind changes on the same block — one is replica 1 (lower),
        // the other is replica 2 (higher counter wins if equal timestamps, or
        // higher counter wins on counter tie by replicaId).
        Cmd::changeKind(a, aBlocks.front(), BlockKind::Heading,
                        { AttrNames::Level }, { AttrValue{1} });
        Cmd::changeKind(b, bBlocks.front(), BlockKind::CodeBlock, {}, {});

        // Both should have converged to the same kind.
        QCOMPARE(a.blockKind(aBlocks.front()), b.blockKind(bBlocks.front()));
    }
};
QTEST_MAIN(TstD5SiblingMapLww)
#include "tst_d5_sibling_map_lww.moc"
