// SPDX-License-Identifier: GPL-3.0-or-later
//
// P7.2f (F1 #8, #10) — 4 small, independent visual additions: scroll-past-
// end, empty-document placeholder, bracket-match highlight, drag drop-cursor
// indicator. CodeMirror references: view/src/scrollpastend.ts,
// view/src/placeholder.ts, language/src/matchbrackets.ts,
// view/src/dropcursor.ts.
//
// Falsification grouping (session protocol): 3 pairs, not 4 — scroll-past-
// end and the drop-cursor indicator each get their own (independent
// mechanisms: updateScrollRange()'s padding math vs. drag-event state
// tracking); bracket-match's "found a pair" and "respects nesting" cases
// share one pair (both live in the same findMatchingBracket() scan, and the
// nesting case is strictly the stronger of the two — a break that passes
// the nesting test would also pass the simple-pair test); the placeholder's
// focus-independence and its "typing clears it" cases share their own pair
// (both are the same isDocumentEmpty() gate, read from opposite sides).

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QMimeData>
#include <QScrollBar>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

using Markoff::BlockId;
using Markoff::Canvas::BracketMatch;
using Markoff::Canvas::CanvasCursor;
using Markoff::Canvas::View;
using Markoff::MarkoffDocument;
using Markoff::Theme;

namespace {

// Exposes the protected drag-event seams for direct testing — same access
// pattern tst_canvas_drag_drop.cpp's TestableView uses.
class TestableView : public View {
public:
    using View::dragEnterEvent;
    using View::dragLeaveEvent;
    using View::dragMoveEvent;
    using View::dropEvent;
};

/// Count of pixels in `img` matching `color` exactly — same helper
/// tst_canvas_remote_presence.cpp uses for its own visual assertions.
int countColorPixels(const QImage &img, QColor color)
{
    const QRgb target = color.rgb();
    int count = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixel(x, y) == target)
                ++count;
    return count;
}

} // namespace

class TstCanvasP72f : public QObject {
    Q_OBJECT

private slots:
    void scroll_past_end_allows_scrolling_beyond_the_last_line();
    void short_document_gets_no_spurious_scroll_range();
    void empty_document_shows_placeholder_regardless_of_focus();
    void typing_clears_the_empty_document_placeholder();
    void bracket_match_resolves_the_pair_adjacent_to_the_caret();
    void bracket_match_respects_nesting_depth();
    void bracket_match_is_absent_with_an_active_selection_or_no_pair();
    void drop_cursor_tracks_drag_move_and_clears_on_leave();
    void drop_cursor_clears_on_drop();
};

