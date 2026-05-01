// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

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
        p.byteStart = 0;
        p.byteEnd = 4;
        QTextCharFormat fmt;
        fmt.setFontWeight(QFont::Bold);
        p.format = fmt;

        layer.createInlinePrediction(p);

        const auto out = layer.predictionsForRow(3);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().row, 3);
        QCOMPARE(out.first().byteStart, 0);
        QCOMPARE(out.first().byteEnd, 4);
        QCOMPARE(out.first().format.fontWeight(), int(QFont::Bold));

        // Other rows return empty.
        QVERIFY(layer.predictionsForRow(2).isEmpty());
        QVERIFY(layer.predictionsForRow(4).isEmpty());

        // Multiple predictions on the same row accumulate.
        InlinePrediction q = p;
        q.byteStart = 6;
        q.byteEnd = 10;
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
        hole.pairedSourceEditByteCount = 2;

        layer.createBlockHole(hole);

        QCOMPARE(layer.blockHoleCount(), 1);
        // Stage-1: rowIsHole returns true once any block hole is registered;
        // Stage 4 narrows this to a row-specific check once the model
        // interleaves holes.
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
        p.byteStart = 0;
        p.byteEnd = 1;
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
