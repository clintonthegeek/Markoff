// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/BlockKind.h>

using namespace Markoff::View::Qml;

namespace {

BlockRecord para(const QString &t) {
    BlockRecord r;
    r.kind = BlockKind::Paragraph;
    r.source = t;
    r.text = t;
    return r;
}
BlockRecord heading(int level, const QString &t) {
    BlockRecord r;
    r.kind = BlockKind::Heading;
    r.headingLevel = level;
    r.text = t;
    r.source = QString(level, QChar('#')) + QChar(' ') + t;
    return r;
}

}  // namespace

class TstLiveBlockModel : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void empty_initially() {
        LiveBlockModel m;
        QCOMPARE(m.rowCount(), 0);
    }

    void setRecords_populates_rows() {
        LiveBlockModel m;
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        m.setRecords({ para("a"), para("b"), heading(1, "T") });
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(reset.count(), 1);
    }

    void roleNames_includes_kind_and_text() {
        LiveBlockModel m;
        const auto names = m.roleNames();
        const QList<QByteArray> values = names.values();
        QVERIFY(values.contains("kind"));
        QVERIFY(values.contains("text"));
        QVERIFY(values.contains("headingLevel"));
        QVERIFY(values.contains("imageSrc"));
        QVERIFY(values.contains("imageAlt"));
        QVERIFY(values.contains("imageTitle"));
        QVERIFY(values.contains("codeLanguage"));
        QVERIFY(values.contains("codeText"));
    }

    void data_returns_kind_and_text() {
        LiveBlockModel m;
        m.setRecords({ heading(2, "Hello") });
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("kind")).toString(), BlockKind::Heading);
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("text")).toString(), QStringLiteral("Hello"));
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("headingLevel")).toInt(), 2);
    }

    void applyOps_pure_inserts_emits_correct_signals() {
        LiveBlockModel m;
        QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
        m.setRecords({});  // start empty
        m.applyOps(
            { { AstBlockDiff::OpKind::Insert, -1, 0 },
              { AstBlockDiff::OpKind::Insert, -1, 1 } },
            { para("a"), para("b") });
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(ins.count(), 2);
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("text")).toString(), QStringLiteral("a"));
        QCOMPARE(m.data(m.index(1, 0), m.roleForName("text")).toString(), QStringLiteral("b"));
    }

    void applyOps_pure_deletes_emits_correct_signals() {
        LiveBlockModel m;
        m.setRecords({ para("a"), para("b") });
        QSignalSpy rem(&m, &QAbstractItemModel::rowsRemoved);
        m.applyOps(
            { { AstBlockDiff::OpKind::Delete, 0, -1 },
              { AstBlockDiff::OpKind::Delete, 1, -1 } },
            {});
        QCOMPARE(m.rowCount(), 0);
        QCOMPARE(rem.count(), 2);
    }

    void applyOps_equal_ops_keep_rows_unchanged_no_signals() {
        LiveBlockModel m;
        m.setRecords({ para("a"), para("b") });
        QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
        QSignalSpy rem(&m, &QAbstractItemModel::rowsRemoved);
        QSignalSpy chg(&m, &QAbstractItemModel::dataChanged);
        m.applyOps(
            { { AstBlockDiff::OpKind::Equal, 0, 0 },
              { AstBlockDiff::OpKind::Equal, 1, 1 } },
            { para("a"), para("b") });
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(ins.count(), 0);
        QCOMPARE(rem.count(), 0);
        QCOMPARE(chg.count(), 0);
    }

    void applyOps_replace_in_middle_emits_minimal_diff() {
        LiveBlockModel m;
        m.setRecords({ para("a"), para("b"), para("c") });
        QSignalSpy ins(&m, &QAbstractItemModel::rowsInserted);
        QSignalSpy rem(&m, &QAbstractItemModel::rowsRemoved);
        m.applyOps(
            { { AstBlockDiff::OpKind::Equal,  0, 0 },
              { AstBlockDiff::OpKind::Delete, 1, -1 },
              { AstBlockDiff::OpKind::Insert, -1, 1 },
              { AstBlockDiff::OpKind::Equal,  2, 2 } },
            { para("a"), para("B!"), para("c") });
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(rem.count(), 1);
        QCOMPARE(ins.count(), 1);
        QCOMPARE(m.data(m.index(1, 0), m.roleForName("text")).toString(), QStringLiteral("B!"));
    }

    void image_record_carries_role_fields() {
        LiveBlockModel m;
        BlockRecord img;
        img.kind = BlockKind::Image;
        img.imageSrc = "u.png";
        img.imageAlt = "alt";
        img.source = "![alt](u.png)";
        m.setRecords({ img });
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("imageSrc")).toString(), QStringLiteral("u.png"));
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("imageAlt")).toString(), QStringLiteral("alt"));
    }

    void codeblock_record_carries_role_fields() {
        LiveBlockModel m;
        BlockRecord cb;
        cb.kind = BlockKind::CodeBlock;
        cb.codeLanguage = "rust";
        cb.codeText = "fn main(){}";
        cb.source = "```rust\nfn main(){}\n```";
        m.setRecords({ cb });
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("codeLanguage")).toString(), QStringLiteral("rust"));
        QCOMPARE(m.data(m.index(0, 0), m.roleForName("codeText")).toString(), QStringLiteral("fn main(){}"));
    }
};

QTEST_APPLESS_MAIN(TstLiveBlockModel)
#include "tst_view_qml_live_block_model.moc"
