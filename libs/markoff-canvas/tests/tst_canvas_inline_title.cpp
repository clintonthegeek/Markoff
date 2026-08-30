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

#include <QAccessible>
#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QTest>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/FindController.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::Canvas::EditorWidget;
using Markoff::Canvas::View;
using Markoff::FindController;

class TstCanvasInlineTitle : public QObject {
    Q_OBJECT

private slots:
    void off_by_default();
    void visible_grows_document_height();
    void click_types_and_emits_title_edited();
    void down_and_enter_seam_lands_at_block_zero();
    void backspace_at_document_start_does_not_consume_title();
    void cursor_position_excludes_title();

    // H arc — hide a matching first heading as the document title.
    void hide_flag_off_by_default();
    void atx_match_hides_zero_height_keeps_index_space();
    void setext_match_hides();
    void no_hide_on_mismatch();
    void no_hide_for_level_two_heading();
    void no_hide_when_title_empty();
    void no_hide_for_sole_block();
    void hide_toggles_with_title_and_heading_edits();
    void caret_unstrands_when_block_zero_becomes_title();
    void hidden_title_not_foldable();
    void a11y_tree_reports_hidden_title_invisible();
    void find_navigation_lands_on_first_visible();
    void ctrl_home_skips_hidden_title();
    void select_all_excludes_hidden_title();
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

// ---- H arc: hide a matching first heading as the document title ------------

void TstCanvasInlineTitle::hide_flag_off_by_default()
{
    View view;
    QVERIFY(!view.hideMatchingFirstHeadingAsTitle());
    EditorWidget ed;
    QVERIFY(!ed.hideMatchingFirstHeadingAsTitle());
}

void TstCanvasInlineTitle::atx_match_hides_zero_height_keeps_index_space()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));

    view.setInlineTitle(QStringLiteral("notes"));
    const qreal heightVisible = view.documentHeight();
    QCOMPARE(view.blockRect(blocks[0]).height() > 0, true);

    view.setHideMatchingFirstHeadingAsTitle(true);

    // Hidden: zero-height rect, absent from the y-layout, isBlockHidden.
    QVERIFY(view.hideMatchingFirstHeadingAsTitle());
    QVERIFY(view.isBlockHidden(blocks[0]));
    QCOMPARE(view.blockRect(blocks[0]).height(), 0);
    QVERIFY(view.documentHeight() < heightVisible);

    // D2: stays in the block-index space — document order intact.
    QCOMPARE(view.blockCount(), 2);
    QCOMPARE(view.blockIndexOf(blocks[0]), 0);
    QCOMPARE(view.blockIndexOf(blocks[1]), 1);
    QCOMPARE(view.blockIdAt(0), blocks[0]);
    QCOMPARE(view.blockIdAt(1), blocks[1]);

    // The document itself is untouched — serialize round-trip is intact.
    const QByteArray roundTrip = doc.serializeForSave();
    QVERIFY(roundTrip.contains("# notes"));
    QVERIFY(roundTrip.contains("Body."));

    // Flag off re-shows the block.
    view.setHideMatchingFirstHeadingAsTitle(false);
    QVERIFY(!view.isBlockHidden(blocks[0]));
    QCOMPARE(view.blockRect(blocks[0]).height() > 0, true);
}

void TstCanvasInlineTitle::setext_match_hides()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("notes\n====\n\nBody.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(doc.blockKind(blocks[0]), Markoff::BlockKind::Heading);

    view.setInlineTitle(QStringLiteral("notes"));
    view.setHideMatchingFirstHeadingAsTitle(true);

    // D3 setext rule: the buffer is content-only, used as-is.
    QVERIFY(view.isBlockHidden(blocks[0]));
    QCOMPARE(view.blockRect(blocks[0]).height(), 0);
}

