// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff/view/qml/EditorBackend.h>
#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveProjectionLayer.h>
#include <markoff/view/qml/ProjectionItem.h>

using namespace Markoff::View::Qml;

class TstLiveProjectionLayer : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // -----------------------------------------------------------------------
    // T5.1 Construction.
    // -----------------------------------------------------------------------
    void construction_default_state()
    {
        LiveProjectionLayer layer;
        QCOMPARE(layer.editorBackend(), nullptr);
        QCOMPARE(layer.blockModel(), nullptr);
        QCOMPARE(layer.blockHoleCount(), 0);
        QCOMPARE(layer.inlineHoleCount(), 0);
        QCOMPARE(layer.inlinePredictionCount(), 0);
        QCOMPARE(layer.blockKindPredictionCount(), 0);
        QVERIFY(layer.predictionsForRow(0).isEmpty());
        QCOMPARE(layer.blockKindPredictionFor(0), nullptr);
        QVERIFY(!layer.rowIsHole(0));
    }

    // -----------------------------------------------------------------------
    // T5.2 Add prediction → predictionsForRow returns it.
    // -----------------------------------------------------------------------
    void inline_prediction_round_trip()
    {
        LiveProjectionLayer layer;

        InlinePrediction p;
        p.row = 3;
        p.charStart = 0;
        p.charEnd = 4;
        QTextCharFormat fmt;
        fmt.setFontWeight(QFont::Bold);
        p.format = fmt;

        layer.createInlinePrediction(p);

        const auto out = layer.predictionsForRow(3);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().row, 3);
        QCOMPARE(out.first().charStart, 0);
        QCOMPARE(out.first().charEnd, 4);
        QCOMPARE(out.first().format.fontWeight(), int(QFont::Bold));

        // Other rows return empty.
        QVERIFY(layer.predictionsForRow(2).isEmpty());
        QVERIFY(layer.predictionsForRow(4).isEmpty());

        // Multiple predictions on the same row accumulate.
        InlinePrediction q = p;
        q.charStart = 6;
        q.charEnd = 10;
        layer.createInlinePrediction(q);
        QCOMPARE(layer.predictionsForRow(3).size(), 2);
        QCOMPARE(layer.inlinePredictionCount(), 2);
    }

    // -----------------------------------------------------------------------
    // T5.3 Add block-kind prediction → blockKindPredictionFor(row) returns it.
    // -----------------------------------------------------------------------
    void block_kind_prediction_round_trip()
    {
        LiveProjectionLayer layer;

        BlockKindPrediction p;
        p.row = 5;
        p.originalKind = QStringLiteral("paragraph");
        p.speculativeKind = QStringLiteral("code_block");

        layer.createBlockKindPrediction(p);

        const auto *out = layer.blockKindPredictionFor(5);
        QVERIFY(out != nullptr);
        QCOMPARE(out->row, 5);
        QCOMPARE(out->originalKind, QStringLiteral("paragraph"));
        QCOMPARE(out->speculativeKind, QStringLiteral("code_block"));

        QCOMPARE(layer.blockKindPredictionFor(4), nullptr);
        QCOMPARE(layer.blockKindPredictionCount(), 1);
    }

    // -----------------------------------------------------------------------
    // T5.4 Add hole → rowIsHole(row) returns true.
    // -----------------------------------------------------------------------
    void block_hole_round_trip()
    {
        LiveProjectionLayer layer;
        QVERIFY(!layer.rowIsHole(0));

        BlockHole hole;
        hole.kind = QStringLiteral("paragraph");
        hole.reifyOffset = 0;
        hole.afterParsedRow = -1;

        const quint64 id = layer.createBlockHole(hole);
        QVERIFY(id != 0);

        QCOMPARE(layer.blockHoleCount(), 1);
        QVERIFY(layer.hasPendingBlockHole());
        QCOMPARE(layer.pendingBlockHoleId(), id);
        // Without a model wired, rowIsHole returns true whenever any hole is
        // registered (test-convenience path).
        QVERIFY(layer.rowIsHole(0));
    }

    void inline_hole_round_trip()
    {
        LiveProjectionLayer layer;

        InlineHole hole;
        hole.kind = QStringLiteral("wikilink_target");
        layer.createInlineHole(hole);

        QCOMPARE(layer.inlineHoleCount(), 1);
    }

    // -----------------------------------------------------------------------
    // T5.5 Reconcile-on-parse: placeholder, asserts no crash with empty
    // parse output.
    // -----------------------------------------------------------------------
    void reconcile_no_op_does_not_crash()
    {
        LiveProjectionLayer layer;

        // No items registered.
        layer.onParseUpdated();
        layer.onLocalEditApplied();

        // Stage-3: `onParseUpdated` now clears prediction registries
        // wholesale (inline + block-kind). Holes survive — they hold user
        // intent and are reconciled against subsequent input, not parse
        // return.
        InlinePrediction p;
        p.row = 0;
        p.charStart = 0;
        p.charEnd = 1;
        layer.createInlinePrediction(p);

        BlockHole hole;
        hole.kind = QStringLiteral("paragraph");
        layer.createBlockHole(hole);

        layer.onParseUpdated();
        layer.onLocalEditApplied();

        QCOMPARE(layer.inlinePredictionCount(), 0);
        QCOMPARE(layer.blockHoleCount(), 1);
    }

    // -----------------------------------------------------------------------
    // setEditorBackend / setBlockModel are pure setters in Stage 1.
    // -----------------------------------------------------------------------
    // -----------------------------------------------------------------------
    // T17-T19 v1 hole API.
    // -----------------------------------------------------------------------

    void v1_create_block_hole_inserts_row_with_is_hole_true()
    {
        LiveProjectionLayer layer;
        LiveBlockModel model;
        layer.setBlockModel(&model);
        QCOMPARE(model.rowCount(), 0);

        BlockHole h;
        h.kind = QStringLiteral("paragraph");
        h.reifyOffset = 0;
        h.afterParsedRow = -1;

        const quint64 id = layer.createBlockHole(h);
        QVERIFY(id != 0);
        QVERIFY(layer.hasPendingBlockHole());
        QCOMPARE(model.rowCount(), 1);

        const auto idx0 = model.index(0, 0);
        QCOMPARE(model.data(idx0, LiveBlockModel::IsHoleRole).toBool(), true);
        QCOMPARE(model.data(idx0, LiveBlockModel::HoleIdRole).value<quint64>(), id);
        QCOMPARE(model.data(idx0, LiveBlockModel::KindRole).toString(),
                 QStringLiteral("paragraph"));
        QCOMPARE(model.data(idx0, LiveBlockModel::TextRole).toString(), QString());
    }

    void v1_set_block_hole_buffer_updates_pending_and_model()
    {
        LiveProjectionLayer layer;
        LiveBlockModel model;
        layer.setBlockModel(&model);

        BlockHole h;
        h.kind = QStringLiteral("paragraph");
        const quint64 id = layer.createBlockHole(h);

        QSignalSpy spy(&layer, &LiveProjectionLayer::bufferChanged);
        layer.setBlockHoleBuffer(id, QStringLiteral("hello"));
        QCOMPARE(layer.pendingBlockHoleBuffer(), QStringLiteral("hello"));

        const auto idx0 = model.index(0, 0);
        QCOMPARE(model.data(idx0, LiveBlockModel::TextRole).toString(),
                 QStringLiteral("hello"));
        QCOMPARE(spy.count(), 1);

        // Stale id is a no-op.
        layer.setBlockHoleBuffer(9999, QStringLiteral("nope"));
        QCOMPARE(layer.pendingBlockHoleBuffer(), QStringLiteral("hello"));
    }

    void v1_commit_block_hole_drops_row_writes_source_emits_about_to_commit()
    {
        Markoff::MarkoffDocument doc(1);
        EditorBackend backend;
        backend.setDocument(&doc);

        LiveProjectionLayer layer;
        LiveBlockModel model;
        layer.setEditorBackend(&backend);
        layer.setBlockModel(&model);

        BlockHole h;
        h.kind = QStringLiteral("paragraph");
        h.reifyOffset = 0;
        h.afterParsedRow = -1;
        const quint64 id = layer.createBlockHole(h);
        layer.setBlockHoleBuffer(id, QStringLiteral("hi"));
        QCOMPARE(model.rowCount(), 1);

        QSignalSpy aboutToCommitSpy(&layer, &LiveProjectionLayer::aboutToCommit);
        QSignalSpy reifiedSpy(&layer, &LiveProjectionLayer::holeReified);

        // Track ordering: aboutToCommit must arrive BEFORE the row is removed
        // and BEFORE the source mutation. Capture both via a slot.
        QList<int> orderedRowCounts;
        QObject::connect(&layer, &LiveProjectionLayer::aboutToCommit, &layer,
            [&](quint64) { orderedRowCounts.append(model.rowCount()); });

        layer.commitBlockHole(id);

        QCOMPARE(aboutToCommitSpy.count(), 1);
        QCOMPARE(aboutToCommitSpy.first().first().value<quint64>(), id);
        // At aboutToCommit time the row was still present (rowCount==1).
        QCOMPARE(orderedRowCounts.size(), 1);
        QCOMPARE(orderedRowCounts.first(), 1);

        QVERIFY(!layer.hasPendingBlockHole());
        QCOMPARE(model.rowCount(), 0);

        // Document source contains "\n\n" + buffer at reifyOffset(=0).
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("\n\nhi"));

        // holeReified hasn't been emitted yet — it requires a parse round-trip
        // that inserts a row at the expected viewRow. Fire that synthetically
        // via applyOps: insert a paragraph block at row 0.
        QCOMPARE(reifiedSpy.count(), 0);

        BlockRecord rec;
        rec.kind = QStringLiteral("paragraph");
        rec.text = QStringLiteral("hi");
        AstBlockDiff::Op insertOp;
        insertOp.kind = AstBlockDiff::OpKind::Insert;
        insertOp.nextIndex = 0;
        model.applyOps({ insertOp }, { rec });

        QCOMPARE(reifiedSpy.count(), 1);
        QCOMPARE(reifiedSpy.first().at(0).toInt(), 0);                  // viewRow
        QCOMPARE(reifiedSpy.first().at(1).toInt(), int(2));             // qtPos = "hi".length()
    }

    void v1_drop_block_hole_does_not_mutate_source()
    {
        Markoff::MarkoffDocument doc(1);
        EditorBackend backend;
        backend.setDocument(&doc);

        LiveProjectionLayer layer;
        LiveBlockModel model;
        layer.setEditorBackend(&backend);
        layer.setBlockModel(&model);

        BlockHole h;
        h.kind = QStringLiteral("paragraph");
        h.afterParsedRow = -1;
        const quint64 id = layer.createBlockHole(h);
        layer.setBlockHoleBuffer(id, QStringLiteral("partial"));
        QCOMPARE(model.rowCount(), 1);

        QSignalSpy droppedSpy(&layer, &LiveProjectionLayer::holeDropped);
        layer.dropBlockHole(id);

        QCOMPARE(droppedSpy.count(), 1);
        QCOMPARE(droppedSpy.first().first().toInt(), 0);
        QVERIFY(!layer.hasPendingBlockHole());
        QCOMPARE(model.rowCount(), 0);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
    }

    void v1_create_while_pending_commits_or_drops_prior()
    {
        // Prior empty → drop.
        {
            Markoff::MarkoffDocument doc(1);
            EditorBackend backend;
            backend.setDocument(&doc);
            LiveProjectionLayer layer;
            LiveBlockModel model;
            layer.setEditorBackend(&backend);
            layer.setBlockModel(&model);

            BlockHole h1;
            h1.kind = QStringLiteral("paragraph");
            const quint64 id1 = layer.createBlockHole(h1);

            BlockHole h2 = h1;
            const quint64 id2 = layer.createBlockHole(h2);

            QVERIFY(id2 != id1);
            QCOMPARE(layer.pendingBlockHoleId(), id2);
            QCOMPARE(model.rowCount(), 1);
            QCOMPARE(doc.toMarkdownUtf8(), QByteArray());  // prior drop, no source
        }

        // Prior non-empty → commit.
        {
            Markoff::MarkoffDocument doc(1);
            EditorBackend backend;
            backend.setDocument(&doc);
            LiveProjectionLayer layer;
            LiveBlockModel model;
            layer.setEditorBackend(&backend);
            layer.setBlockModel(&model);

            BlockHole h1;
            h1.kind = QStringLiteral("paragraph");
            h1.reifyOffset = 0;
            const quint64 id1 = layer.createBlockHole(h1);
            layer.setBlockHoleBuffer(id1, QStringLiteral("kept"));

            BlockHole h2;
            h2.kind = QStringLiteral("paragraph");
            h2.reifyOffset = 6;  // post-commit position; the test only checks the prior committed
            const quint64 id2 = layer.createBlockHole(h2);

            QCOMPARE(layer.pendingBlockHoleId(), id2);
            QCOMPARE(doc.toMarkdownUtf8(), QByteArray("\n\nkept"));
        }
    }

    void v1_commit_all_pending_holes_drops_or_commits()
    {
        // Empty → drop, no source.
        {
            Markoff::MarkoffDocument doc(1);
            EditorBackend backend;
            backend.setDocument(&doc);
            LiveProjectionLayer layer;
            LiveBlockModel model;
            layer.setEditorBackend(&backend);
            layer.setBlockModel(&model);

            BlockHole h;
            h.kind = QStringLiteral("paragraph");
            layer.createBlockHole(h);
            layer.commitAllPendingHoles();

            QVERIFY(!layer.hasPendingBlockHole());
            QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
        }
        // Non-empty → commit, source updated.
        {
            Markoff::MarkoffDocument doc(1);
            EditorBackend backend;
            backend.setDocument(&doc);
            LiveProjectionLayer layer;
            LiveBlockModel model;
            layer.setEditorBackend(&backend);
            layer.setBlockModel(&model);

            BlockHole h;
            h.kind = QStringLiteral("paragraph");
            h.reifyOffset = 0;
            const quint64 id = layer.createBlockHole(h);
            layer.setBlockHoleBuffer(id, QStringLiteral("X"));
            layer.commitAllPendingHoles();

            QVERIFY(!layer.hasPendingBlockHole());
            QCOMPARE(doc.toMarkdownUtf8(), QByteArray("\n\nX"));
        }
    }

    void v1_stacked_enter_two_empty_creates_net_one_hole()
    {
        Markoff::MarkoffDocument doc(1);
        EditorBackend backend;
        backend.setDocument(&doc);
        LiveProjectionLayer layer;
        LiveBlockModel model;
        layer.setEditorBackend(&backend);
        layer.setBlockModel(&model);

        BlockHole h;
        h.kind = QStringLiteral("paragraph");
        const quint64 id1 = layer.createBlockHole(h);
        const quint64 id2 = layer.createBlockHole(h);

        QVERIFY(id2 != id1);
        QVERIFY(layer.hasPendingBlockHole());
        QCOMPARE(layer.pendingBlockHoleId(), id2);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(doc.toMarkdownUtf8(), QByteArray());
    }

    void v1_about_to_commit_can_inject_final_text_before_snapshot()
    {
        Markoff::MarkoffDocument doc(1);
        EditorBackend backend;
        backend.setDocument(&doc);
        LiveProjectionLayer layer;
        LiveBlockModel model;
        layer.setEditorBackend(&backend);
        layer.setBlockModel(&model);

        BlockHole h;
        h.kind = QStringLiteral("paragraph");
        h.reifyOffset = 0;
        const quint64 id = layer.createBlockHole(h);
        layer.setBlockHoleBuffer(id, QStringLiteral("draft"));

        // T21 will wire the delegate's IME-finalize on aboutToCommit; here we
        // simulate that slot writing the final value.
        QObject::connect(&layer, &LiveProjectionLayer::aboutToCommit, &layer,
            [&](quint64 hid) { layer.setBlockHoleBuffer(hid, QStringLiteral("FINAL")); });

        layer.commitBlockHole(id);

        QCOMPARE(doc.toMarkdownUtf8(), QByteArray("\n\nFINAL"));
    }

    void setters_store_pointers()
    {
        LiveProjectionLayer layer;
        QCOMPARE(layer.editorBackend(), nullptr);
        QCOMPARE(layer.blockModel(), nullptr);

        layer.setEditorBackend(nullptr);
        layer.setBlockModel(nullptr);
        QCOMPARE(layer.editorBackend(), nullptr);
        QCOMPARE(layer.blockModel(), nullptr);
    }
};

QTEST_MAIN(TstLiveProjectionLayer)
#include "tst_view_qml_live_projection_layer.moc"
