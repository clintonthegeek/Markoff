// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/live/Cursor.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockHitTester.h>
#include <markoff/live/BlockKind.h>
#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/AstBlockDiff.h>
#include <markoff/live/LiveListModelBinding.h>

#include "KindDispatch.h"

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/CrdtProxies.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff::Live;

// ---- helpers ----

static BlockRecord makeRec(const QString &kind, const QString &text,
                            int headingLevel = 0)
{
    // Each call produces a record with a unique blockAnchor so diff keys
    // are distinct. (Real records get anchors from the CRDT; tests use a
    // simple counter to avoid collisions in AstBlockDiff.)
    static quint32 s_anchorCounter = 1;
    BlockRecord r;
    r.kind = kind;
    r.text = text;
    r.headingLevel = headingLevel;
    r.blockAnchor = Markoff::BlockId::fromRaw(s_anchorCounter++);
    return r;
}

static BlockKey keyOf(const BlockRecord &r)
{
    const QString cls = r.delegateClass.isEmpty()
        ? Markoff::Live::delegateClassFor(r.kind)
        : r.delegateClass;
    return BlockKey{ cls, r.blockAnchor };
}

// No mock ListView needed — the hit-test math lives in LiveView.qml (JS),
// so BlockHitTester is just a reportHit relay. Its tests verify that relay.

// ---- Test class ----

