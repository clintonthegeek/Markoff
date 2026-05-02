// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QAbstractItemModelTester>
#include <QSignalSpy>

#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/BlockKind.h>

using namespace Markoff::LiveRender;

// Helper: build a minimal BlockRecord for testing.
static BlockRecord makeRecord(const QString &kind, const QString &text,
                              int headingLevel = 0, const QString &codeLang = {})
{
    BlockRecord r;
    r.kind         = kind;
    r.text         = text;
    r.headingLevel = headingLevel;
    r.codeLanguage = codeLang;
    return r;
}

// Helper: derive BlockKey from a record.
static BlockKey keyOf(const BlockRecord &r)
{
    return BlockKey{ r.kind, r.blockAnchor };
}

class TstLiveRenderBlockModel : public QObject {
    Q_OBJECT
private Q_SLOTS:

    void initially_empty() {
        LiveBlockModel m;
        QCOMPARE(m.rowCount(), 0);
    }

    void model_passes_abstract_model_tester() {
        LiveBlockModel m;
        QAbstractItemModelTester tester(
            &m, QAbstractItemModelTester::FailureReportingMode::Fatal);
        const QList<BlockRecord> recs = {
            makeRecord(BlockKind::Paragraph, "hello"),
            makeRecord(BlockKind::Heading,   "# World", 1),
        };
        QList<BlockKey> next = { keyOf(recs[0]), keyOf(recs[1]) };
        m.applyOps(AstBlockDiff::diff({}, next), recs);
        QCOMPARE(m.rowCount(), 2);
    }

    void apply_ops_insert_two_rows() {
        LiveBlockModel m;
        const QList<BlockRecord> recs = {
            makeRecord(BlockKind::Paragraph, "para 1"),
            makeRecord(BlockKind::Heading,   "# h1", 1),
        };
        QList<BlockKey> next = { keyOf(recs[0]), keyOf(recs[1]) };
        QSignalSpy spy(&m, &QAbstractListModel::rowsInserted);

        m.applyOps(AstBlockDiff::diff({}, next), recs);

        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(m.data(m.index(0), LiveBlockModel::KindRole).toString(),
                 BlockKind::Paragraph);
        QCOMPARE(m.data(m.index(1), LiveBlockModel::KindRole).toString(),
                 BlockKind::Heading);
        QCOMPARE(m.data(m.index(1), LiveBlockModel::HeadingLevelRole).toInt(), 1);
    }

