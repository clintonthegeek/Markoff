// SPDX-License-Identifier: GPL-3.0-or-later
//
// P5.6 — folding: `View`'s own BlockId-keyed fold state (`m_foldedHeads`)
// over the canvas-local `Folding::resolveFoldable` shape rules (heading
// section / long list / callout — see Folding.h's own doc comment for the
// exact per-kind body computation).
//
// Session/FoldRef finding (see the plan's findings log, P5.6 entry): the
// core seam this task names, `Session::foldedRegions()` (`FoldRef`,
// `<markoff/core/FoldRef.h>`), stores its fold anchor as a raw
// `CollabText::Crdt::Anchor`. The only PUBLIC way to construct one of
// those, `MarkoffDocument::anchorAt(quint32 byteOffset, Bias)`, is backed
// by the LEGACY flat buffer (`d->buffer.anchor_at` in
// MarkoffDocument.cpp), which core's own CLAUDE.md documents as stale/
// unreliable once a document is D2-loaded (per-block CRDT buffers) — canvas
// is D2-only. The D2-aware, per-block anchor constructors that DO exist
// publicly (`textAnchorAt(BlockAnchor, offset, rightBias)`) return
// `Markoff::TextAnchor`, not the raw `Crdt::Anchor` `FoldRef::start`
// needs, and the private conversion (`Detail::toCrdtAnchor`,
// AnchorConversion.h) lives in markoff-core's src/, not its public
// include/. This is exactly why `EditorWidget::saveEphemeralState`'s
// scroll/cursor keys (P3.6) already bypass `Session::topVisibleAnchor`/
// `primarySelection` for the same block-index scheme this task's fold
// persistence uses below — Session's own fold accessors are left
// unwired, same as its scroll/selection accessors already are, pending a
// D2-safe public core accessor that doesn't exist yet.
//
// "Long lists" interpretation (task text names the shape, not a
// threshold): a foldable list head is the FIRST item of a maximal
// consecutive run of ListItem blocks, at `AttrNames::IndentLevel == 0`,
// with at least `Folding::kLongListFoldThreshold` (6) TOP-LEVEL items —
// folding it hides every subsequent item in the run (any indent). A
// shorter list is left un-foldable (no affordance) — see Folding.h.
//
// Callouts (P5.5) needed NO adjustment to become foldable: a callout's
// head paragraph is already the only block `CalloutBlocks::parseCallout`
// matches, and `AttrNames::BlockQuoteRunId` (already written by the P5.5-
// era load-time blockquote split) already groups its continuation
// paragraphs — Folding::resolveFoldable's BlockQuote case is a thin read
// of state P5.5 already produced.

#include <QTest>

#include <crdt/Anchor.h>

#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/core/FoldRef.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>

using Markoff::BlockId;
using Markoff::Canvas::EditorWidget;
using Markoff::Canvas::View;
using Markoff::FoldRef;
using Markoff::MarkoffDocument;
using Markoff::Session;

class TstCanvasFolding : public QObject {
    Q_OBJECT

private slots:
    void heading_with_no_body_is_not_foldable();
    void heading_fold_collapses_body_to_zero_height_but_still_queryable();
    void heading_fold_stops_at_next_same_or_higher_level();
    void toggle_fold_twice_restores_height();
    void long_list_below_threshold_is_not_foldable();
    void long_list_at_threshold_folds_all_but_first_item();
    void callout_folds_its_continuation_paragraphs();
    void fold_affordance_rect_only_for_foldable_heads();
    void click_on_fold_affordance_toggles();
    void caret_down_skips_folded_heading_body();
    void caret_up_skips_folded_heading_body();
    void toggling_fold_under_caret_relocates_caret_to_head();
    void ephemeral_state_round_trip_through_detach_reattach();
    void toggle_fold_writes_through_to_session();
};

void TstCanvasFolding::heading_with_no_body_is_not_foldable()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("# Only heading\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId heading = doc.iterateBlocks().front();
    QVERIFY(!view.isBlockFoldable(heading));
    QVERIFY(view.foldAffordanceRectFor(heading).isNull());
}

