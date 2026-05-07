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
    return BlockKey{ r.kind, r.blockAnchor };
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
        tc.cachedByteOffset = 0;
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
        tc.cachedByteOffset = 0;
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
        tc.cachedByteOffset = 3;
        LiveRenderSelection sel;
        sel.anchor = tc;
        sel.active = tc;
        QVERIFY(sel.isCaret());
        QVERIFY(sel.isCollapsed());
    }

    void selection_non_collapsed() {
        TextCaret a, b;
        a.cachedByteOffset = 0;
        b.cachedByteOffset = 5;
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

    void requestTextCaretAtRow_already_exists_resolves_immediately() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{
            makeRec(BlockKind::Paragraph, "alpha"),
            makeRec(BlockKind::Paragraph, "beta"),
        };
        QList<BlockKey> keys;
        for (const auto &r : recs) keys << keyOf(r);
        model.applyOps(AstBlockDiff::diff({}, keys), recs);

        LiveCursorState cs(&reg, &model, /*binding=*/nullptr);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        cs.requestTextCaretAtRow(/*expectedRow=*/1, /*qtPos=*/0);

        QCOMPARE(spy.count(), 1);
        const Cursor cur = cs.cursor();
        QVERIFY(std::holds_alternative<TextCaret>(cur));
        QCOMPARE(std::get<TextCaret>(cur).cachedByteOffset, quint32(0));
        QCOMPARE(std::get<TextCaret>(cur).block, recs[1].blockAnchor);
    }

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

    void requestTextCaretAtNewRow_landsAtQtPos0() {
        // D2 version: use loadFromMarkdown + structureChanged to get model rows.
        // Then use Cmd::enterAtEnd to create a new block and verify the pending
        // cursor request resolves at the new row.
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        document.loadFromMarkdown("alpha");
        // loadFromMarkdown fires structureChanged synchronously → model rows are
        // already populated. Use QTRY_COMPARE as a safety net.
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        // Get the block anchor.
        const Markoff::BlockId block0 = binding.model()->recordAt(0).blockAnchor;

        // Schedule a pending request for "the row that's about to be born".
        binding.cursorState()->requestTextCaretAtNewRow(/*expectedRow=*/1, /*qtPos=*/0);

        // Create a new block after block0 using D2 API.
        Markoff::Cmd::enterAtEnd(document, block0);

        // The new row should arrive via structureChanged → onD2Changed → rowsInserted.
        // The pending cursor request resolves on rowsInserted.
        QTRY_COMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.cursorState()->focusedAnchorRow(), 1);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 0);
    }

    void requestTextCaretAtRow_pending_resolves_on_structural_insert()
    {
        // Set up a single-block document
        Markoff::MarkoffDocument document(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&document);

        document.loadFromMarkdown("Hello\n");
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        // Get the block anchor for the existing block.
        const Markoff::BlockId block0 = binding.model()->recordAt(0).blockAnchor;

        // Request a TextCaret at row 1, which does not yet exist
        binding.cursorState()->requestTextCaretAtRow(1, 0);

        // The pending should not have resolved yet (row 1 doesn't exist)
        QCOMPARE(binding.cursorState()->cursorKind(), QStringLiteral("none"));

        // Apply a D2 command that inserts a new block (which fires structuralRowsInserted)
        Markoff::Cmd::enterAtEnd(document, block0);
        // Process events so the debounced d2DocumentChanged fires → onD2Changed → structural signal
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        // The pending should now be resolved: cursor at row 1, qtPos 0
        QCOMPARE(binding.cursorState()->cursorKind(), QStringLiteral("TextCaret"));
        QCOMPARE(binding.cursorState()->focusedAnchorRow(), 1);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 0);
    }

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

    // ---- LiveListModelBinding: structural signals ----

    void structural_rows_inserted_emitted_on_new_block()
    {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello");
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        QSignalSpy spy(&binding, &LiveListModelBinding::structuralRowsInserted);

        // Insert a new block (Transaction commits on scope exit)
        auto ids = doc.iterateBlocks();
        QVERIFY(!ids.empty());
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2InsertBlock(ids.back(), Markoff::BlockKind::Paragraph, t);
        }

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        // row index for the inserted block
        QCOMPARE(spy[0][0].toInt(), 1);
    }

    void structural_row_removed_emitted_on_block_removal()
    {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("hello\n\nworld");
        QTRY_COMPARE(binding.model()->rowCount(), 2);

        QSignalSpy spy(&binding, &LiveListModelBinding::structuralRowRemoved);

        auto ids = doc.iterateBlocks();
        QVERIFY(ids.size() >= 2);
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2RemoveBlock(ids[1], t);
        }

        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
        QCOMPARE(spy[0][0].toInt(), 1);
    }
};

QTEST_GUILESS_MAIN(TstLiveRenderCursor)
#include "tst_live_render_cursor.moc"
