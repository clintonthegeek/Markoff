// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include "../src/AstBlockDiff.h"
#include <markoff/view/qml/BlockKind.h>

using namespace Markoff::View::Qml;

namespace {

BlockKey k(const QString &kind, const QString &source) {
    return BlockKey { kind, source };
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
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("hi")) };
        const auto ops = AstBlockDiff::diff({}, next);
        QCOMPARE(ops.size(), 1);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[0].nextIndex, 0);
    }

    void one_block_to_empty_emits_one_delete() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("hi")) };
        const auto ops = AstBlockDiff::diff(prev, {});
        QCOMPARE(ops.size(), 1);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Delete);
        QCOMPARE(ops[0].prevIndex, 0);
    }

    void identity_three_blocks_three_equals() {
        QList<BlockKey> seq {
            k(BlockKind::Paragraph, QStringLiteral("a")),
            k(BlockKind::Paragraph, QStringLiteral("b")),
            k(BlockKind::Paragraph, QStringLiteral("c"))
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
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("b")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 3);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[0].nextIndex, 0);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[2].kind, AstBlockDiff::OpKind::Equal);
    }

    void single_insert_at_end() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[1].nextIndex, 1);
    }

    void single_insert_middle() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 3);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Insert);
        QCOMPARE(ops[1].nextIndex, 1);
        QCOMPARE(ops[2].kind, AstBlockDiff::OpKind::Equal);
    }

    void single_delete_at_start() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("b")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Delete);
        QCOMPARE(ops[0].prevIndex, 0);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Equal);
    }

    void single_delete_at_end() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[1].kind, AstBlockDiff::OpKind::Delete);
        QCOMPARE(ops[1].prevIndex, 1);
    }

    void replace_kind_change_emits_delete_then_insert() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")) };
        QList<BlockKey> next { k(BlockKind::Heading,   QStringLiteral("a")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 2);
        const bool first_is_delete = ops[0].kind == AstBlockDiff::OpKind::Delete;
        const bool first_is_insert = ops[0].kind == AstBlockDiff::OpKind::Insert;
        QVERIFY2(first_is_delete || first_is_insert, qPrintable(opsToString(ops)));
    }

    void content_edit_in_middle_of_three_blocks() {
        QList<BlockKey> prev { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("b")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        QList<BlockKey> next { k(BlockKind::Paragraph, QStringLiteral("a")),
                               k(BlockKind::Paragraph, QStringLiteral("B!")),
                               k(BlockKind::Paragraph, QStringLiteral("c")) };
        const auto ops = AstBlockDiff::diff(prev, next);
        QCOMPARE(ops.size(), 4);
        QCOMPARE(ops[0].kind, AstBlockDiff::OpKind::Equal);
        QCOMPARE(ops[3].kind, AstBlockDiff::OpKind::Equal);
    }

    void large_identical_short_circuits_to_all_equals() {
        QList<BlockKey> seq;
        for (int i = 0; i < 50; ++i)
            seq.append(k(BlockKind::Paragraph, QString::number(i)));
        const auto ops = AstBlockDiff::diff(seq, seq);
        QCOMPARE(ops.size(), 50);
        for (const auto &op : ops) {
            QCOMPARE(op.kind, AstBlockDiff::OpKind::Equal);
        }
    }
};

QTEST_APPLESS_MAIN(TstAstBlockDiff)
#include "tst_view_qml_ast_block_diff.moc"
