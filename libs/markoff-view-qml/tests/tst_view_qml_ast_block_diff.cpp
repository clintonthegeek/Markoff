// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "../src/AstBlockDiff.h"
#include <markoff/view/qml/BlockKind.h>
#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/TextAnchor.h>

using namespace Markoff::View::Qml;

namespace {

/// Construct a BlockAnchor with a unique integer ID (charValue).
/// replicaId=1, bias=0 (Left) for all test fixtures.
Markoff::BlockAnchor anchor(quint32 id) {
    return Markoff::BlockAnchor{ Markoff::TextAnchor{ 1, id, 0 } };
}

BlockKey k(const QString &kind, quint32 anchorId) {
    return BlockKey { kind, anchor(anchorId) };
}

QString opName(AstBlockDiff::OpKind k) {
    switch (k) {
        case AstBlockDiff::OpKind::Equal:  return QStringLiteral("Equal");
        case AstBlockDiff::OpKind::Insert: return QStringLiteral("Insert");
        case AstBlockDiff::OpKind::Delete: return QStringLiteral("Delete");
    }
    return QString();
}

QString opsToString(const QList<AstBlockDiff::Op> &ops) {
    QStringList parts;
    for (const auto &op : ops) {
        parts << QStringLiteral("%1(prev=%2,next=%3)").arg(opName(op.kind))
                                                      .arg(op.prevIndex)
                                                      .arg(op.nextIndex);
    }
    return parts.join(QStringLiteral(", "));
}

}  // namespace