void TstCanvasP72f::scroll_past_end_allows_scrolling_beyond_the_last_line()
{
    MarkoffDocument doc;
    QByteArray src;
    for (int i = 0; i < 200; ++i)
        src += "Line " + QByteArray::number(i) + ".\n\n";
    doc.loadFromMarkdown(src);

    View view;
    view.resize(600, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QTest::keyClick(&view, Qt::Key_End, Qt::ControlModifier);
    const int afterCtrlEnd = view.verticalScrollBar()->value();
    const int maximum = view.verticalScrollBar()->maximum();

    // Falsification target: dropping the "+= scroll-past-end padding" term
    // in updateScrollRange() collapses maximum() back down to exactly
    // where Ctrl+End already lands (the old, pre-P7.2f behavior) — this
    // assertion (room to scroll further) is what catches it.
    QVERIFY2(maximum > afterCtrlEnd,
             qPrintable(QStringLiteral("maximum=%1 afterCtrlEnd=%2 — expected room to "
                                       "scroll past the last line")
                            .arg(maximum).arg(afterCtrlEnd)));

    // Scrolling all the way to the new maximum must actually be honored by
    // the scrollbar (not silently clamped back).
    view.verticalScrollBar()->setValue(maximum);
    QCOMPARE(view.verticalScrollBar()->value(), maximum);
}

void TstCanvasP72f::short_document_gets_no_spurious_scroll_range()
{
    // A short document that already fits entirely within a tall viewport
    // must NOT gain a scroll range just because "viewport height minus one
    // line" happens to be a large, positive number in a tall window — see
    // updateScrollRange()'s own doc comment for why this guard exists.
    MarkoffDocument doc;
    doc.loadFromMarkdown("One line.\n");

    View view;
    view.resize(600, 2000);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QCOMPARE(view.verticalScrollBar()->maximum(), 0);
}

void TstCanvasP72f::empty_document_shows_placeholder_regardless_of_focus()
{
    // loadFromMarkdown("")/("\n") produce a zero-block document (tst_d2_load's
    // own precedent, also noted in tst_canvas_auto_pair.cpp) — to exercise
    // the "exactly one empty Paragraph block" shape specifically (as
    // opposed to the zero-block shape, which isDocumentEmpty() also treats
    // as empty but which this test isn't targeting), seed one real
    // character and delete it back out via a real Backspace.
    MarkoffDocument doc;
    doc.loadFromMarkdown("z\n");
    const BlockId block = doc.iterateBlocks().front();

    View view;
    view.resize(400, 200);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    view.setCaretPosition(block, 1);
    QTest::keyClick(&view, Qt::Key_Backspace);
    QCOMPARE(doc.blockText(block), QByteArray());

    const QColor placeholderColor = view.theme().color(Theme::Slot::Quote);

    // Unfocused: placeholder must already be visible.
    view.viewport()->repaint();
    const int unfocusedCount = countColorPixels(view.grab().toImage(), placeholderColor);
    QVERIFY2(unfocusedCount > 0, "placeholder must paint while unfocused");

    // Focused, still empty: CM's own placeholder.ts gates purely on
    // doc.length, no focus check anywhere in that source (verified by
    // reading it) — the placeholder must NOT disappear just because the
    // view gained focus. Falsification target: gating the paint on
    // `!m_hasFocus` in addition to `isDocumentEmpty()` makes this
    // assertion fail once focus is set below.
    view.setFocus();
    view.viewport()->repaint();
    const int focusedCount = countColorPixels(view.grab().toImage(), placeholderColor);
    QVERIFY2(focusedCount > 0, "placeholder must stay visible once focused (CM's actual "
                               "placeholder.ts behavior is focus-independent)");
}

void TstCanvasP72f::typing_clears_the_empty_document_placeholder()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("z\n");
    const BlockId block = doc.iterateBlocks().front();

    View view;
    view.resize(400, 200);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    view.setCaretPosition(block, 1);
    QTest::keyClick(&view, Qt::Key_Backspace);
    QCOMPARE(doc.blockText(block), QByteArray());

    const QColor placeholderColor = view.theme().color(Theme::Slot::Quote);
    view.viewport()->repaint();
    const int beforeCount = countColorPixels(view.grab().toImage(), placeholderColor);
    QVERIFY(beforeCount > 0);

    QTest::keyClick(&view, Qt::Key_A);
    QCOMPARE(doc.blockText(block), QByteArray("a"));

    // Not asserting exactly 0: a stray antialiasing edge pixel of the typed
    // glyph can coincidentally exact-match the placeholder's own grey — the
    // load-bearing signal is that the placeholder's own multi-glyph string
    // ("Start typing…", many dozens of matching pixels) is gone, not a
    // literal zero. Falsification target: dropping `isDocumentEmpty()`'s
    // gate (always painting the placeholder) keeps this near `beforeCount`
    // instead of collapsing toward it.
    view.viewport()->repaint();
    const int afterCount = countColorPixels(view.grab().toImage(), placeholderColor);
    QVERIFY2(afterCount < beforeCount / 4,
             qPrintable(QStringLiteral("before=%1 after=%2 — placeholder did not clear")
                            .arg(beforeCount).arg(afterCount)));
}

void TstCanvasP72f::bracket_match_resolves_the_pair_adjacent_to_the_caret()
{
    // "foo(bar)baz" — '(' at byte 3, ')' at byte 7.
    MarkoffDocument doc;
    doc.loadFromMarkdown("foo(bar)baz\n");
    View view;
    view.resize(400, 200);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();

    // Caret immediately AFTER '(' (byte 4): the before-caret char is '('.
    view.setCaretPosition(block, 4);
    auto m1 = view.bracketMatchAtCaret();
    QVERIFY(m1.has_value());
    QCOMPARE(m1->block, block);
    QCOMPARE(m1->openByte, 3);
    QCOMPARE(m1->closeByte, 7);

    // Caret immediately AFTER ')' (byte 8): the before-caret char is ')' —
    // same pair, resolved by scanning backward this time.
    view.setCaretPosition(block, 8);
    auto m2 = view.bracketMatchAtCaret();
    QVERIFY(m2.has_value());
    QCOMPARE(m2->openByte, 3);
    QCOMPARE(m2->closeByte, 7);
}

