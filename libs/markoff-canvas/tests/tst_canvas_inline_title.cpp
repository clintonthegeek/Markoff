// SPDX-License-Identifier: GPL-3.0-or-later
//
// P4.9 — inline title band (spec §5.2, user-directed 2026-08-13).
//
// Falsification target named by the plan: "include the title in the
// flat-line walk; a cursorPosition() round-trip assertion must fail" —
// cursor_position_excludes_title() below is that assertion. It passes today
// because EditorWidget's toCursorPos()/fromCursorPos() (EditorWidget.cpp)
// walk doc->iterateBlocks() only and have no notion of the title at all;
// see the plan's findings log entry for the plant/revert SHAs that proved
// the test actually catches a regression of that exclusion.

#include <QSignalSpy>
#include <QTest>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::Canvas::EditorWidget;
using Markoff::Canvas::View;

class TstCanvasInlineTitle : public QObject {
    Q_OBJECT

private slots:
    void off_by_default();
    void visible_grows_document_height();
    void click_types_and_emits_title_edited();
    void down_and_enter_seam_lands_at_block_zero();
    void backspace_at_document_start_does_not_consume_title();
    void cursor_position_excludes_title();
};

void TstCanvasInlineTitle::off_by_default()
{
    View view;
    QVERIFY(!view.inlineTitleVisible());
    QVERIFY(view.inlineTitle().isEmpty());
}

void TstCanvasInlineTitle::visible_grows_document_height()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const qreal heightBefore = view.documentHeight();
    view.setInlineTitleVisible(true);
    view.setInlineTitle(QStringLiteral("My Note"));
    const qreal heightAfter = view.documentHeight();

    QVERIFY(heightAfter > heightBefore);
}

void TstCanvasInlineTitle::click_types_and_emits_title_edited()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setInlineTitleVisible(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QSignalSpy titleEditedSpy(&view, &View::titleEdited);

    // The document's own caret before the title click — proof that a click
    // inside the band never resolves through hitTest() to a BlockId (the
    // one hit-test path this file has; a miss there is what keeps the
    // title out of selection/copy too).
    const auto blocksBefore = doc.iterateBlocks();
    view.setCaretPosition(blocksBefore[0], 0);
    const auto caretBlockBefore = view.caretBlock();
    const int caretByteBefore = view.caretByteOffset();

    // Any point within the band's vertical extent is a valid click target
    // (whole-band click, like a normal line edit) — top-left corner plus a
    // small margin is safely inside it regardless of exact font metrics.
    const QPoint clickPos(20, 10);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, clickPos);

    QCOMPARE(view.caretBlock(), caretBlockBefore);
    QCOMPARE(view.caretByteOffset(), caretByteBefore);

    QTest::keyClick(&view, Qt::Key_H);
    QTest::keyClick(&view, Qt::Key_I);

    QCOMPARE(view.inlineTitle(), QStringLiteral("hi"));
    QVERIFY(titleEditedSpy.count() >= 2);
    QCOMPARE(titleEditedSpy.last().at(0).toString(), QStringLiteral("hi"));

    // The document itself never saw any of this — the title is not a block.
    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("Alpha."));
}

void TstCanvasInlineTitle::down_and_enter_seam_lands_at_block_zero()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha.\n\nBeta.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setInlineTitleVisible(true);
    view.setInlineTitle(QStringLiteral("Title"));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();

    // Down from the title.
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 10));
    QTest::keyClick(&view, Qt::Key_Down);
    QCOMPARE(view.caretBlock(), blocks[0]);
    QCOMPARE(view.caretByteOffset(), 0);

    // Enter from the title (re-enter title-edit mode first).
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 10));
    QTest::keyClick(&view, Qt::Key_Return);
    QCOMPARE(view.caretBlock(), blocks[0]);
    QCOMPARE(view.caretByteOffset(), 0);
}

void TstCanvasInlineTitle::backspace_at_document_start_does_not_consume_title()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setInlineTitleVisible(true);
    view.setInlineTitle(QStringLiteral("Keep Me"));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    view.setCaretPosition(blocks[0], 0);  // document start
    QVERIFY(!view.isComposing());

    QTest::keyClick(&view, Qt::Key_Backspace);

    // Backspace at document start has nothing to merge into (block 0 has
    // no predecessor) — it must not reach into the title band, which isn't
    // a document block in the first place.
    QCOMPARE(view.inlineTitle(), QStringLiteral("Keep Me"));
    QCOMPARE(doc.iterateBlocks().size(), size_t(1));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("Alpha."));
}

void TstCanvasInlineTitle::cursor_position_excludes_title()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Alpha.\n\nBeta.\n");
    EditorWidget ed;
    ed.setDocument(&doc);
    ed.view()->resize(400, 300);
    ed.view()->setInlineTitleVisible(true);
    ed.view()->setInlineTitle(QStringLiteral("A Long Title That Would Shift Lines If Counted"));

    const auto blocks = doc.iterateBlocks();
    ed.view()->setCaretPosition(blocks[0], 0);

    // The named falsification target: flat-line {1,1} for block 0 byte 0,
    // regardless of the title band's presence/content — NOT {2,1} (which
    // is what a flat-line walk that counted the title as a leading line
    // would report).
    const Markoff::CursorPos pos = ed.cursorPosition();
    QCOMPARE(pos.line, 1);
    QCOMPARE(pos.column, 1);

    // Round trip: {1,1} must resolve back to block 0 byte 0.
    ed.setCursorPosition({1, 1});
    QCOMPARE(ed.view()->caretBlock(), blocks[0]);
    QCOMPARE(ed.view()->caretByteOffset(), 0);
}

QTEST_MAIN(TstCanvasInlineTitle)
#include "tst_canvas_inline_title.moc"