    void apply_ops_delete_first_row() {
        LiveBlockModel m;
        const QList<BlockRecord> initial = {
            makeRecord(BlockKind::Paragraph, "para 1"),
            makeRecord(BlockKind::Paragraph, "para 2"),
        };
        QList<BlockKey> initKeys = { keyOf(initial[0]), keyOf(initial[1]) };
        m.applyOps(AstBlockDiff::diff({}, initKeys), initial);
        QCOMPARE(m.rowCount(), 2);

        const QList<BlockRecord> next = { initial[1] };
        QList<BlockKey> nextKeys = { keyOf(initial[1]) };
        QSignalSpy spy(&m, &QAbstractListModel::rowsRemoved);

        m.applyOps(AstBlockDiff::diff(initKeys, nextKeys), next);

        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.data(m.index(0), LiveBlockModel::TextRole).toString(),
                 QStringLiteral("para 2"));
    }

    void apply_ops_equal_text_change_emits_data_changed() {
        LiveBlockModel m;
        BlockRecord rec = makeRecord(BlockKind::Paragraph, "original");
        QList<BlockKey> keys = { keyOf(rec) };
        m.applyOps(AstBlockDiff::diff({}, keys), { rec });

        rec.text = "updated";
        QSignalSpy spy(&m, &QAbstractListModel::dataChanged);
        m.applyOps(AstBlockDiff::diff(keys, { keyOf(rec) }), { rec });

        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.data(m.index(0), LiveBlockModel::TextRole).toString(),
                 QStringLiteral("updated"));
    }

    void apply_ops_identity_emits_no_signals() {
        LiveBlockModel m;
        BlockRecord rec = makeRecord(BlockKind::Paragraph, "text");
        QList<BlockKey> keys = { keyOf(rec) };
        m.applyOps(AstBlockDiff::diff({}, keys), { rec });

        QSignalSpy insertSpy(&m, &QAbstractListModel::rowsInserted);
        QSignalSpy removeSpy(&m, &QAbstractListModel::rowsRemoved);
        QSignalSpy changeSpy(&m, &QAbstractListModel::dataChanged);

        m.applyOps(AstBlockDiff::diff(keys, keys), { rec });

        QCOMPARE(insertSpy.count(), 0);
        QCOMPARE(removeSpy.count(), 0);
        QCOMPARE(changeSpy.count(), 0);
    }

    void role_names_exposed() {
        LiveBlockModel m;
        const auto names = m.roleNames();
        QVERIFY(names.values().contains("kind"));
        QVERIFY(names.values().contains("text"));
        QVERIFY(names.values().contains("headingLevel"));
        QVERIFY(names.values().contains("codeLanguage"));
    }

    void row_edit_sequence_defaults_to_zero() {
        LiveBlockModel m;
        BlockRecord rec = makeRecord(BlockKind::Paragraph, "hi");
        QList<BlockKey> keys = { keyOf(rec) };
        m.applyOps(AstBlockDiff::diff({}, keys), { rec });
        QCOMPARE(m.rowEditSequence(0), quint64(0));
    }

    void set_row_edit_sequence() {
        LiveBlockModel m;
        BlockRecord rec = makeRecord(BlockKind::Paragraph, "hi");
        QList<BlockKey> keys = { keyOf(rec) };
        m.applyOps(AstBlockDiff::diff({}, keys), { rec });
        m.setRowEditSequence(0, quint64(42));
        QCOMPARE(m.rowEditSequence(0), quint64(42));
    }

    void equal_op_with_stale_row_preserves_model_text() {
        // The R4 freshness rule: when a row's lastEditSequence is GREATER
        // than the incoming parse's parseInputEditSequence, the parse arrived
        // with stale input for that row. The text-role update must NOT be
        // applied — the CRDT is canonical for those bytes; the existing model
        // text reflects post-edit state and must be preserved.
        LiveBlockModel model;
        const auto firstRecs = QList<BlockRecord>{
            makeRecord(BlockKind::Paragraph, "hello"),
            makeRecord(BlockKind::Paragraph, "world"),
        };
        QList<BlockKey> firstKeys; for (const auto &r : firstRecs) firstKeys << keyOf(r);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), firstRecs);
        QCOMPARE(model.rowCount(), 2);

        // Simulate a local edit on row 0: stamp its sequence at 5.
        model.setRowEditSequence(0, 5);
        // Row 1 is untouched: stays at 0.

        // Now parse arrives with parseInputEditSeq=3 (i.e. captured BEFORE
        // the row-0 edit at seq 5). Records have NEW text on row 0 (the
        // pre-edit text) and matching text on row 1.
        const auto secondRecs = QList<BlockRecord>{
            makeRecord(BlockKind::Paragraph, "hello-PRE-EDIT"),  // stale
            makeRecord(BlockKind::Paragraph, "world"),            // fresh (no local edit)
        };
        QList<BlockKey> secondKeys; for (const auto &r : secondRecs) secondKeys << keyOf(r);
        // BlockKey only includes (kind, anchor); for this synthesised test
        // anchors are default-constructed and equal across both lists -> Equal ops.
        const auto ops = AstBlockDiff::diff(firstKeys, secondKeys);

        const quint64 parseInputEditSeq = 3;
        model.applyOps(ops, secondRecs, parseInputEditSeq);

        // Stale row: original text retained.
        QCOMPARE(model.recordAt(0).text, QString("hello"));
        // Fresh row: text updated as normal.
        QCOMPARE(model.recordAt(1).text, QString("world"));
    }

    void equal_op_with_stale_row_still_updates_non_text_fields() {
        LiveBlockModel model;
        const auto firstRecs = QList<BlockRecord>{
            makeRecord(BlockKind::Heading, "Title", /*headingLevel=*/2),
        };
        QList<BlockKey> firstKeys; firstKeys << keyOf(firstRecs[0]);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), firstRecs);

        model.setRowEditSequence(0, 10);

        // Same kind+anchor so the diff is Equal; level changes 2 -> 3.
        const auto secondRecs = QList<BlockRecord>{
            makeRecord(BlockKind::Heading, "STALE TEXT", /*headingLevel=*/3),
        };
        QList<BlockKey> secondKeys; secondKeys << keyOf(secondRecs[0]);
        model.applyOps(AstBlockDiff::diff(firstKeys, secondKeys), secondRecs,
                       /*parseInputEditSeq=*/5);  // stale (5 < 10)

        // Stale: text preserved.
        QCOMPARE(model.recordAt(0).text, QString("Title"));
        // Non-text: applied even when stale (block-shape is parser-authoritative).
        QCOMPARE(model.recordAt(0).headingLevel, 3);
    }

    void equal_op_with_default_parse_seq_treats_all_rows_fresh() {
        // Backwards compatibility: existing R2/R3 callsites use the 2-arg
        // overload; default treats every row as fresh.
        LiveBlockModel model;
        const auto firstRecs = QList<BlockRecord>{ makeRecord(BlockKind::Paragraph, "a") };
        QList<BlockKey> firstKeys; firstKeys << keyOf(firstRecs[0]);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), firstRecs);
        model.setRowEditSequence(0, 999);

        const auto secondRecs = QList<BlockRecord>{ makeRecord(BlockKind::Paragraph, "b") };
        QList<BlockKey> secondKeys; secondKeys << keyOf(secondRecs[0]);
        model.applyOps(AstBlockDiff::diff(firstKeys, secondKeys), secondRecs);
        // No third arg -> default UINT64_MAX -> 999 <= MAX -> fresh -> "b".
        QCOMPARE(model.recordAt(0).text, QString("b"));
    }
};

QTEST_APPLESS_MAIN(TstLiveRenderBlockModel)
#include "tst_live_render_block_model.moc"