void TstCanvasP72f::bracket_match_respects_nesting_depth()
{
    // "(a(b)c)" — caret right after the FIRST '(' (byte 1) must resolve to
    // the OUTER ')' (byte 6), not the inner one (byte 4). This is the
    // load-bearing nesting check: a depth-less "first close wins" scan
    // would wrongly report byte 4.
    MarkoffDocument doc;
    doc.loadFromMarkdown("(a(b)c)\n");
    View view;
    view.resize(400, 200);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    view.setCaretPosition(block, 1);

    auto m = view.bracketMatchAtCaret();
    QVERIFY(m.has_value());
    QCOMPARE(m->openByte, 0);
    QCOMPARE(m->closeByte, 6);
}

void TstCanvasP72f::bracket_match_is_absent_with_an_active_selection_or_no_pair()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("(foo\n");  // unbalanced: never closed
    View view;
    view.resize(400, 200);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    view.setCaretPosition(block, 1);
    QVERIFY(!view.bracketMatchAtCaret().has_value());

    MarkoffDocument doc2;
    doc2.loadFromMarkdown("foo(bar)baz\n");
    View view2;
    view2.resize(400, 200);
    view2.setDocument(&doc2);
    view2.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view2));
    const BlockId block2 = doc2.iterateBlocks().front();
    view2.setCaretPosition(block2, 4);
    QVERIFY(view2.bracketMatchAtCaret().has_value());  // sanity: collapsed caret DOES match

    QTest::keyClick(&view2, Qt::Key_Right, Qt::ShiftModifier);  // now a selection
    QVERIFY(view2.hasSelection());
    QVERIFY(!view2.bracketMatchAtCaret().has_value());
}

void TstCanvasP72f::drop_cursor_tracks_drag_move_and_clears_on_leave()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n\nBeta two.\n");
    TestableView view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const QRectF r0 = view.blockRect(blocks[0]);
    const QRectF r1 = view.blockRect(blocks[1]);

    QVERIFY(!view.dropCursorPosition().has_value());

    QMimeData mime;
    mime.setText(QStringLiteral("X"));

    const QPoint p0(int(r0.x()) + 2, int(r0.y()) + 8);
    QDragEnterEvent enter(p0, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
    view.dragEnterEvent(&enter);
    QVERIFY(enter.isAccepted());
    QVERIFY(view.dropCursorPosition().has_value());
    QCOMPARE(view.dropCursorPosition()->block, blocks[0]);

    // Falsification target: not re-hit-testing on move (leaving the
    // dragEnterEvent-time position stashed) makes this next comparison
    // fail — the indicator would still report block 0.
    const QPoint p1(int(r1.x()) + 2, int(r1.y()) + 8);
    QDragMoveEvent move(p1, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
    view.dragMoveEvent(&move);
    QVERIFY(move.isAccepted());
    QVERIFY(view.dropCursorPosition().has_value());
    QCOMPARE(view.dropCursorPosition()->block, blocks[1]);

    QDragLeaveEvent leave;
    view.dragLeaveEvent(&leave);
    QVERIFY(!view.dropCursorPosition().has_value());
}

void TstCanvasP72f::drop_cursor_clears_on_drop()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha one.\n");
    TestableView view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const QRectF r0 = view.blockRect(blocks[0]);
    const QPoint p0(int(r0.x()) + 2, int(r0.y()) + 8);

    QMimeData mime;
    mime.setText(QStringLiteral("X"));
    QDragEnterEvent enter(p0, Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
    view.dragEnterEvent(&enter);
    QVERIFY(view.dropCursorPosition().has_value());

    QDropEvent drop(QPointF(p0), Qt::CopyAction, &mime, Qt::NoButton, Qt::NoModifier);
    view.dropEvent(&drop);
    QVERIFY(!view.dropCursorPosition().has_value());
}

QTEST_MAIN(TstCanvasP72f)
#include "tst_canvas_p72f.moc"