void TstCanvasInlineTitle::no_hide_on_mismatch()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody.\n");
    View view;
    view.setDocument(&doc);
    const auto blocks = doc.iterateBlocks();

    view.setInlineTitle(QStringLiteral("other"));
    view.setHideMatchingFirstHeadingAsTitle(true);

    QVERIFY(!view.isBlockHidden(blocks[0]));
}

void TstCanvasInlineTitle::no_hide_for_level_two_heading()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("## notes\n\nBody.\n");
    View view;
    view.setDocument(&doc);
    const auto blocks = doc.iterateBlocks();

    view.setInlineTitle(QStringLiteral("notes"));
    view.setHideMatchingFirstHeadingAsTitle(true);

    // D3 level gate: only a level-1 heading is "the title".
    QVERIFY(!view.isBlockHidden(blocks[0]));
}

void TstCanvasInlineTitle::no_hide_when_title_empty()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody.\n");
    View view;
    view.setDocument(&doc);
    const auto blocks = doc.iterateBlocks();

    view.setInlineTitle(QString());
    view.setHideMatchingFirstHeadingAsTitle(true);

    QVERIFY(!view.isBlockHidden(blocks[0]));
}

void TstCanvasInlineTitle::no_hide_for_sole_block()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n");
    View view;
    view.setDocument(&doc);
    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));

    view.setInlineTitle(QStringLiteral("notes"));
    view.setHideMatchingFirstHeadingAsTitle(true);

    // D4: never hide the document's only block — a fully-invisible
    // document would have no caret target at all.
    QVERIFY(!view.isBlockHidden(blocks[0]));
}

void TstCanvasInlineTitle::hide_toggles_with_title_and_heading_edits()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody.\n");
    View view;
    view.setDocument(&doc);
    const auto blocks = doc.iterateBlocks();
    view.setHideMatchingFirstHeadingAsTitle(true);

    // Title change (consumer rename path): match -> hide, mismatch -> show.
    view.setInlineTitle(QStringLiteral("notes"));
    QVERIFY(view.isBlockHidden(blocks[0]));
    view.setInlineTitle(QStringLiteral("other"));
    QVERIFY(!view.isBlockHidden(blocks[0]));
    view.setInlineTitle(QStringLiteral("notes"));
    QVERIFY(view.isBlockHidden(blocks[0]));

    // Document edit (heading typed to no longer match): onDocumentChanged
    // re-derives — "# notes" -> "# Other".
    doc.applyBlockEdit(Markoff::BlockEdit{blocks[0], 0, 7, "# Other"});
    QCoreApplication::processEvents();  // core defers d2DocumentChanged one spin
    QVERIFY(!view.isBlockHidden(blocks[0]));
    QCOMPARE(view.blockIndexOf(blocks[0]), 0);

    // ... and back.
    doc.applyBlockEdit(Markoff::BlockEdit{blocks[0], 0, 8, "# notes"});
    QCoreApplication::processEvents();
    QVERIFY(view.isBlockHidden(blocks[0]));
}

void TstCanvasInlineTitle::caret_unstrands_when_block_zero_becomes_title()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    // setDocument leaves the caret clamped to block 0.
    QCOMPARE(view.caretBlock(), blocks[0]);

    view.setInlineTitle(QStringLiteral("notes"));
    // The caret is still in block 0, which this call newly hides — it must
    // be un-stranded onto the first visible block, never left invisible.
    view.setHideMatchingFirstHeadingAsTitle(true);
    QCOMPARE(view.caretBlock(), blocks[1]);
    QCOMPARE(view.caretByteOffset(), 0);
}

void TstCanvasInlineTitle::hidden_title_not_foldable()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody.\n");
    View view;
    view.setDocument(&doc);
    const auto blocks = doc.iterateBlocks();

    view.setInlineTitle(QStringLiteral("notes"));
    view.setHideMatchingFirstHeadingAsTitle(true);

    // H1.3: the hidden title is never foldable — toggleFold no-ops and no
    // fold arrow can appear.
    QVERIFY(!view.isBlockFoldable(blocks[0]));
    view.toggleFold(blocks[0]);
    QVERIFY(!view.isBlockFolded(blocks[0]));

    // Un-hiding restores the heading's ordinary foldability (a heading
    // with body below it IS foldable).
    view.setHideMatchingFirstHeadingAsTitle(false);
    QVERIFY(view.isBlockFoldable(blocks[0]));
}

