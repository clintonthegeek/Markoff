// SPDX-License-Identifier: GPL-3.0-or-later
//
// [cluster-k] P3 — double-click word-select and triple-click paragraph-
// select. Neither gesture existed before this task: View had no
// mouseDoubleClickEvent override at all, and Qt has no native triple-click
// event (its own double-click state machine resets after firing one, so a
// third physical click arrives back at mousePressEvent as an ordinary
// press — see View.h's `m_pendingTripleClickBlock` doc comment for how
// mousePressEvent turns that press into a paragraph-select when it follows
// a double-click closely enough).
//
// Pixel positions use the same "computed from the production projection
// map + font, not a guessed offset" technique tst_canvas_links.cpp's
// pointForFullQChar uses — word-select is exactly the kind of test an
// off-by-one in the boundary math could silently pass with an imprecise
// click, so precision matters here too.

#include <QApplication>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

#include "../src/BlockPresentation.h"
#include "../src/InlineFormatting.h"
#include "../src/ProjectionMap.h"

using Markoff::BlockId;
using Markoff::Canvas::ProjectionMap;
using Markoff::Canvas::View;
using Markoff::MarkoffDocument;

namespace {

// Same helper as tst_canvas_links.cpp's pointForFullQChar (duplicated
// rather than shared — each test binary is a standalone executable per
// this suite's own CMakeLists.txt convention, no shared test-support lib).
QPoint pointForFullQChar(MarkoffDocument &doc, const View &view, BlockId block, int fullQChar)
{
    const QByteArray text = doc.blockText(block);
    const auto spans = doc.inlineSpansFor(block);
    const auto omitted = Markoff::Canvas::Detail::omittedDelimiterRanges(spans, {});
    const ProjectionMap proj = ProjectionMap::build(text, omitted);
    const int layoutQChar = proj.fullQCharToLayoutQChar(fullQChar);

    const Markoff::Theme theme = Markoff::Theme::defaultLight();
    const auto style = Markoff::Canvas::presentationFor(doc, block, theme);

    QTextLayout layout(proj.layoutText());
    layout.setFont(style.font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    line.setLineWidth(100000);
    layout.endLayout();

    const qreal x = line.cursorToX(layoutQChar);
    const QRectF rect = view.blockRect(block);
    return QPoint(int(rect.x() + x) + 1, int(rect.y()) + 8);
}

}  // namespace

class TstCanvasClickSelect : public QObject {
    Q_OBJECT

private slots:
    void double_click_selects_word_under_cursor();
    void double_click_on_whitespace_just_places_caret();
    void triple_click_selects_whole_block();
    void triple_click_too_slow_does_not_select_block();
    void triple_click_on_different_block_does_not_select();
};

// "hello world" — double-click the middle of "world" (full-QChar 8, 'r')
// and expect the selection to snap to [6, 11) ("world").
void TstCanvasClickSelect::double_click_selects_word_under_cursor()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("hello world\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QPoint p = pointForFullQChar(doc, view, block, 8);

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);

    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorBlock(), block);
    QCOMPARE(view.caretBlock(), block);
    const int lo = qMin(view.selectionAnchorByteOffset(), view.caretByteOffset());
    const int hi = qMax(view.selectionAnchorByteOffset(), view.caretByteOffset());
    QCOMPARE(doc.blockText(block).mid(lo, hi - lo), QByteArray("world"));
}

// Double-clicking a whitespace run selects nothing — same "click on
// nothing selects nothing" behavior every other Qt text widget gives.
void TstCanvasClickSelect::double_click_on_whitespace_just_places_caret()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("hello   world\n");  // 3 spaces between words
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QPoint p = pointForFullQChar(doc, view, block, 6);  // the middle space

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);

    QVERIFY(!view.hasSelection());
    QCOMPARE(view.caretBlock(), block);
}

// Third click within doubleClickInterval() on the SAME block selects the
// whole block's text (paragraph-select).
void TstCanvasClickSelect::triple_click_selects_whole_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("hello world, this is a paragraph.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QPoint p = pointForFullQChar(doc, view, block, 8);

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);
    QVERIFY(view.hasSelection());

    // The third physical click: an ordinary press, delivered directly
    // (QTest::mouseDClick already sent press+release+doubleclick+release
    // for the first two; this is the ordinary press mousePressEvent must
    // recognize as "close enough" to the double-click just handled).
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);

    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorBlock(), block);
    QCOMPARE(view.selectionAnchorByteOffset(), 0);
    QCOMPARE(view.caretBlock(), block);
    QCOMPARE(view.caretByteOffset(), int(doc.blockText(block).size()));
}

// A third click that arrives too late (past QApplication::doubleClickInterval())
// is just an ordinary click — no paragraph-select, and the detector doesn't
// leak into a LATER unrelated triple-click.
void TstCanvasClickSelect::triple_click_too_slow_does_not_select_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("hello world, this is a paragraph.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QPoint p = pointForFullQChar(doc, view, block, 8);

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);
    QVERIFY(view.hasSelection());

    QTest::qWait(QApplication::doubleClickInterval() + 150);

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, p);

    // An ordinary click on a word collapses the prior word-selection to a
    // caret placement — NOT a whole-block selection.
    const bool wholeBlockSelected =
        view.hasSelection() && view.selectionAnchorByteOffset() == 0
        && view.caretByteOffset() == int(doc.blockText(block).size());
    QVERIFY(!wholeBlockSelected);
}

// A double-click on block A followed promptly by a click on block B must
// not select block B's ENTIRE text as though it were a triple-click — the
// detector is scoped to the SAME block the double-click landed on.
void TstCanvasClickSelect::triple_click_on_different_block_does_not_select()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("first block here\n\nsecond block here\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2);
    const BlockId blockA = blocks.front();
    const BlockId blockB = blocks.back();

    const QPoint pA = pointForFullQChar(doc, view, blockA, 2);
    const QPoint pB = pointForFullQChar(doc, view, blockB, 2);

    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pA);
    QVERIFY(view.hasSelection());

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pB);

    const bool wholeBlockBSelected =
        view.hasSelection() && view.selectionAnchorBlock() == blockB
        && view.selectionAnchorByteOffset() == 0
        && view.caretByteOffset() == int(doc.blockText(blockB).size());
    QVERIFY(!wholeBlockBSelected);
}

QTEST_MAIN(TstCanvasClickSelect)
#include "tst_canvas_click_select.moc"