class TstLiveRenderCursor : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---- LiveCursorState tests ----

    void cursor_starts_with_no_focus() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));
        QCOMPARE(cs.cursorKind(), QStringLiteral("none"));
    }

    void request_text_caret_emits_cursor_changed() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "hello") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        TextCaret tc;
        tc.block = recs[0].blockAnchor;
        tc.cachedQtPos = 0;
        cs.request(tc);

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::holds_alternative<TextCaret>(cs.cursor()));
        QCOMPARE(cs.cursorKind(), QStringLiteral("TextCaret"));
    }

    void request_block_selected_for_hr() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::HorizontalRule, "---") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        BlockSelected bs;
        bs.block = recs[0].blockAnchor;
        cs.request(bs);

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::holds_alternative<BlockSelected>(cs.cursor()));
        QCOMPARE(cs.cursorKind(), QStringLiteral("BlockSelected"));
    }

    void request_invalid_variant_for_kind_is_rejected() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "hello") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        BlockSelected bs;
        bs.block = recs[0].blockAnchor;
        cs.request(bs);  // BlockSelected on paragraph = invalid

        QCOMPARE(spy.count(), 0);
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));
    }

    void request_same_cursor_emits_no_signal() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "hi") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        TextCaret tc;
        tc.block = recs[0].blockAnchor;
        tc.cachedQtPos = 0;
        cs.request(tc);

        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);
        cs.request(tc);
        QCOMPARE(spy.count(), 0);
    }

    void clear_resets_to_no_cursor() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "hi") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        TextCaret tc;
        tc.block = recs[0].blockAnchor;
        cs.request(tc);
        QVERIFY(!std::holds_alternative<NoCursor>(cs.cursor()));

        cs.clear();
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));
    }

    // ---- LiveRenderSelection type tests ----

    void selection_collapsed_caret() {
        TextCaret tc;
        tc.cachedQtPos = 3;
        LiveRenderSelection sel;
        sel.anchor = tc;
        sel.active = tc;
        QVERIFY(sel.isCaret());
        QVERIFY(sel.isCollapsed());
    }

    void selection_non_collapsed() {
        TextCaret a, b;
        a.cachedQtPos = 0;
        b.cachedQtPos = 5;
        LiveRenderSelection sel;
        sel.anchor = a;
        sel.active = b;
        QVERIFY(!sel.isCollapsed());
        QVERIFY(!sel.isCaret());
    }

    void selection_no_focus() {
        LiveRenderSelection sel;
        QVERIFY(sel.hasNoFocus());
    }

    // ---- BlockHitTester tests ----
    // The hit-test math lives in LiveView.qml (JS). BlockHitTester is a
    // simple relay: QML calls reportHit(), C++ emits hitReported().

    void hit_tester_report_hit_emits_signal() {
        BlockHitTester ht;
        QSignalSpy spy(&ht, &BlockHitTester::hitReported);
        ht.reportHit(3, 12);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 3);
        QCOMPARE(spy.at(0).at(1).toInt(), 12);
        QCOMPARE(ht.lastBlockIndex(), 3);
        QCOMPARE(ht.lastQtPos(), 12);
    }

    void hit_tester_starts_with_miss() {
        BlockHitTester ht;
        QCOMPARE(ht.lastBlockIndex(), -1);
        QCOMPARE(ht.lastQtPos(), -1);
    }

    // ---- LiveCursorState: requestTextCaretAtRow tests ----

    // requestTextCaretAtRow_already_exists_resolves_immediately moved to
    // tst_live_render_cursor_qml.cpp — the focus-chokepoint refactor routes
    // requestTextCaretAtRow through establishFocus, which only fires
    // cursorChanged when a delegate is registered for the target anchor.
    // Direct unit-test setups can't supply that.

    void requestTextCaretAtRow_pending_cleared_by_clear() {
        // A pending requestTextCaretAtRow that has not resolved yet is
        // cancelled when clear() is called. No cursorChanged fires.
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "alpha") };
        QList<BlockKey> keys; keys << keyOf(recs[0]);
        model.applyOps(AstBlockDiff::diff({}, keys), recs);

        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        // Request row 1: doesn't exist yet.
        cs.requestTextCaretAtRow(/*expectedRow=*/1, /*qtPos=*/0);
        QCOMPARE(spy.count(), 0);

        cs.clear();
        QCOMPARE(spy.count(), 0);
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));
    }

    void enterAtEnd_landsFocusOnNewRowViaChokepoint() {
        // Production path: structural-key handler calls Cmd::enterAtEnd, then
        // calls LiveCursorState::establishFocus on the newly-created BlockAnchor.
        // The chokepoint stages the pending and tryResolvePending picks the
        // correct delegate once it registers. Pre-tier-4b this lived under
        // requestTextCaretAtNewRow, which had its own pending slot (m_pendingRow)
        // resolved against binding-side structural signals; that path was deleted
        // in tier 4b. See docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md.
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        document.loadFromMarkdown("alpha");
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        const Markoff::BlockId block0 = binding.model()->recordAt(0).blockAnchor;

        // Create the new block first (structural edit completes).
        Markoff::Cmd::enterAtEnd(document, block0);
        QTRY_COMPARE(binding.model()->rowCount(), 2);

        // Resolve the new row's BlockAnchor and stage focus through the chokepoint.
        const Markoff::BlockId block1 = binding.model()->recordAt(1).blockAnchor;
        binding.cursorState()->establishFocus(block1, /*qtPos=*/0);

        // No delegate is registered in this unit-test fixture (no QML view), so
        // the chokepoint holds the pending. The cursor state observable here is
        // the pending slot's contents — not the resolved cursor. To assert the
        // pending-side observable: hasPendingFocus() returns true.
        //
        // For the resolved-side assertion (focusedAnchorRow == 1), see
        // tst_live_render_focus_chokepoint_invariant — that file uses the QML
        // integration fixture which DOES register delegates. This unit-test slot
        // covers the chokepoint-staging step only.
        QVERIFY(binding.cursorState()->hasPendingFocus());
    }

    // requestTextCaretAtRow_pending_resolves_on_structural_insert removed —
    // the chokepoint API no longer supports "pending request for a row that
    // doesn't yet exist" via requestTextCaretAtRow (out-of-range rows are
    // rejected synchronously). That semantic now lives under the chokepoint,
    // covered by enterAtEnd_landsFocusOnNewRowViaChokepoint above.

    // ---- LiveListModelBinding: D2 model drive via structureChanged ----

    void model_populates_from_d2_load() {
        // Verify the model drives from loadFromMarkdown via D2 CRDT signals.
        // structureChanged fires synchronously inside loadFromMarkdown, so rows
        // are available immediately after the call returns.
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&document);

        QCOMPARE(binding.model()->rowCount(), 0);

        document.loadFromMarkdown("first\n\nsecond");
        // Synchronous signal path: rows should be populated immediately.
        QCOMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.model()->recordAt(0).text, QStringLiteral("first"));
        QCOMPARE(binding.model()->recordAt(1).text, QStringLiteral("second"));
    }

    // ---- LiveBlockModel: row-mutation signals ----

    void blockModel_emits_rowsInserted_on_new_block()
    {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello");
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        QSignalSpy spy(binding.model(), &QAbstractItemModel::rowsInserted);

        // Insert a new block (Transaction commits on scope exit)
        auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2InsertBlock(ids.back(), Markoff::BlockKind::Paragraph, t);
        }

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        // QAbstractItemModel::rowsInserted signature: (parent, first, last)
        QCOMPARE(spy[0][1].toInt(), 1);  // first
        QCOMPARE(spy[0][2].toInt(), 1);  // last
    }

    void blockModel_emits_rowsRemoved_on_block_removal()
    {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello\n\nworld");
        QTRY_COMPARE(binding.model()->rowCount(), 2);

        QSignalSpy spy(binding.model(), &QAbstractItemModel::rowsRemoved);

        auto ids = doc.iterateBlocks();
        QVERIFY(ids.size() >= 2);
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2RemoveBlock(ids[1], t);
        }

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        QCOMPARE(spy[0][1].toInt(), 1);  // first
        QCOMPARE(spy[0][2].toInt(), 1);  // last
    }

    // ---- LiveCursorState: desiredVisualX column-preservation tests ----

    void desired_visual_x_default_is_unset() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        QCOMPARE(cs.desiredVisualX(), -1.0);
    }

    void desired_visual_x_persists_across_set_and_get() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        cs.setDesiredVisualX(42.5);
        QCOMPARE(cs.desiredVisualX(), 42.5);
    }

    void clear_desired_visual_x_resets_to_sentinel() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        cs.setDesiredVisualX(42.5);
        cs.clearDesiredVisualX();
        QCOMPARE(cs.desiredVisualX(), -1.0);
    }

    // request_text_caret_at_row_visual_x_records_hint moved to
    // tst_live_render_cursor_qml.cpp — hint-clearing depends on the chokepoint
    // actually resolving the cursor, which requires a registered delegate.
};

QTEST_GUILESS_MAIN(TstLiveRenderCursor)
#include "tst_live_render_cursor.moc"