class TstAstBlockDiff : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_to_empty_no_ops() {
        const auto ops = AstBlockDiff::diff({}, {});
        QCOMPARE(ops.size(), 0);
    }

    void empty_to_one_block_emits_one_insert() {
        QList<BlockKey> next { k(BlockKind::Paragraph, 1) };
        const auto ops = AstBlockDiff::diff({}, next);
        QCOMPARE(ops.size(), 1);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[0].nextIndex, 0);
    }

    void one_block_to_empty_emits_one_delete() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, 1) };
        const auto ops = AstBlockDiff::diff(prev, {});
        QCOMPARE(ops.size(), 1);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Delete);
        QCOMPARE(ops[0].prevIndex, 0);
    }

    void identity_three_blocks_three_equals() {
        QList<BlockKey> seq {
            k(BlockKind::Paragraph, 1),
            k(BlockKind::Paragraph, 2),
            k(BlockKind::Paragraph, 3)
        };
        const auto ops = AstBlockDiff::diff(seq, seq);
        QCOMPARE(ops.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(ops[i].kind, AstBlockDiff::OpKind::Equal);
            QCOMPARE(ops[i].prevIndex, i);
            QCOMPARE(ops[i].nextIndex, i);
        }
    }

    void single_insert_at_start() {
        // prev: blocks 2, 3  →  next: blocks 1(new), 2, 3
        QList<BlockKey> prev { k(BlockKind::Paragraph, 2),
                               k(BlockKind::Paragraph, 3) };
        QList<BlockKey> next { k(BlockKind::Paragraph, 1),
                               k(BlockKind::Paragraph, 2),
                               k(BlockKind::Paragraph, 3) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 3);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[0].nextIndex, 0);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[2].kind, AstBlockDiff::OpKind::Equal);
    }

    void single_insert_at_end() {
        // prev: block 1  →  next: blocks 1, 2(new)
        QList<BlockKey> prev { k(BlockKind::Paragraph, 1) };
        QList<BlockKey> next { k(BlockKind::Paragraph, 1),
                               k(BlockKind::Paragraph, 2) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[1].nextIndex, 1);
    }

    void single_insert_middle() {
        // prev: blocks 1, 3  →  next: blocks 1, 2(new), 3
        QList<BlockKey> prev { k(BlockKind::Paragraph, 1),
                               k(BlockKind::Paragraph, 3) };
        QList<BlockKey> next { k(BlockKind::Paragraph, 1),
                               k(BlockKind::Paragraph, 2),
                               k(BlockKind::Paragraph, 3) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 3);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[1].nextIndex, 1);
        QCOMPARE(ops[2].kind, AstBlockDiff::OpKind::Equal);
    }

    void single_delete_at_start() {
        // prev: blocks 1, 2  →  next: block 2 only
        QList<BlockKey> prev { k(BlockKind::Paragraph, 1),
                               k(BlockKind::Paragraph, 2) };
        QList<BlockKey> next { k(BlockKind::Paragraph, 2) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Delete);
        QCOMPARE(ops[0].prevIndex, 0);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Equal);
    }

    void single_delete_at_end() {
        // prev: blocks 1, 2  →  next: block 1 only
        QList<BlockKey> prev { k(BlockKind::Paragraph, 1),
                               k(BlockKind::Paragraph, 2) };
        QList<BlockKey> next { k(BlockKind::Paragraph, 1) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Delete);
        QCOMPARE(ops[1].prevIndex, 1);
    }

    void replace_kind_change_emits_delete_then_insert() {
        // Same anchor, different kind → Delete + Insert (anchor+kind pair differs).
        QList<BlockKey> prev { k(BlockKind::Paragraph, 1) };
        QList<BlockKey> next { k(BlockKind::Heading,   1) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        const bool hasDelete = ops[0].kind == AstBlockDiff::OpKind::Delete ||
                               ops[1].kind == AstBlockDiff::OpKind::Delete;
        const bool hasInsert = ops[0].kind == AstBlockDiff::OpKind::Insert ||
                               ops[1].kind == AstBlockDiff::OpKind::Insert;
        QVERIFY2(hasDelete && hasInsert, qPrintable(opsToString(ops)));
    }

    void large_identical_short_circuits_to_all_equals() {
        QList<BlockKey> seq;
        for (int i = 0; i < 50; ++i)
            seq.append(k(BlockKind::Paragraph, static_cast<quint32>(i + 1)));
        const auto ops = AstBlockDiff::diff(seq, seq);
        QCOMPARE(ops.size(), 50);
        for (const auto &op : ops) {
            QCOMPARE(op.kind, AstBlockDiff::OpKind::Equal);
        }
    }

    /// Content edit on a block (same anchor, same kind) must produce a single
    /// Equal op — not a Delete+Insert. This is the key property that anchor-
    /// based identity provides: a block whose text was modified in-place keeps
    /// its delegate alive rather than being torn down and recreated.
    void content_edit_preserves_block_identity() {
        // Block 2 has the same anchor (id=2) in prev and next; only the content
        // (stored in BlockRecord, not in BlockKey) changed. The diff must see
        // these as Equal.
        QList<BlockKey> prev { k(BlockKind::Paragraph, 1),
                               k(BlockKind::Paragraph, 2),
                               k(BlockKind::Paragraph, 3) };
        QList<BlockKey> next { k(BlockKind::Paragraph, 1),
                               k(BlockKind::Paragraph, 2),   // same anchor → Equal
                               k(BlockKind::Paragraph, 3) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 3);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[2].kind, AstBlockDiff::OpKind::Equal);
    }

    /// A paragraph split produces a Delete for the old block and two Inserts
    /// for the resulting blocks (they carry fresh anchors unknown to prev).
    void paragraph_split_produces_delete_and_two_inserts() {
        // prev has one block (anchor 1); after split, next has two new blocks
        // (anchors 10 and 11). The split = Delete anchor-1 + Insert anchor-10 +
        // Insert anchor-11.
        QList<BlockKey> prev { k(BlockKind::Paragraph, 1) };
        QList<BlockKey> next { k(BlockKind::Paragraph, 10),
                               k(BlockKind::Paragraph, 11) };
        const auto ops = AstBlockDiff::diff(prev, next);
        // Exactly 3 ops: 1 Delete + 2 Inserts (order depends on LCS backtrack,
        // but none may be Equal since no anchor is shared).
        QCOMPARE(ops.size(), 3);
        for (const auto &op : ops) {
            QVERIFY2(op.kind == AstBlockDiff::OpKind::Delete ||
                     op.kind == AstBlockDiff::OpKind::Insert,
                     qPrintable(opsToString(ops)));
        }
    }

    /// Two blocks merged: prev has two blocks; next has one block with a new
    /// anchor. Both prev blocks are deleted; the merged block is inserted.
    void paragraph_merge_produces_two_deletes_and_one_insert() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, 1),
                               k(BlockKind::Paragraph, 2) };
        QList<BlockKey> next { k(BlockKind::Paragraph, 10) };  // merged, new anchor
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 3);
        for (const auto &op : ops) {
            QVERIFY2(op.kind == AstBlockDiff::OpKind::Delete ||
                     op.kind == AstBlockDiff::OpKind::Insert,
                     qPrintable(opsToString(ops)));
        }
    }
};

QTEST_APPLESS_MAIN(TstAstBlockDiff)
#include "tst_view_qml_ast_block_diff.moc"