void TstCanvasInlineTitle::a11y_tree_reports_hidden_title_invisible()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody.\n");
    View view;
    view.setDocument(&doc);
    view.setInlineTitle(QStringLiteral("notes"));
    view.setHideMatchingFirstHeadingAsTitle(true);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);

    // D2: the title stays in the tree as an `invisible` child — childCount
    // still equals blockCount, and child indices never shift.
    QCOMPARE(iface->childCount(), view.blockCount());
    QCOMPARE(iface->childCount(), 2);
    QAccessibleInterface *child0 = iface->child(0);
    QVERIFY(child0);
    QVERIFY(child0->state().invisible);
    QAccessibleInterface *child1 = iface->child(1);
    QVERIFY(child1);
    QVERIFY(!child1->state().invisible);

    // Index identity is stable across a hide/unhide toggle.
    view.setHideMatchingFirstHeadingAsTitle(false);
    QCOMPARE(iface->indexOfChild(child0), 0);
    QVERIFY(!iface->child(0)->state().invisible);
    view.setHideMatchingFirstHeadingAsTitle(true);
    QCOMPARE(iface->indexOfChild(child0), 0);
    QVERIFY(iface->child(0)->state().invisible);
}

void TstCanvasInlineTitle::find_navigation_lands_on_first_visible()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody notes.\n");
    EditorWidget ed;
    ed.setDocument(&doc);
    ed.view()->resize(400, 300);
    ed.view()->setInlineTitle(QStringLiteral("notes"));
    ed.view()->setHideMatchingFirstHeadingAsTitle(true);

    const auto blocks = doc.iterateBlocks();

    FindController fc(&doc);
    ed.attachFindController(&fc);
    fc.activate();
    fc.setNeedle("notes");

    // Two matches: block0 (the hidden title) and block1 ("Body notes.").
    fc.findNext();  // current 0 -> 1 (block1): lands normally
    QCOMPARE(ed.view()->caretBlock(), blocks[1]);
    fc.findNext();  // current 1 -> 0 (the hidden title): must NOT strand
    QCOMPARE(ed.view()->caretBlock(), blocks[1]);
    QVERIFY(ed.view()->caretByteOffset() >= 0);
}

void TstCanvasInlineTitle::ctrl_home_skips_hidden_title()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setInlineTitle(QStringLiteral("notes"));
    view.setHideMatchingFirstHeadingAsTitle(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    view.setCaretPosition(blocks[1], 5);

    QTest::keyClick(&view, Qt::Key_Home, Qt::ControlModifier);
    QCOMPARE(view.caretBlock(), blocks[1]);
    QCOMPARE(view.caretByteOffset(), 0);
}

void TstCanvasInlineTitle::select_all_excludes_hidden_title()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown("# notes\n\nBody text.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.setInlineTitle(QStringLiteral("notes"));
    view.setHideMatchingFirstHeadingAsTitle(true);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    view.setCaretPosition(blocks[1], 0);
    view.selectAll();

    // Anchor at the first VISIBLE block, not the hidden title.
    QCOMPARE(view.selectionAnchorBlock(), blocks[1]);
    QCOMPARE(view.selectionAnchorByteOffset(), 0);
    QCOMPARE(view.caretBlock(), blocks[1]);

    // And the copied text carries no title text.
    view.copy();
    const QString clip = QApplication::clipboard()->text();
    QVERIFY(!clip.contains("notes"));
    QVERIFY(clip.contains("Body text."));
}

QTEST_MAIN(TstCanvasInlineTitle)
#include "tst_canvas_inline_title.moc"
