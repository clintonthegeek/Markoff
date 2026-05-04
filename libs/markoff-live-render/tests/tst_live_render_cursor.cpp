// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/live-render/Cursor.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/BlockHitTester.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/AstBlockDiff.h>
#include <markoff/live-render/LiveListModelBinding.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>

using namespace Markoff::LiveRender;

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
    r.blockAnchor.firstByte.charValue = s_anchorCounter++;
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
        LiveCursorState cs(&reg, &model);
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));
        QCOMPARE(cs.cursorKind(), QStringLiteral("none"));
    }

    void request_text_caret_emits_cursor_changed() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto recs = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "hello") };
        model.applyOps(AstBlockDiff::diff({}, { keyOf(recs[0]) }), recs);

        LiveCursorState cs(&reg, &model);
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

        LiveCursorState cs(&reg, &model);
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

        LiveCursorState cs(&reg, &model);
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

        LiveCursorState cs(&reg, &model);
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

        LiveCursorState cs(&reg, &model);
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

        LiveCursorState cs(&reg, &model);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        cs.requestTextCaretAtRow(/*expectedRow=*/1, /*qtPos=*/0);

        QCOMPARE(spy.count(), 1);
        const Cursor cur = cs.cursor();
        QVERIFY(std::holds_alternative<TextCaret>(cur));
        QCOMPARE(std::get<TextCaret>(cur).cachedByteOffset, quint32(0));
        QCOMPARE(anchorOf(std::get<TextCaret>(cur).block), recs[1].blockAnchor);
    }

    void requestTextCaretAtRow_pending_resolves_on_rowsInserted() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto first = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "alpha") };
        QList<BlockKey> firstKeys; firstKeys << keyOf(first[0]);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), first);

        LiveCursorState cs(&reg, &model);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        // Request row 1: doesn't exist yet (rowCount == 1, valid rows are 0..0).
        cs.requestTextCaretAtRow(/*expectedRow=*/1, /*qtPos=*/0);

        // No cursorChanged yet — pending.
        QCOMPARE(spy.count(), 0);
        QVERIFY(std::holds_alternative<NoCursor>(cs.cursor()));

        // Row 1 appears: applyOps with an Insert at row 1.
        const auto second = QList<BlockRecord>{
            first[0],
            makeRec(BlockKind::Paragraph, "beta"),
        };
        QList<BlockKey> secondKeys;
        for (const auto &r : second) secondKeys << keyOf(r);
        model.applyOps(AstBlockDiff::diff(firstKeys, secondKeys), second);

        QCOMPARE(spy.count(), 1);
        QVERIFY(std::holds_alternative<TextCaret>(cs.cursor()));
        QCOMPARE(anchorOf(std::get<TextCaret>(cs.cursor()).block), second[1].blockAnchor);
    }

    void requestTextCaretAtRow_pending_dropped_after_two_parse_cycles() {
        BlockKindRegistry reg;
        LiveBlockModel model;
        const auto first = QList<BlockRecord>{ makeRec(BlockKind::Paragraph, "alpha") };
        QList<BlockKey> firstKeys; firstKeys << keyOf(first[0]);
        model.applyOps(AstBlockDiff::diff({}, firstKeys), first);

        LiveCursorState cs(&reg, &model);
        QSignalSpy spy(&cs, &LiveCursorState::cursorChanged);

        cs.requestTextCaretAtRow(/*expectedRow=*/1, /*qtPos=*/0);

        // Two parse arrivals with no row insertion at the expected row:
        // pending should be dropped, cursorChanged not fired.
        cs.noteParseArrived(/*parseSeq=*/1);
        cs.noteParseArrived(/*parseSeq=*/2);

        // A third parse with the row inserted should NOT fire (pending dropped).
        const auto third = QList<BlockRecord>{
            first[0],
            makeRec(BlockKind::Paragraph, "beta"),
        };
        QList<BlockKey> thirdKeys;
        for (const auto &r : third) thirdKeys << keyOf(r);
        model.applyOps(AstBlockDiff::diff(firstKeys, thirdKeys), third);

        QCOMPARE(spy.count(), 0);
    }

    void requestTextCaretAtNewRow_markerParagraph_landsAtQtPos0() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);

        // Initial: one paragraph; cursor at end of it.
        document.resetContent(QByteArrayLiteral("alpha\n"), Markoff::Origin::TestFixture);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);

        // Schedule a pending request for "the row that's about to be born".
        binding.cursorState()->requestTextCaretAtNewRow(/*expectedRow=*/1, /*qtPos=*/0);

        // Insert "\n\n<ZWSP>" at end of "alpha". The new row arrives
        // asynchronously via parse-back; the pending request resolves on
        // its rowsInserted.
        Markoff::MarkoffEdit ed;
        ed.oldStart = 5; ed.oldEnd = 5;
        ed.newText  = QByteArrayLiteral("\n\n\xE2\x80\x8B");
        document.applyLocalEdit({ ed });

        // Wait for parse-back to arrive (resolves the pending cursor request).
        QVERIFY(parseSpy.wait(2000));

        // Verify the cursor was placed at row 1, qtPos 0.
        QTRY_COMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.cursorState()->focusedAnchorRow(), 1);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 0);
    }

    // ---- LiveListModelBinding: cachedByteOffset refresh tests ----

    void textcaret_cached_offset_refreshes_on_parse_arrival() {
        Markoff::MarkoffDocument document(/*replicaId=*/1);

        LiveListModelBinding binding;
        binding.setDocument(&document);

        QSignalSpy parseSpy(&document, &Markoff::MarkoffDocument::parseUpdated);
        document.resetContent("hello world", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);

        const auto blockAnchor = binding.model()->recordAt(0).blockAnchor;

        // Place a caret at byte offset 3 (inside "hello", on 'l').
        TextCaret tc;
        tc.block = blockAnchor;
        tc.positionAnchor = document.textAnchorAt(blockAnchor, /*offset=*/3, /*rightBias=*/true);
        tc.cachedByteOffset = 3;
        binding.cursorState()->request(tc);

        // Prepend a paragraph above. The anchor at offset 3 should still
        // resolve to byte 3 within the (now second) block; the absolute
        // resolved byte changes (it's now in a later position in the doc).
        Markoff::MarkoffEdit prepend;
        prepend.oldStart = 0; prepend.oldEnd = 0;
        prepend.newText = "before\n\n";
        document.applyLocalEdit({ prepend });
        QVERIFY(parseSpy.wait(2000));

        const auto refreshed = std::get<TextCaret>(binding.cursorState()->cursor());
        // Verify cachedByteOffset matches the resolved-relative-to-block-start.
        const auto blockRangeOpt = document.blockByteRange(anchorOf(refreshed.block));
        QVERIFY(blockRangeOpt.has_value());
        const quint32 blockStart = blockRangeOpt->first;
        const quint32 resolvedAbs = document.resolveTextAnchor(refreshed.positionAnchor);
        QCOMPARE(refreshed.cachedByteOffset, resolvedAbs - blockStart);
    }
};

QTEST_GUILESS_MAIN(TstLiveRenderCursor)
#include "tst_live_render_cursor.moc"
