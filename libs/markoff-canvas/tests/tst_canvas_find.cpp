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