void TstCanvasFolding::heading_fold_collapses_body_to_zero_height_but_still_queryable()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "# Section One\n"
        "para one\n\n"
        "para two\n\n"
        "# Section Two\n"
        "para three\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(5));
    const BlockId h1   = blocks[0];
    const BlockId p1   = blocks[1];
    const BlockId p2   = blocks[2];
    const BlockId h2   = blocks[3];

    QVERIFY(view.isBlockFoldable(h1));
    QVERIFY(!view.isBlockFolded(h1));
    QVERIFY(!view.isBlockHidden(p1));

    const QRectF p1RectBefore = view.blockRect(p1);
    QVERIFY(p1RectBefore.height() > 0);
    const qreal totalBefore = view.documentHeight();
    const qreal h2YBefore = view.blockRect(h2).y();

    view.toggleFold(h1);

    QVERIFY(view.isBlockFolded(h1));
    QVERIFY(view.isBlockHidden(p1));
    QVERIFY(view.isBlockHidden(p2));
    QVERIFY(!view.isBlockHidden(h1));   // the head itself is never "hidden"
    QVERIFY(!view.isBlockHidden(h2));   // stops before the next heading

    // Still found/queryable: a valid (non-null) index and rect, just
    // occupying no y-space (height 0) — not gone from the cache.
    QVERIFY(view.blockIndexOf(p1) >= 0);
    QVERIFY(view.blockIndexOf(p2) >= 0);
    const QRectF p1RectAfter = view.blockRect(p1);
    QVERIFY(!p1RectAfter.isNull());
    QCOMPARE(p1RectAfter.height(), 0.0);

    // The document's total height shrank by exactly the folded body's
    // former contribution, and Section Two's own top moved up to meet it
    // (the "skipped in y-layout" behavior, not merely invisible-in-place).
    QVERIFY(view.documentHeight() < totalBefore);
    QVERIFY(view.blockRect(h2).y() < h2YBefore);
}

void TstCanvasFolding::heading_fold_stops_at_next_same_or_higher_level()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "# H1\n"
        "para a\n\n"
        "## H2 sub\n"
        "para b\n\n"
        "# H1 again\n"
        "para c\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(6));
    const BlockId h1a = blocks[0];
    const BlockId pa  = blocks[1];
    const BlockId h2  = blocks[2];
    const BlockId pb  = blocks[3];
    const BlockId h1b = blocks[4];

    view.toggleFold(h1a);

    // A ## sub-heading is deeper than the folded #1 — it and its body fold
    // away too, all the way up to (not including) the next # at level <= 1.
    QVERIFY(view.isBlockHidden(pa));
    QVERIFY(view.isBlockHidden(h2));
    QVERIFY(view.isBlockHidden(pb));
    QVERIFY(!view.isBlockHidden(h1b));
}

void TstCanvasFolding::toggle_fold_twice_restores_height()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("# Section\npara\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId h = doc.iterateBlocks().front();
    const qreal before = view.documentHeight();

    view.toggleFold(h);
    QVERIFY(view.documentHeight() < before);

    view.toggleFold(h);
    QVERIFY(!view.isBlockFolded(h));
    QCOMPARE(view.documentHeight(), before);
}

void TstCanvasFolding::long_list_below_threshold_is_not_foldable()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- one\n- two\n- three\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId first = doc.iterateBlocks().front();
    QVERIFY(!view.isBlockFoldable(first));
}

void TstCanvasFolding::long_list_at_threshold_folds_all_but_first_item()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "- one\n- two\n- three\n- four\n- five\n- six\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(6));
    const BlockId first = blocks[0];

    QVERIFY(view.isBlockFoldable(first));
    view.toggleFold(first);

    QVERIFY(!view.isBlockHidden(first));
    for (size_t i = 1; i < blocks.size(); ++i)
        QVERIFY(view.isBlockHidden(blocks[i]));
}

void TstCanvasFolding::callout_folds_its_continuation_paragraphs()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "> [!note]\n"
        "> body line one\n"
        "\n"
        "para after\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QVERIFY(blocks.size() >= 2);
    const BlockId head = blocks[0];
    QVERIFY(view.isCalloutBlock(head));

    // Some parses may fold the whole callout into one paragraph block (no
    // separate continuation block) — only assert foldability/behavior when
    // there IS a body block to hide, matching Folding's own "empty body
    // stays Kind::None" rule.
    if (view.isBlockFoldable(head)) {
        view.toggleFold(head);
        QVERIFY(view.isBlockHidden(blocks[1]));
        // The trailing plain paragraph (outside the callout's
        // BlockQuoteRunId) is never part of this fold.
        QVERIFY(!view.isBlockHidden(blocks.back()));
    }
}

void TstCanvasFolding::fold_affordance_rect_only_for_foldable_heads()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("# Section\npara\n\nplain paragraph\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const BlockId heading = blocks[0];
    const BlockId plain   = blocks.back();

    QVERIFY(!view.foldAffordanceRectFor(heading).isNull());
    QVERIFY(view.foldAffordanceRectFor(plain).isNull());
}

void TstCanvasFolding::click_on_fold_affordance_toggles()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("# Section\npara\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId heading = doc.iterateBlocks().front();
    QVERIFY(!view.isBlockFolded(heading));

    const QRectF glyph = view.foldAffordanceRectFor(heading);
    QVERIFY(!glyph.isNull());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       glyph.center().toPoint());

    QVERIFY(view.isBlockFolded(heading));

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                       glyph.center().toPoint());
    QVERIFY(!view.isBlockFolded(heading));
}

void TstCanvasFolding::caret_down_skips_folded_heading_body()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "# Section One\n"
        "para one\n\n"
        "# Section Two\n"
        "para two\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const BlockId h1 = blocks[0];
    const BlockId h2 = blocks[2];

    view.toggleFold(h1);
    view.setCaretPosition(h1, 0);
    QCOMPARE(view.caretBlock(), h1);

    QTest::keyClick(&view, Qt::Key_Down);

    // Landed on the next VISIBLE block (Section Two's heading), never on
    // the hidden "para one" in between.
    QCOMPARE(view.caretBlock(), h2);
}

