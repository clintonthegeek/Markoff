// SPDX-License-Identifier: GPL-3.0-or-later
//
// P3.4 — FindController integration (contract-v2 plan).
//
// EditorWidget::attachFindController subscribes to
// matchesChanged/currentMatchChanged and pushes draw-time find highlights
// onto the composed View (never QTextCharFormat/setFormats — see
// View::setFindHighlights's doc comment); navigationRequested scrolls the
// match into view and places a non-focusing caret via
// View::setCaretPosition. detachFindController clears all highlight paint
// state.

#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::EditorWidget;
using Markoff::Canvas::FindHighlight;
using Markoff::Canvas::View;
using Markoff::FindController;
using Markoff::MarkoffDocument;

namespace {

// "the cat" (block 0) / "sat on the mat" (block 1) — mirrors the live
// leaf's find-adapter test fixture (tst_live_find_adapter.cpp) so the
// byte offsets below are cross-checked against a known-good reference.
QByteArray fixture() { return QByteArray("the cat\n\nsat on the mat"); }

}  // namespace

class TstCanvasFind : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<Markoff::FindController::Match>();
    }

    // matchesChanged: all matches get a highlight; currentMatchChanged:
    // exactly one carries isCurrent, and it moves with findNext().
    void matches_and_current_match_paint_highlights() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(fixture());
        const auto ids = doc.iterateBlocks();
        QCOMPARE(ids.size(), 2);
        const BlockId block0 = ids[0];
        const BlockId block1 = ids[1];

        EditorWidget ed;
        ed.setDocument(&doc);

        FindController fc(&doc);
        ed.attachFindController(&fc);
        fc.activate();
        fc.setNeedle("the");

        // "the cat" -> "the" at byte 0; "sat on the mat" -> "the" at byte 7.
        const auto h0 = ed.view()->findHighlightsForBlock(block0);
        const auto h1 = ed.view()->findHighlightsForBlock(block1);
        QCOMPARE(h0.size(), 1);
        QCOMPARE(h1.size(), 1);
        QCOMPARE(h0.first().byteOffset, 0);
        QCOMPARE(h0.first().byteLength, 3);
        QCOMPARE(h1.first().byteOffset, 7);
        QCOMPARE(h1.first().byteLength, 3);

        // currentMatchIndex starts at 0 -> block0's match is current.
        QVERIFY(h0.first().isCurrent);
        QVERIFY(!h1.first().isCurrent);

        fc.findNext();

        const auto h0b = ed.view()->findHighlightsForBlock(block0);
        const auto h1b = ed.view()->findHighlightsForBlock(block1);
        QVERIFY(!h0b.first().isCurrent);
        QVERIFY(h1b.first().isCurrent);
    }

    // navigationRequested (fired by findNext/findPrevious): places the
    // caret at the match and scrolls it into view, without stealing focus
    // (FindController's documented contract).
    void navigate_places_caret_and_scrolls_without_focus() {
        MarkoffDocument doc(1);
        // Enough blocks that block1's match is off-screen in a small view.
        QByteArray big = "the cat\n\n";
        for (int i = 0; i < 60; ++i)
            big += "filler paragraph line\n\n";
        big += "sat on the mat\n";
        doc.loadFromMarkdown(big);
        const auto ids = doc.iterateBlocks();
        const BlockId block0 = ids.front();
        const BlockId block1 = ids.back();

        EditorWidget ed;
        ed.resize(300, 120);
        ed.setDocument(&doc);
        ed.show();
        QVERIFY(QTest::qWaitForWindowExposed(&ed));
        ed.view()->clearFocus();
        QVERIFY(!ed.view()->hasFocus());

        FindController fc(&doc);
        ed.attachFindController(&fc);
        fc.activate();
        fc.setNeedle("the");
        // setNeedle alone lands currentMatchIndex at 0 (block0's match) but
        // does NOT emit navigationRequested (FindController's documented
        // contract: only findNext/findPrevious do) — the caret is still
        // wherever setDocument() left it.
        QCOMPARE(ed.view()->caretBlock(), block0);
        QCOMPARE(ed.view()->caretByteOffset(), 0);

        fc.findNext();  // index 0 -> 1: block1's match

        QCOMPARE(ed.view()->caretBlock(), block1);
        QVERIFY(ed.view()->caretByteOffset() > 0);
        QVERIFY(!ed.view()->hasFocus());  // never stole focus
        // Scrolled: block1 is far down the document, so the scrollbar must
        // have moved off zero to bring it into view.
        QVERIFY(ed.view()->verticalScrollBar()->value() > 0);

        fc.findNext();  // index 1 -> 0 (wraps): block0's match again

        QCOMPARE(ed.view()->caretBlock(), block0);
        QCOMPARE(ed.view()->caretByteOffset(), 0);
        QVERIFY(!ed.view()->hasFocus());

        ed.hide();
    }

    // F3/find-next regression (punch-list [cluster-k]): repeated findNext()
    // calls across matches whose true (post-wrap) positions are far outside
    // the initial viewport. Root cause was that ensureCaretVisible() (called
    // from onFindNavigationRequested -> setCaretPosition -> setCaret) aims
    // the scrollbar using BlockLayoutCache's cheap line-count ESTIMATE for
    // every block between the old viewport and the match — see
    // BlockLayoutCache::estimateHeight()'s doc comment: wrapped lines are
    // under-counted, so a jump into never-realized territory undershoots.
    // Nothing used to re-check the scroll position once realizeRange()
    // (paintEvent -> ensureLayoutForViewport()) replaced those estimates
    // with real heights, so the undershoot never got corrected — from the
    // user's chair, F3 looked like it stopped doing anything once the
    // current match scrolled off-screen. Driving each findNext() through a
    // real event-loop spin (QTest::qWait) lets paintEvent actually run
    // between navigations, mirroring interactive F3 presses (each a
    // separate event with the previous paint already settled), unlike
    // navigate_places_caret_and_scrolls_without_focus() above which never
    // pumps the loop.
    void repeated_find_next_keeps_match_onscreen_across_wrapped_content() {
        MarkoffDocument doc(1);

        // Long, naturally-wrapping paragraphs (BlockLayoutCache::
        // estimateHeight() counts embedded '\n's only, so a single long
        // line that wraps to several visual lines is estimated as ONE
        // line — exactly the under-count this test needs to provoke).
        // Deliberately contains no "the" substring (case-insensitively) —
        // this filler must NOT itself match the "the" needle below, or it
        // would add extra matches and break the three-match assumption
        // this test walks through.
        const QByteArray longLine =
            "This is a long paragraph of filler text meant to wrap across "
            "several visual lines once it gets laid out in a narrow column, "
            "precisely what BlockLayoutCache's estimateHeight function "
            "undercounts, since only embedded newlines get tallied and "
            "nothing about a full wrap simulation.\n\n";

        QByteArray big = "the cat\n\n";
        for (int i = 0; i < 25; ++i)
            big += longLine;
        big += "sat on the mat\n\n";
        for (int i = 0; i < 25; ++i)
            big += longLine;
        big += "the dog\n";
        doc.loadFromMarkdown(big);

        EditorWidget ed;
        ed.resize(260, 150);  // narrow: forces heavy wrapping
        ed.setDocument(&doc);
        ed.show();
        QVERIFY(QTest::qWaitForWindowExposed(&ed));
        QTest::qWait(20);  // let the initial paint settle/realize

        FindController fc(&doc);
        ed.attachFindController(&fc);
        fc.activate();
        fc.setNeedle("the");

        View *view = ed.view();

        // Three matches, confirmed by construction (needle "the" appears
        // nowhere in the filler paragraphs — see longLine's comment):
        // "the cat" (top), "the mat" (middle, buried under 25 wrapped
        // paragraphs), "the dog" (bottom, buried under 50). Walk forward
        // through all of them and assert the matched block is actually
        // inside the viewport every time — not just that the scrollbar
        // moved off zero (that much already passed before this fix; see
        // navigate_places_caret_and_scrolls_without_focus() above).
        for (int step = 0; step < 3; ++step) {
            fc.findNext();

            // Convergence is progressive, not instant: ensureCaretVisible()
            // (fired synchronously off navigationRequested) can only aim at
            // the ESTIMATED y of a match buried under many never-realized
            // wrapped paragraphs (BlockLayoutCache::estimateHeight() counts
            // embedded newlines only, so a long wrapped line is badly
            // under-counted), and each repaint's ensureLayoutForViewport()
            // only realizes a ~3-viewport-tall window per pass ("fixed-point
            // loop, not a one-shot pass"). Closing a large estimate-vs-real
            // gap can take a few repaints. QTRY_VERIFY2 pumps the event loop
            // — each spin lets a real paintEvent run and, per the
            // ensureLayoutForViewport fix, scrollCaretIntoView() re-clamp —
            // until it settles or times out (5s default). Before that fix,
            // nothing ever re-clamped after the first (estimate-based)
            // guess, so this failed to converge even given the full 5s: it
            // doesn't just paper over slow convergence, it distinguishes
            // "converging" from "permanently stuck" (verified by reverting
            // the View.cpp fix locally and re-running this test).
            QTRY_VERIFY2(view->caretRect().isValid() &&
                         view->caretRect().bottom() >= 0 &&
                         view->caretRect().top() <= view->viewport()->height(),
                         "matched block's caret rect never settled inside "
                         "the viewport after find-next — the classic 'F3 "
                         "does nothing' symptom");
        }
    }

    void detach_clears_all_highlights() {
        MarkoffDocument doc(1);
        doc.loadFromMarkdown(fixture());
        const BlockId block0 = doc.iterateBlocks().front();

        EditorWidget ed;
        ed.setDocument(&doc);

        FindController fc(&doc);
        ed.attachFindController(&fc);
        fc.activate();
        fc.setNeedle("the");
        QVERIFY(!ed.view()->findHighlightsForBlock(block0).isEmpty());

        ed.detachFindController();
        QVERIFY(ed.view()->findHighlightsForBlock(block0).isEmpty());

        // A controller left running after detach must not repaint anything.
        fc.findNext();
        QVERIFY(ed.view()->findHighlightsForBlock(block0).isEmpty());
    }
};

QTEST_MAIN(TstCanvasFind)
#include "tst_canvas_find.moc"