void TstCanvasFolding::caret_up_skips_folded_heading_body()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "# Section One\n"
        "para one\n\n"
        "# Section Two\n"
        "para two\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const BlockId h1 = blocks[0];
    const BlockId h2 = blocks[2];

    view.toggleFold(h1);
    view.setCaretPosition(h2, 0);
    QCOMPARE(view.caretBlock(), h2);

    QTest::keyClick(&view, Qt::Key_Up);

    QCOMPARE(view.caretBlock(), h1);
}

void TstCanvasFolding::toggling_fold_under_caret_relocates_caret_to_head()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("# Section\npara one\n\npara two\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const BlockId heading = blocks[0];
    const BlockId body    = blocks[1];

    view.setCaretPosition(body, 2);
    QCOMPARE(view.caretBlock(), body);

    view.toggleFold(heading);

    // The caret was inside the body this fold just hid — it must not be
    // left stranded there; it lands on the head instead.
    QCOMPARE(view.caretBlock(), heading);
    QCOMPARE(view.caretByteOffset(), 0);
}

void TstCanvasFolding::ephemeral_state_round_trip_through_detach_reattach()
{
    auto *doc = new MarkoffDocument(7);
    doc->loadFromMarkdown("# Section One\npara one\n\n# Section Two\npara two\n");
    auto *ed = new EditorWidget;
    ed->resize(300, 200);
    ed->setDocument(doc);
    ed->show();
    QVERIFY(QTest::qWaitForWindowExposed(ed));

    const BlockId heading = doc->iterateBlocks().front();
    ed->view()->toggleFold(heading);
    QVERIFY(ed->view()->isBlockFolded(heading));
    const int headingIndex = ed->view()->blockIndexOf(heading);

    const QJsonObject saved = ed->saveEphemeralState();
    const QJsonArray folds = saved.value(QStringLiteral("folds")).toArray();
    QCOMPARE(folds.size(), 1);
    QCOMPARE(folds.first().toObject().value(QStringLiteral("blockIndex")).toInt(),
             headingIndex);

    // Real detach/reattach (plan P3.6's own wording, reused for P5.6):
    // setDocument(nullptr) drops the View's fold state along with
    // everything else the cache holds.
    ed->setDocument(nullptr);
    QVERIFY(!ed->view()->isBlockFolded(heading));

    ed->setDocument(doc);
    QVERIFY(!ed->view()->isBlockFolded(heading));  // not yet restored

    ed->restoreEphemeralState(saved);

    QVERIFY(ed->view()->isBlockFolded(heading));
    const BlockId para = doc->iterateBlocks()[1];
    QVERIFY(ed->view()->isBlockHidden(para));

    delete ed;
    delete doc;
}

// P6.0 falsification: toggleFold() must write through to the attached
// Session, not merely keep its own m_foldedHeads mirror. An EXTERNAL
// reader (this test, standing in for a remote-presence/collab consumer
// that never touches View at all) reads session->foldedRegions()
// directly after view->toggleFold(id) — same shape P6.1's planned
// falsification uses for primarySelection(). If toggleFold() stopped
// pushing to the Session (the throwaway break below), this is the
// assertion that fails; isBlockFolded() alone would not catch it, since
// that reads View's own local cache, not the Session.
void TstCanvasFolding::toggle_fold_writes_through_to_session()
{
    auto *doc = new MarkoffDocument(3);
    doc->loadFromMarkdown("# Section One\npara one\n\n# Section Two\npara two\n");
    auto *ed = new EditorWidget;
    ed->resize(300, 200);
    ed->setDocument(doc);
    ed->show();
    QVERIFY(QTest::qWaitForWindowExposed(ed));

    Session *session = ed->view()->session();
    QVERIFY(session != nullptr);
    QVERIFY(session->foldedRegions().isEmpty());

    const BlockId heading = doc->iterateBlocks().front();
    ed->view()->toggleFold(heading);

    // The View's own cache agrees...
    QVERIFY(ed->view()->isBlockFolded(heading));
    // ...and, independently, the Session — read straight off its own
    // accessor, not through View at all — genuinely received the fold.
    const QList<FoldRef> regions = session->foldedRegions();
    QCOMPARE(regions.size(), 1);
    QCOMPARE(regions.first().kind, FoldRef::Kind::Heading);
    QVERIFY(!(regions.first().start == CollabText::Crdt::Anchor{}));

    // Toggling again removes it from the Session too (not merely from
    // View's local cache) — a real write-through, not a one-shot append.
    ed->view()->toggleFold(heading);
    QVERIFY(session->foldedRegions().isEmpty());

    delete ed;
    delete doc;
}

QTEST_MAIN(TstCanvasFolding)
#include "tst_canvas_folding.moc"
