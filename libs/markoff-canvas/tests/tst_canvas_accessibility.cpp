// SPDX-License-Identifier: GPL-3.0-or-later
//
// G1 a11y arc — Phase A1 (tree, roles, registration).
//
// A1.1: CanvasAccessible container + factory registration.
// A1.2: CanvasBlockAccessible role/state mapping (spec §4.2/§4.3), one test
// per BlockKind row, plus the heading-level attribute (A1.0's confirmed
// Attribute::Level mechanism).
//
// All in-process via QAccessible::queryAccessibleInterface (spec §7) — no
// AT-SPI bridge, no display, no --direct.

#include <QAccessible>
#include <QCoreApplication>
#include <QTest>

#include "A11yEventSpy.h"

#include <markoff/canvas/View.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/UndoLog.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::MarkoffDocument;
using Markoff::Canvas::View;

namespace {

QByteArray threeParagraphFixture()
{
    return "First paragraph.\n\nSecond paragraph.\n\nThird paragraph.\n";
}

/// Attaches `view` to `doc` and brings it up offscreen — the realization
/// (and, for Image, the load-time caret-block kind promotion) some of the
/// A1.2 role rows depend on needs the window actually exposed, same
/// convention every other realization-dependent canvas test uses (e.g.
/// tst_canvas_side_content.cpp, tst_canvas_media_seams.cpp).
void attachAndExpose(View &view, MarkoffDocument &doc)
{
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
}

}  // namespace

class TstCanvasAccessibility : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void container_role_is_document();
    void child_count_tracks_block_count();
    void child_indices_round_trip();
    void child_count_tracks_document_reload();

    // ---- A1.3: container Name resolution (spec §9 Q2) ----
    void name_falls_back_to_generic_when_unset();
    void name_falls_back_to_inline_title_when_set();
    void name_prefers_accessible_document_name_over_inline_title();

    // ---- A1.2: role mapping (spec §4.2), one case per BlockKind row ----
    void role_paragraph();
    void role_heading_has_level_attribute();
    void role_codeblock();
    void role_listitem_plain();
    void role_listitem_task_checkable_and_checked_states();
    void role_blockquote_is_section_limitation();
    void role_horizontalrule();
    void role_image();
    void role_math_limitation();
    void role_mermaid();
    void role_htmlblock();
    void role_table();
    void role_footnote_def_paragraph_is_section();

    // ---- A1.2: state mapping (spec §4.2/§4.3) ----
    void state_focusable_and_focused_tracks_caret();
    void state_editable_tracks_read_only();
    void state_invisible_for_folded_hidden_block();

    // ---- A2.1: QAccessibleTextInterface core ----
    void text_interface_absent_for_no_text_kinds();
    void text_interface_present_for_text_kinds();
    void text_and_character_count_ascii();
    void text_and_character_count_multibyte_utf8();
    void char_boundary_at_offset();
    void word_boundary_at_offset();
    void paragraph_boundary_is_whole_block();
    void paragraph_boundary_whole_block_with_embedded_newlines_in_codeblock();
    void paragraph_before_after_boundary_report_no_item();

    // ---- A2.2: caret + selection ----
    void cursor_position_only_on_caret_block();
    void set_cursor_position_routes_to_view_caret();
    void selection_within_single_block();
    void selection_partial_range_within_block();
    void selection_spans_multiple_blocks();
    void no_selection_reports_empty();

    // ---- A2.3: geometry and line boundaries ----
    void character_rect_offset_at_point_round_trip();
    void character_rect_unrealized_block_realizes_just_that_one();
    void character_rect_no_text_kind_returns_null();
    void offset_at_point_outside_block_returns_negative_one();
    void line_boundary_reports_wrapped_visual_line();

    // ---- A3.1: event spy harness self-tests ----
    void spy_captures_hand_fired_events_with_payload();
    void spy_restores_previous_handler_and_active_state();
    void spy_second_case_sees_no_stale_events();
    void spy_nested_restores_in_lifo_order();

    // ---- A3.2: caret / selection / focus events (spec §4.4) ----
    void events_caret_move_within_block();
    void events_caret_move_across_blocks();
    void events_no_change_no_event();
    void events_selection_extend_and_collapse_single_block();
    void events_selection_cross_block_and_shrink();
    void events_focus_in_out();
    void events_inactive_emits_nothing();

    // ---- A4.1: folding state + expand/collapse action ----
    void fold_head_state_and_toggle_event();
    void fold_body_invisible_events_and_stable_children();
    void fold_action_toggles_and_readonly_does_not_block();
    void fold_non_foldable_has_no_action();
    void fold_restore_via_set_folded_head_indices();
    void fold_caret_moved_out_of_body_emits_caret_events();
    void fold_hidden_title_invisible_never_expandable();
    void fold_edit_changing_foldability_announces_expandable();

    // ---- A4.2: QAccessibleEditableTextInterface ----
    void editable_insert_mid_block();
    void editable_delete_range();
    void editable_replace_multibyte_offsets();
    void editable_read_only_rejects_all_three();
    void editable_one_undo_step_each();
    void editable_events_fire_once_with_payload();
    void editable_invalid_ranges_rejected();
    void editable_newline_rejected_outside_code_block();
    void editable_interface_absent_for_no_text_kinds();

    // ---- A3.3: text insert/remove, block create/destroy, eviction ----
    void text_typing_emits_insert();
    void text_backspace_and_delete_emit_remove();
    void text_replace_selection_emits_remove_then_insert();
    void text_document_replace_edit_emits_remove_then_insert();
    void text_astral_char_keeps_surrogate_pair_whole();
    void text_undo_emits_remove();
    void structure_split_emits_created_and_head_remove();
    void structure_merge_emits_destroyed_and_insert_and_evicts();
    void structure_remote_edit_insert_remove_create_destroy();
    void eviction_no_dangling_interface_after_removal();
    void eviction_runs_while_inactive_and_emits_nothing();
    void eviction_document_swap_releases_every_block();
    void eviction_view_destruction_after_churn_is_clean();
};

void TstCanvasAccessibility::container_role_is_document()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    QCOMPARE(iface->role(), QAccessible::Document);
}

void TstCanvasAccessibility::child_count_tracks_block_count()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    QCOMPARE(iface->childCount(), view.blockCount());
    QCOMPARE(iface->childCount(), 3);
}

void TstCanvasAccessibility::child_indices_round_trip()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    for (int i = 0; i < iface->childCount(); ++i) {
        QAccessibleInterface *child = iface->child(i);
        QVERIFY(child);
        QCOMPARE(iface->indexOfChild(child), i);
        QCOMPARE(child->parent(), iface);
    }
    QVERIFY(!iface->child(-1));
    QVERIFY(!iface->child(iface->childCount()));
}

void TstCanvasAccessibility::child_count_tracks_document_reload()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    QCOMPARE(iface->childCount(), 3);

    // loadFromMarkdown() emits documentLoaded()/documentChanged() synchronously
    // but defers d2DocumentChanged() one event-loop spin (core's own doc
    // comment on the signal); View::onDocumentChanged() — which rebuilds the
    // block-index cache childCount() reads — is wired to d2DocumentChanged(),
    // not documentChanged(), so a reload past the first (which View primes by
    // hand in setDocument()) needs a spin before the container's childCount()
    // reflects it.
    doc.loadFromMarkdown("Just one paragraph.\n");
    QCoreApplication::processEvents();
    QCOMPARE(iface->childCount(), 1);

    doc.loadFromMarkdown("A\n\nB\n\nC\n\nD\n\nE\n");
    QCoreApplication::processEvents();
    QCOMPARE(iface->childCount(), 5);
}

// ---- A1.3: container Name resolution (spec §9 Q2) ------------------------

void TstCanvasAccessibility::name_falls_back_to_generic_when_unset()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);

    QVERIFY(view.accessibleDocumentName().isEmpty());
    QVERIFY(view.inlineTitle().isEmpty());

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("Markdown document"));
}

void TstCanvasAccessibility::name_falls_back_to_inline_title_when_set()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);
    view.setInlineTitle(QStringLiteral("My Note"));

    QVERIFY(view.accessibleDocumentName().isEmpty());

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("My Note"));
}

void TstCanvasAccessibility::name_prefers_accessible_document_name_over_inline_title()
{
    Markoff::MarkoffDocument doc(1);
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    view.setDocument(&doc);
    view.setInlineTitle(QStringLiteral("My Note"));
    view.setAccessibleDocumentName(QStringLiteral("project-plan.md"));

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface);
    QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("project-plan.md"));
    QCOMPARE(view.accessibleDocumentName(), QStringLiteral("project-plan.md"));

    // Clearing it back to empty falls through to inline title again.
    view.setAccessibleDocumentName(QString());
    QCOMPARE(iface->text(QAccessible::Name), QStringLiteral("My Note"));
}

// ---- A1.2: role mapping -------------------------------------------------

void TstCanvasAccessibility::role_paragraph()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Just a paragraph.\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::Paragraph);
    QCOMPARE(iface->child(0)->role(), QAccessible::Paragraph);
}

void TstCanvasAccessibility::role_heading_has_level_attribute()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("## A level-2 heading\n\nBody.\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *heading = iface->child(0);
    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::Heading);
    QCOMPARE(heading->role(), QAccessible::Heading);

    // A1.0's confirmed mechanism: Attribute::Level reaches AT-SPI, so it is
    // the sole level-exposure path — no description-text fallback.
    QAccessibleAttributesInterface *attrs = heading->attributesInterface();
    QVERIFY(attrs);
    QCOMPARE(attrs->attributeKeys(), QList<QAccessible::Attribute>{QAccessible::Attribute::Level});
    QCOMPARE(attrs->attributeValue(QAccessible::Attribute::Level).toInt(), 2);

    // A non-heading block reports no attribute keys at all.
    QAccessibleInterface *body = iface->child(1);
    QCOMPARE(body->role(), QAccessible::Paragraph);
    QAccessibleAttributesInterface *bodyAttrs = body->attributesInterface();
    QVERIFY(bodyAttrs);
    QVERIFY(bodyAttrs->attributeKeys().isEmpty());
}

void TstCanvasAccessibility::role_codeblock()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("```python\ndef foo():\n    return 1\n```\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::CodeBlock);
    // No dedicated "code" role in Qt — EditableText (→ ROLE_TEXT) per §4.2.
    QCOMPARE(iface->child(0)->role(), QAccessible::EditableText);
}

void TstCanvasAccessibility::role_listitem_plain()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- an item\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::ListItem);
    QAccessibleInterface *item = iface->child(0);
    QCOMPARE(item->role(), QAccessible::ListItem);
    // Not a task item: not checkable.
    QVERIFY(!item->state().checkable);
}

void TstCanvasAccessibility::role_listitem_task_checkable_and_checked_states()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("- [ ] todo\n- [x] done\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *unchecked = iface->child(0);
    QAccessibleInterface *checked = iface->child(1);

    QCOMPARE(unchecked->role(), QAccessible::ListItem);
    QVERIFY(unchecked->state().checkable);
    QVERIFY(!unchecked->state().checked);

    QCOMPARE(checked->role(), QAccessible::ListItem);
    QVERIFY(checked->state().checkable);
    QVERIFY(checked->state().checked);
}

void TstCanvasAccessibility::role_blockquote_is_section_limitation()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("> quoted text\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::BlockQuote);
    // LIMITATION (spec §4.6 finding 4): ROLE_BLOCK_QUOTE is unreachable from
    // Qt — Section is the best available role, not a bug to "fix".
    QCOMPARE(iface->child(0)->role(), QAccessible::Section);
}

void TstCanvasAccessibility::role_horizontalrule()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("text\n\n---\n\nmore\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(3));
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::HorizontalRule);
    QCOMPARE(iface->child(1)->role(), QAccessible::Separator);
}

void TstCanvasAccessibility::role_image()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("![a cat](cat.png)\n");
    View view;
    attachAndExpose(view, doc);

    // Image is a load-time caret-block promotion (View::promoteCaretBlockKind),
    // not a direct parser mapping — confirmed distinctly from every other
    // row in this file, which read straight off MarkoffDocument::blockKind()
    // with no View involvement at all.
    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::Image);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QCOMPARE(iface->child(0)->role(), QAccessible::Graphic);
}

void TstCanvasAccessibility::role_math_limitation()
{
    // BlockKind::Math is never produced by the load path or by any promotion
    // View wires up from a mere load — it is reachable only through live
    // typed inference (tst_canvas_math.cpp, tst_canvas_kind_transition.cpp).
    // Exercise the role mapping directly at the document level instead,
    // same technique role_mermaid() below uses: testInsertBlock() + attach
    // (View::setDocument() primes its block-index cache synchronously, no
    // d2DocumentChanged signal needed — see A1.1's findings-log note on
    // that signal's debounce for why a signal-driven path would not do).
    MarkoffDocument doc;
    const BlockId id = doc.testInsertBlock(BlockKind::Math, "$$x^2$$");
    View view;
    attachAndExpose(view, doc);

    QCOMPARE(doc.blockKind(id), BlockKind::Math);
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QCOMPARE(iface->childCount(), 1);
    // LIMITATION (spec §4.6 finding 4): ROLE_MATH is unreachable from Qt —
    // StaticText (→ ROLE_LABEL) is the best available role, not a bug to fix.
    QCOMPARE(iface->child(0)->role(), QAccessible::StaticText);
}

void TstCanvasAccessibility::role_mermaid()
{
    // BlockKind::Mermaid is never assigned by the load path, and nothing in
    // canvas ever assigns it either (confirmed: mermaid fences load as
    // BlockKind::CodeBlock with infoString "mermaid" — BlockPresentation.cpp's
    // own comment). testInsertBlock() + attach is the only way to exercise
    // this row at all; see role_math_limitation() above for why that's a
    // legitimate substitute for a load fixture here.
    MarkoffDocument doc;
    const BlockId id = doc.testInsertBlock(BlockKind::Mermaid, "graph TD; A-->B;");
    View view;
    attachAndExpose(view, doc);

    QCOMPARE(doc.blockKind(id), BlockKind::Mermaid);
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QCOMPARE(iface->childCount(), 1);
    QCOMPARE(iface->child(0)->role(), QAccessible::Graphic);
}

void TstCanvasAccessibility::role_htmlblock()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("<div>\nraw html\n</div>\n");
    View view;
    attachAndExpose(view, doc);

    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::HtmlBlock);
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    // Raw source is what the user edits — EditableText per §4.2.
    QCOMPARE(iface->child(0)->role(), QAccessible::EditableText);
}

void TstCanvasAccessibility::role_table()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "| h0 | h1 |\n"
        "|----|----|\n"
        "| a0 | a1 |\n");
    View view;
    attachAndExpose(view, doc);

    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::Table);
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    // QAccessibleTableInterface is explicitly deferred (spec §6) — role only.
    QCOMPARE(iface->child(0)->role(), QAccessible::Table);
}

void TstCanvasAccessibility::role_footnote_def_paragraph_is_section()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Body text with a ref[^1].\n\n[^1]: The footnote content.\n");
    View view;
    attachAndExpose(view, doc);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));
    // The footnote-def line is an ordinary Paragraph in the document model
    // (View::isFootnoteDefBlock is presentation-layer detection over the
    // realized entry, same as tst_canvas_side_content.cpp establishes) —
    // this is the spec §4.2 *(footnote def)* row's whole reason to exist.
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Paragraph);
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::Paragraph);
    QVERIFY(!view.isFootnoteDefBlock(blocks[0]));
    QVERIFY(view.isFootnoteDefBlock(blocks[1]));

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QCOMPARE(iface->child(0)->role(), QAccessible::Paragraph);
    // LIMITATION (spec §4.6 finding 4 area — no ROLE_FOOTNOTE in Qt either):
    // Section, same best-available choice as BlockQuote.
    QCOMPARE(iface->child(1)->role(), QAccessible::Section);
}

// ---- A1.2: state mapping -------------------------------------------------

void TstCanvasAccessibility::state_focusable_and_focused_tracks_caret()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);

    view.setCaretPosition(blocks[0], 0);
    QVERIFY(iface->child(0)->state().focusable);
    QVERIFY(iface->child(0)->state().focused);
    QVERIFY(iface->child(1)->state().focusable);
    QVERIFY(!iface->child(1)->state().focused);

    view.setCaretPosition(blocks[1], 0);
    QVERIFY(!iface->child(0)->state().focused);
    QVERIFY(iface->child(1)->state().focused);
}

void TstCanvasAccessibility::state_editable_tracks_read_only()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(iface->child(0)->state().editable);

    view.setReadOnly(true);
    QVERIFY(!iface->child(0)->state().editable);

    view.setReadOnly(false);
    QVERIFY(iface->child(0)->state().editable);
}

void TstCanvasAccessibility::state_invisible_for_folded_hidden_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "# Section One\n"
        "para one\n\n"
        "para two\n\n"
        "# Section Two\n"
        "para three\n");
    View view;
    attachAndExpose(view, doc);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(5));
    const BlockId h1 = blocks[0];
    const BlockId p1 = blocks[1];
    const BlockId p2 = blocks[2];
    const BlockId h2 = blocks[3];

    QVERIFY(view.isBlockFoldable(h1));
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QVERIFY(!iface->child(int(view.blockIndexOf(p1)))->state().invisible);

    view.toggleFold(h1);
    QVERIFY(view.isBlockHidden(p1));
    QVERIFY(view.isBlockHidden(p2));
    QVERIFY(!view.isBlockHidden(h1));
    QVERIFY(!view.isBlockHidden(h2));

    // Hidden blocks stay in the child list (spec §4.3 — removing them would
    // destabilize child indices for AT clients holding references) and
    // report invisible; the fold head and the next section's head do not.
    QCOMPARE(iface->childCount(), 5);
    QVERIFY(iface->child(int(view.blockIndexOf(p1)))->state().invisible);
    QVERIFY(iface->child(int(view.blockIndexOf(p2)))->state().invisible);
    QVERIFY(!iface->child(int(view.blockIndexOf(h1)))->state().invisible);
    QVERIFY(!iface->child(int(view.blockIndexOf(h2)))->state().invisible);
}

// ---- A2.1: QAccessibleTextInterface core --------------------------------

void TstCanvasAccessibility::text_interface_absent_for_no_text_kinds()
{
    // Spec §4.2: HorizontalRule, Image, Mermaid have no text interface at
    // all — interface_cast(TextInterface) must return nullptr for them,
    // not a vacuous implementation.
    {
        MarkoffDocument doc;
        doc.loadFromMarkdown("text\n\n---\n\nmore\n");
        View view;
        attachAndExpose(view, doc);
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(doc.blockKind(blocks[1]), BlockKind::HorizontalRule);
        QVERIFY(!iface->child(1)->textInterface());
    }
    {
        MarkoffDocument doc;
        doc.loadFromMarkdown("![a cat](cat.png)\n");
        View view;
        attachAndExpose(view, doc);
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
        QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::Image);
        QVERIFY(!iface->child(0)->textInterface());
    }
    {
        MarkoffDocument doc;
        const BlockId id = doc.testInsertBlock(BlockKind::Mermaid, "graph TD; A-->B;");
        View view;
        attachAndExpose(view, doc);
        QCOMPARE(doc.blockKind(id), BlockKind::Mermaid);
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
        QVERIFY(!iface->child(0)->textInterface());
    }
}

void TstCanvasAccessibility::text_interface_present_for_text_kinds()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);
    QCOMPARE(text->characterCount(), int(QStringLiteral("First paragraph.").size()));
}

void TstCanvasAccessibility::text_and_character_count_ascii()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);

    const QString expected = QStringLiteral("Hello world.");
    QCOMPARE(text->characterCount(), expected.size());
    QCOMPARE(text->text(0, text->characterCount()), expected);
    QCOMPARE(text->text(0, 5), QStringLiteral("Hello"));
    QCOMPARE(text->text(6, 11), QStringLiteral("world"));
}

void TstCanvasAccessibility::text_and_character_count_multibyte_utf8()
{
    // "café 日本語 😀!" — accents (2-byte UTF-8, 1 QChar), CJK (3-byte
    // UTF-8, 1 QChar each), and an emoji requiring a UTF-16 surrogate pair
    // (4-byte UTF-8, 2 QChars). This is the QChar/byte mix-up break point
    // the plan calls out explicitly.
    const QString fixture = QString::fromUtf8("caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x98\x80!");
    QCOMPARE(fixture, QStringLiteral(u"café 日本語 😀!"));
    // Sanity on the fixture's own shape before trusting the assertions
    // below: 12 QChars (the emoji is a surrogate pair), well under its
    // UTF-8 byte length.
    QCOMPARE(fixture.size(), 12);
    QVERIFY(fixture.toUtf8().size() > fixture.size());

    MarkoffDocument doc;
    doc.loadFromMarkdown(fixture.toUtf8() + "\n");
    View view;
    attachAndExpose(view, doc);

    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::Paragraph);
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);

    // characterCount() is a QChar count, NOT a byte count (A2.1 done-when).
    QCOMPARE(text->characterCount(), 12);
    QCOMPARE(text->text(0, text->characterCount()), fixture);

    // Slice out just the CJK run: QChar indices 5..8.
    QCOMPARE(text->text(5, 8), QStringLiteral(u"日本語"));

    // Slice out just the emoji (occupies QChar indices 9..11, a surrogate
    // pair) — this is the exact boundary a byte-offset/QChar-offset
    // mix-up would get wrong silently (e.g. it would land mid-surrogate or
    // include/exclude the wrong number of trailing bytes).
    QCOMPARE(text->text(9, 11), QStringLiteral(u"😀"));

    // The trailing "!" is the last QChar, at index 11.
    QCOMPARE(text->text(11, 12), QStringLiteral("!"));
}

void TstCanvasAccessibility::char_boundary_at_offset()
{
    const QString fixture = QStringLiteral(u"café 😀!");
    MarkoffDocument doc;
    doc.loadFromMarkdown(fixture.toUtf8() + "\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);

    // "café " is QChars 0..4 ('c','a','f','é',' '); the accented 'é'
    // grapheme is a single QChar at offset 3.
    int s = -1, e = -1;
    QCOMPARE(text->textAtOffset(3, QAccessible::CharBoundary, &s, &e), QStringLiteral(u"é"));
    QCOMPARE(s, 3);
    QCOMPARE(e, 4);

    // The emoji (surrogate pair) is one grapheme spanning QChars 5..6.
    s = e = -1;
    QCOMPARE(text->textAtOffset(5, QAccessible::CharBoundary, &s, &e), QStringLiteral(u"😀"));
    QCOMPARE(s, 5);
    QCOMPARE(e, 7);
}

void TstCanvasAccessibility::word_boundary_at_offset()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);

    int s = -1, e = -1;
    QCOMPARE(text->textAtOffset(0, QAccessible::WordBoundary, &s, &e), QStringLiteral("Hello"));
    QCOMPARE(s, 0);
    QCOMPARE(e, 5);

    s = e = -1;
    QCOMPARE(text->textAtOffset(6, QAccessible::WordBoundary, &s, &e), QStringLiteral("world"));
    QCOMPARE(s, 6);
    QCOMPARE(e, 11);
}

void TstCanvasAccessibility::paragraph_boundary_is_whole_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);

    // A block IS a paragraph (spec §4.1/A2.1 task description) — the
    // paragraph at ANY in-range offset is the whole block.
    for (int offset : {0, 5, 12}) {
        int s = -1, e = -1;
        QCOMPARE(text->textAtOffset(offset, QAccessible::ParagraphBoundary, &s, &e),
                 QStringLiteral("Hello world."));
        QCOMPARE(s, 0);
        QCOMPARE(e, text->characterCount());
    }
}

void TstCanvasAccessibility::paragraph_boundary_whole_block_with_embedded_newlines_in_codeblock()
{
    // CodeBlock buffers keep their fence AND interior newlines inline
    // (markoff-core CLAUDE.md's buffer-convention table) — this is the
    // case that actually exercises the override: the base
    // QAccessibleTextInterface::textAtOffset default treats
    // ParagraphBoundary as a line-break search and would incorrectly stop
    // at the first embedded '\n'.
    MarkoffDocument doc;
    doc.loadFromMarkdown("```python\ndef foo():\n    return 1\n```\n");
    View view;
    attachAndExpose(view, doc);

    QCOMPARE(doc.blockKind(doc.iterateBlocks().front()), BlockKind::CodeBlock);
    const QByteArray raw = doc.blockText(doc.iterateBlocks().front());
    QVERIFY(raw.contains('\n'));  // sanity: the buffer really has embedded newlines.

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);

    const QString whole = QString::fromUtf8(raw);
    int s = -1, e = -1;
    QCOMPARE(text->textAtOffset(0, QAccessible::ParagraphBoundary, &s, &e), whole);
    QCOMPARE(s, 0);
    QCOMPARE(e, text->characterCount());
    QCOMPARE(text->characterCount(), whole.size());
}

void TstCanvasAccessibility::paragraph_before_after_boundary_report_no_item()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);

    // A block is exactly one paragraph — there is no paragraph before or
    // after it within the block's own buffer.
    int s = 0, e = 0;
    QCOMPARE(text->textBeforeOffset(5, QAccessible::ParagraphBoundary, &s, &e), QString());
    QCOMPARE(s, -1);
    QCOMPARE(e, -1);

    s = e = 0;
    QCOMPARE(text->textAfterOffset(5, QAccessible::ParagraphBoundary, &s, &e), QString());
    QCOMPARE(s, -1);
    QCOMPARE(e, -1);
}

// ---- A2.2: caret + selection ----------------------------------------------

void TstCanvasAccessibility::cursor_position_only_on_caret_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);

    view.setCaretPosition(blocks[0], 3);
    QCOMPARE(iface->child(0)->textInterface()->cursorPosition(), 3);
    QCOMPARE(iface->child(1)->textInterface()->cursorPosition(), -1);

    view.setCaretPosition(blocks[1], 5);
    QCOMPARE(iface->child(0)->textInterface()->cursorPosition(), -1);
    QCOMPARE(iface->child(1)->textInterface()->cursorPosition(), 5);
}

void TstCanvasAccessibility::set_cursor_position_routes_to_view_caret()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);

    iface->child(1)->textInterface()->setCursorPosition(6);
    QCOMPARE(view.caretBlock(), blocks[1]);
    QCOMPARE(view.caretByteOffset(), 6);
    // Round trip.
    QCOMPARE(iface->child(1)->textInterface()->cursorPosition(), 6);
}

void TstCanvasAccessibility::selection_within_single_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    const auto blocks = doc.iterateBlocks();
    view.setCaretPosition(blocks[0], 0);
    for (int i = 0; i < 5; ++i)
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QVERIFY(view.hasSelection());

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text0 = iface->child(0)->textInterface();
    QCOMPARE(text0->selectionCount(), 1);
    int start = -2, end = -2;
    text0->selection(0, &start, &end);
    QCOMPARE(start, 0);
    QCOMPARE(end, 5);

    // No other block is touched by a same-block selection.
    QAccessibleTextInterface *text1 = iface->child(1)->textInterface();
    QCOMPARE(text1->selectionCount(), 0);
}

void TstCanvasAccessibility::selection_partial_range_within_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    const auto blocks = doc.iterateBlocks();
    view.setCaretPosition(blocks[0], 4);
    for (int i = 0; i < 4; ++i)
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text0 = iface->child(0)->textInterface();
    int start = -2, end = -2;
    text0->selection(0, &start, &end);
    // A selection ending mid-block reports the right partial range, not the
    // whole block (A2.2's done-when).
    QCOMPARE(start, 4);
    QCOMPARE(end, 8);
}

void TstCanvasAccessibility::selection_spans_multiple_blocks()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(3));
    const int len0 = doc.blockText(blocks[0]).size();
    const int len1 = doc.blockText(blocks[1]).size();
    const int len2 = doc.blockText(blocks[2]).size();

    // Anchor 6 bytes into block 0, extend selection to the end of the
    // document — spans all three blocks.
    view.setCaretPosition(blocks[0], 6);
    QTest::keyClick(&view, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorBlock(), blocks[0]);
    QCOMPARE(view.caretBlock(), blocks[2]);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    int start = -2, end = -2;

    // Cross-block selections present as a per-block selection on each
    // spanned block (spec §4.1) — first block partial (from the anchor to
    // its end), middle block whole, last block whole (up to the caret,
    // which landed at its end).
    iface->child(0)->textInterface()->selection(0, &start, &end);
    QCOMPARE(start, 6);
    QCOMPARE(end, len0);

    iface->child(1)->textInterface()->selection(0, &start, &end);
    QCOMPARE(start, 0);
    QCOMPARE(end, len1);

    iface->child(2)->textInterface()->selection(0, &start, &end);
    QCOMPARE(start, 0);
    QCOMPARE(end, len2);
}

void TstCanvasAccessibility::no_selection_reports_empty()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    QVERIFY(!view.hasSelection());
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    for (int i = 0; i < iface->childCount(); ++i) {
        QAccessibleTextInterface *text = iface->child(i)->textInterface();
        QCOMPARE(text->selectionCount(), 0);
        int start = -2, end = -2;
        text->selection(0, &start, &end);
        QCOMPARE(start, -1);
        QCOMPARE(end, -1);
    }
}

// ---- A2.3: geometry and line boundaries -----------------------------------

void TstCanvasAccessibility::character_rect_offset_at_point_round_trip()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world, this is a paragraph.\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);

    // Round trip (A2.3 done-when): offsetAtPoint(characterRect(n).center())
    // == n, for every offset in a realized block — within a ±1 tolerance.
    // A `QRectF` narrower than ~2px rounds through `.toRect()`/`.center()`
    // to an integer point that can sit exactly on the boundary between two
    // characters, which `xToCursor()` (Qt's own nearest-cursor rounding)
    // can legitimately resolve to either neighbor — the same ambiguity a
    // real mouse click has at a sub-pixel character boundary, not a bug in
    // either direction of the conversion.
    const int count = text->characterCount();
    QVERIFY(count > 0);
    for (int n = 0; n < count; ++n) {
        const QRect r = text->characterRect(n);
        QVERIFY2(r.isValid(), qPrintable(QString("offset %1").arg(n)));
        const int roundTripped = text->offsetAtPoint(r.center());
        QVERIFY2(qAbs(roundTripped - n) <= 1,
                 qPrintable(QString("offset %1 round-tripped to %2").arg(n).arg(roundTripped)));
    }
}

void TstCanvasAccessibility::character_rect_unrealized_block_realizes_just_that_one()
{
    QByteArray src;
    for (int i = 0; i < 60; ++i)
        src += "Paragraph number " + QByteArray::number(i) + " with some words.\n\n";

    MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    View view;
    view.resize(400, 200);  // small viewport: only the first few blocks realize
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(60));

    const int before = view.realizedBlockCount();
    QVERIFY(before < view.blockCount());

    // A block far below the viewport is not yet realized.
    const BlockId farBlock = blocks[50];
    QVERIFY(view.blockRect(farBlock).isNull() || !view.characterRectInViewport(farBlock, 0).isValid());

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *farText = iface->child(50)->textInterface();
    QVERIFY(farText);
    const QRect r = farText->characterRect(0);
    QVERIFY(r.isValid());

    // Realizing to answer the query touched exactly this one block, not the
    // whole document (spec §5's bounded-cost argument for the tree shape).
    QCOMPARE(view.realizedBlockCount(), before + 1);
}

void TstCanvasAccessibility::character_rect_no_text_kind_returns_null()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("text\n\n---\n\nmore\n");
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    const auto blocks = doc.iterateBlocks();
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::HorizontalRule);
    // No text interface at all for a kind with no text content (A2.1).
    QVERIFY(!iface->child(1)->textInterface());
}

void TstCanvasAccessibility::offset_at_point_outside_block_returns_negative_one()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *block0 = iface->child(0);
    QAccessibleInterface *block1 = iface->child(1);
    QAccessibleTextInterface *text0 = block0->textInterface();
    QVERIFY(text0);

    // A point inside block 1's rect is not a valid offset for block 0's
    // text interface (C4's per-block discipline extended to geometry).
    const QPoint pointInBlock1 = block1->rect().center();
    QCOMPARE(text0->offsetAtPoint(pointInBlock1), -1);
}

void TstCanvasAccessibility::line_boundary_reports_wrapped_visual_line()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(
        "This paragraph has enough words in it that it should wrap onto "
        "more than one visual line once the view is narrow enough to "
        "force it, without any embedded newline characters at all.\n");
    View view;
    view.resize(180, 400);  // narrow: forces wrapping
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(&view);
    QAccessibleTextInterface *text = iface->child(0)->textInterface();
    QVERIFY(text);
    const int count = text->characterCount();

    int start = -2, end = -2;
    const QString firstLine = text->textAtOffset(0, QAccessible::LineBoundary, &start, &end);
    QVERIFY(!firstLine.isEmpty());
    QCOMPARE(start, 0);
    // The first visual LINE is a strict prefix of the whole block — proof
    // this is real wrapped-line detection, not the base class's literal
    // '\n'-splitting default (which has no '\n' to find here at all and
    // would report the whole block as one "line", end == count).
    QVERIFY(end < count);

    // A later offset (well past the first line) reports a DIFFERENT range.
    int start2 = -2, end2 = -2;
    const QString laterLine = text->textAtOffset(count - 1, QAccessible::LineBoundary, &start2, &end2);
    QVERIFY(!laterLine.isEmpty());
    QVERIFY(start2 >= end);
    QCOMPARE(end2, count);
}

namespace {
int g_sentinelHits = 0;
void sentinelHandler(QAccessibleEvent *) { ++g_sentinelHits; }
}  // namespace

void TstCanvasAccessibility::spy_captures_hand_fired_events_with_payload()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);

    MarkoffTest::A11yEventSpy spy;
    QCOMPARE(spy.count(), 0);

    QAccessibleEvent focus(&view, QAccessible::Focus);
    QAccessible::updateAccessibility(&focus);
    QAccessibleTextCursorEvent cursor(&view, 7);
    QAccessible::updateAccessibility(&cursor);
    QAccessibleTextInsertEvent ins(&view, 3, QStringLiteral("abc"));
    QAccessible::updateAccessibility(&ins);
    QAccessibleTextSelectionEvent sel(&view, 2, 5);
    QAccessible::updateAccessibility(&sel);

    QCOMPARE(spy.count(), 4);
    QCOMPARE(spy.countOfType(QAccessible::Focus, &view), 1);
    const auto cur = spy.eventsOfType(QAccessible::TextCaretMoved, &view);
    QCOMPARE(cur.size(), 1);
    QCOMPARE(cur.first().a, 7);
    const auto in = spy.eventsOfType(QAccessible::TextInserted);
    QCOMPARE(in.size(), 1);
    QCOMPARE(in.first().a, 3);
    QCOMPARE(in.first().text, QStringLiteral("abc"));
    const auto se = spy.eventsOfType(QAccessible::TextSelectionChanged);
    QCOMPARE(se.first().a, 2);
    QCOMPARE(se.first().b, 5);
    // Records are snapshots: still valid after the hand-fired events died.
    spy.clear();
    QCOMPARE(spy.count(), 0);
}

void TstCanvasAccessibility::spy_restores_previous_handler_and_active_state()
{
    const bool activeBefore = QAccessible::isActive();
    g_sentinelHits = 0;
    QAccessible::UpdateHandler orig = QAccessible::installUpdateHandler(&sentinelHandler);
    {
        MarkoffTest::A11yEventSpy spy;
        // While the spy lives it, not the sentinel, receives events, and the
        // platform reports active if it has an accessibility integration.
        QObject probe;
        QAccessibleEvent ev(&probe, QAccessible::Focus);
        QAccessible::updateAccessibility(&ev);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(g_sentinelHits, 0);
    }
    // Spy gone: the sentinel is the installed handler again...
    QObject probe;
    QAccessibleEvent ev(&probe, QAccessible::Focus);
    QAccessible::updateAccessibility(&ev);
    QCOMPARE(g_sentinelHits, 1);
    // ...and active state is back to what it was.
    QCOMPARE(QAccessible::isActive(), activeBefore);
    // Put the process back the way we found it.
    QVERIFY(QAccessible::installUpdateHandler(orig) == &sentinelHandler);
}

void TstCanvasAccessibility::spy_second_case_sees_no_stale_events()
{
    // Case one leaves events behind in its spy, which is then destroyed...
    {
        MarkoffTest::A11yEventSpy first;
        QObject o;
        QAccessibleEvent ev(&o, QAccessible::ObjectCreated);
        QAccessible::updateAccessibility(&ev);
        QCOMPARE(first.count(), 1);
    }
    // ...a fresh spy starts empty, and an event fired with no spy alive is
    // not retroactively delivered to it either.
    QObject o;
    QAccessibleEvent unobserved(&o, QAccessible::ObjectDestroyed);
    QAccessible::updateAccessibility(&unobserved);
    MarkoffTest::A11yEventSpy second;
    QCOMPARE(second.count(), 0);
    QAccessibleEvent seen(&o, QAccessible::ObjectShow);
    QAccessible::updateAccessibility(&seen);
    QCOMPARE(second.count(), 1);
    QCOMPARE(second.events().first().type, QAccessible::ObjectShow);
}

void TstCanvasAccessibility::spy_nested_restores_in_lifo_order()
{
    QObject o;
    MarkoffTest::A11yEventSpy outer;
    {
        MarkoffTest::A11yEventSpy inner;
        QAccessibleEvent ev(&o, QAccessible::Focus);
        QAccessible::updateAccessibility(&ev);
        QCOMPARE(inner.count(), 1);
        QCOMPARE(outer.count(), 0);
    }
    // Inner gone: outer receives events again (handler is a shared function
    // pointer, so this exercises the static current-spy chain).
    QAccessibleEvent ev(&o, QAccessible::Focus);
    QAccessible::updateAccessibility(&ev);
    QCOMPARE(outer.count(), 1);
}


namespace {
/// Priming: the a11y-side "last announced" state starts empty when the spy
/// (which forces QAccessible active) is created, so put the view in the
/// state under test, then drop the events that priming itself produced.
QAccessibleInterface *blockOf(View &view, int i)
{
    return QAccessible::queryAccessibleInterface(&view)->child(i);
}
}  // namespace

void TstCanvasAccessibility::events_caret_move_within_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b0 = blockOf(view, 0);

    MarkoffTest::A11yEventSpy spy;
    view.setCaretPosition(blocks[0], 2);
    spy.clear();

    view.setCaretPosition(blocks[0], 7);
    const auto cur = spy.eventsOfType(QAccessible::TextCaretMoved, b0);
    QCOMPARE(cur.size(), 1);
    QCOMPARE(cur.first().a, 7);
    // Nothing for the other blocks.
    QCOMPARE(spy.count(), 1);
}

void TstCanvasAccessibility::events_caret_move_across_blocks()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b0 = blockOf(view, 0);
    QAccessibleInterface *b1 = blockOf(view, 1);

    MarkoffTest::A11yEventSpy spy;
    view.setCaretPosition(blocks[0], 3);
    spy.clear();

    view.setCaretPosition(blocks[1], 4);
    // Event on the NEW block, with the per-block QChar offset...
    const auto cur = spy.eventsOfType(QAccessible::TextCaretMoved, b1);
    QCOMPARE(cur.size(), 1);
    QCOMPARE(cur.first().a, 4);
    // ...and, by decision (A3.2 log), none on the old block: it just
    // reports cursorPosition() == -1 when asked.
    QCOMPARE(spy.countOfType(QAccessible::TextCaretMoved, b0), 0);
    QCOMPARE(b0->textInterface()->cursorPosition(), -1);
}

void TstCanvasAccessibility::events_no_change_no_event()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b0 = blockOf(view, 0);

    MarkoffTest::A11yEventSpy spy;
    view.setCaretPosition(blocks[0], 3);
    spy.clear();
    view.setCaretPosition(blocks[0], 3);
    QCOMPARE(spy.countOfType(QAccessible::TextCaretMoved, b0), 0);
    QCOMPARE(spy.countOfType(QAccessible::TextSelectionChanged, b0), 0);
}

void TstCanvasAccessibility::events_selection_extend_and_collapse_single_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b0 = blockOf(view, 0);

    MarkoffTest::A11yEventSpy spy;
    view.setCaretPosition(blocks[0], 2);
    spy.clear();

    // Start.
    QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    auto sel = spy.eventsOfType(QAccessible::TextSelectionChanged, b0);
    QCOMPARE(sel.size(), 1);
    QCOMPARE(sel.last().a, 2);
    QCOMPARE(sel.last().b, 3);
    // Extend.
    QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    sel = spy.eventsOfType(QAccessible::TextSelectionChanged, b0);
    QCOMPARE(sel.size(), 2);
    QCOMPARE(sel.last().a, 2);
    QCOMPARE(sel.last().b, 4);
    // Collapse (plain move): selection cleared -> (-1,-1).
    spy.clear();
    QTest::keyClick(&view, Qt::Key_Right);
    sel = spy.eventsOfType(QAccessible::TextSelectionChanged, b0);
    QCOMPARE(sel.size(), 1);
    QCOMPARE(sel.last().a, -1);
    QCOMPARE(sel.last().b, -1);
    QCOMPARE(spy.countOfType(QAccessible::TextCaretMoved, b0), 1);
}

void TstCanvasAccessibility::events_selection_cross_block_and_shrink()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    const int len0 = doc.blockText(blocks[0]).size();
    const int len1 = doc.blockText(blocks[1]).size();
    const int len2 = doc.blockText(blocks[2]).size();
    QAccessibleInterface *b0 = blockOf(view, 0);
    QAccessibleInterface *b1 = blockOf(view, 1);
    QAccessibleInterface *b2 = blockOf(view, 2);

    MarkoffTest::A11yEventSpy spy;
    view.setCaretPosition(blocks[0], 6);
    spy.clear();

    // Grow across all three blocks.
    QTest::keyClick(&view, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    auto ev = [&](QAccessibleInterface *b) {
        const auto l = spy.eventsOfType(QAccessible::TextSelectionChanged, b);
        return l.isEmpty() ? MarkoffTest::A11yEventRecord() : l.last();
    };
    QCOMPARE(spy.countOfType(QAccessible::TextSelectionChanged, b0), 1);
    QCOMPARE(ev(b0).a, 6);
    QCOMPARE(ev(b0).b, len0);
    QCOMPARE(ev(b1).a, 0);
    QCOMPARE(ev(b1).b, len1);
    QCOMPARE(ev(b2).a, 0);
    QCOMPARE(ev(b2).b, len2);
    // Caret moved to the last block.
    QCOMPARE(spy.countOfType(QAccessible::TextCaretMoved, b2), 1);

    // Shrink: caret back into block 0 (Ctrl+Shift+Home would flip; use Up
    // twice from the end). The blocks that fall out of the selection get
    // an explicit "cleared" (-1,-1) event.
    spy.clear();
    view.setCaretPosition(blocks[0], 0);  // collapses everything
    QCOMPARE(ev(b0).a, -1);
    QCOMPARE(ev(b1).a, -1);
    QCOMPARE(ev(b1).b, -1);
    QCOMPARE(ev(b2).a, -1);
}

void TstCanvasAccessibility::events_focus_in_out()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    view.setCaretPosition(blocks[1], 0);
    QAccessibleInterface *b1 = blockOf(view, 1);

    MarkoffTest::A11yEventSpy spy;
    QFocusEvent in(QEvent::FocusIn, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(&view, &in);
    // Focus lands on the caret's block (the effective focus holder).
    QCOMPARE(spy.countOfType(QAccessible::Focus, b1), 1);

    spy.clear();
    QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason);
    QCoreApplication::sendEvent(&view, &out);
    const auto st = spy.eventsOfType(QAccessible::StateChanged, b1);
    QCOMPARE(st.size(), 1);
    QVERIFY(st.first().changedStates.focused);

    // While focused, a caret move into another block also moves a11y focus.
    QCoreApplication::sendEvent(&view, &in);
    spy.clear();
    view.setCaretPosition(blocks[2], 1);
    QCOMPARE(spy.countOfType(QAccessible::Focus, blockOf(view, 2)), 1);
}

void TstCanvasAccessibility::events_inactive_emits_nothing()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();

    MarkoffTest::A11yEventSpy spy;
    QAccessible::setActive(false);
    if (QAccessible::isActive())
        QSKIP("platform keeps QAccessible active; cannot test the inactive path");
    view.setCaretPosition(blocks[1], 2);
    QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QCOMPARE(spy.count(), 0);
}

// ---- A3.3 ---------------------------------------------------------------

namespace {
QList<MarkoffTest::A11yEventRecord> typed(const MarkoffTest::A11yEventSpy &spy,
                                          QAccessible::Event t, const QAccessibleInterface *i)
{
    return spy.eventsOfType(t, i);
}
}  // namespace

void TstCanvasAccessibility::text_typing_emits_insert()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b1 = blockOf(view, 1);
    QAccessibleInterface *b0 = blockOf(view, 0);
    view.setCaretPosition(blocks[1], 6);

    MarkoffTest::A11yEventSpy spy;
    QTest::keyClicks(&view, "XY");
    const auto ins = typed(spy, QAccessible::TextInserted, b1);
    QCOMPARE(ins.size(), 2);
    QCOMPARE(ins[0].a, 6);
    QCOMPARE(ins[0].text, QStringLiteral("X"));
    QCOMPARE(ins[1].a, 7);
    QCOMPARE(ins[1].text, QStringLiteral("Y"));
    QCOMPARE(spy.countOfType(QAccessible::TextRemoved, b1), 0);
    // Other blocks hear nothing about it.
    QCOMPARE(spy.countOfType(QAccessible::TextInserted, b0), 0);
    QCOMPARE(b1->textInterface()->text(0, 20), QStringLiteral("SecondXY paragraph."));
}

void TstCanvasAccessibility::text_backspace_and_delete_emit_remove()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b1 = blockOf(view, 1);
    view.setCaretPosition(blocks[1], 3);  // "Sec|ond paragraph."

    MarkoffTest::A11yEventSpy spy;
    QTest::keyClick(&view, Qt::Key_Backspace);
    doc.flushPendingD2Changed();  // the doc's own debounce, not a view deferral
    auto rem = typed(spy, QAccessible::TextRemoved, b1);
    QCOMPARE(rem.size(), 1);
    QCOMPARE(rem[0].a, 2);
    QCOMPARE(rem[0].text, QStringLiteral("c"));

    spy.clear();
    QTest::keyClick(&view, Qt::Key_Delete);
    doc.flushPendingD2Changed();
    rem = typed(spy, QAccessible::TextRemoved, b1);
    QCOMPARE(rem.size(), 1);
    QCOMPARE(rem[0].a, 2);
    QCOMPARE(rem[0].text, QStringLiteral("o"));
    QCOMPARE(spy.countOfType(QAccessible::TextInserted, b1), 0);
}

void TstCanvasAccessibility::text_replace_selection_emits_remove_then_insert()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b1 = blockOf(view, 1);
    view.setCaretPosition(blocks[1], 0);
    for (int i = 0; i < 6; ++i)  // select "Second"
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);

    MarkoffTest::A11yEventSpy spy;
    QTest::keyClick(&view, 'Z');
    const auto rem = typed(spy, QAccessible::TextRemoved, b1);
    const auto ins = typed(spy, QAccessible::TextInserted, b1);
    QCOMPARE(rem.size(), 1);
    QCOMPARE(rem[0].a, 0);
    QCOMPARE(rem[0].text, QStringLiteral("Second"));
    QCOMPARE(ins.size(), 1);
    QCOMPARE(ins[0].a, 0);
    QCOMPARE(ins[0].text, QStringLiteral("Z"));
    // Remove is announced before insert.
    int remAt = -1, insAt = -1;
    for (int i = 0; i < spy.events().size(); ++i) {
        if (spy.events()[i].type == QAccessible::TextRemoved) remAt = i;
        if (spy.events()[i].type == QAccessible::TextInserted) insAt = i;
    }
    QVERIFY(remAt >= 0 && remAt < insAt);
}

void TstCanvasAccessibility::text_document_replace_edit_emits_remove_then_insert()
{
    // A non-typing path (paste/programmatic): one buffer edit that replaces
    // a range with different text. Middle-of-string diff: "Second paragraph."
    // -> "Second sentence." changes only "paragraph" -> "sentence".
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b1 = blockOf(view, 1);

    MarkoffTest::A11yEventSpy spy;
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2ApplyBufferEdit(blocks[1], 7, 9, QByteArrayLiteral("sentence"), t);
    }
    doc.flushPendingD2Changed();
    const auto rem = typed(spy, QAccessible::TextRemoved, b1);
    const auto ins = typed(spy, QAccessible::TextInserted, b1);
    QCOMPARE(b1->textInterface()->text(0, 40), QStringLiteral("Second sentence."));
    // The diff is minimal (shared prefix/suffix trimmed), so the payload is
    // a sub-range of the replaced/inserted text, anchored at the same start.
    QCOMPARE(rem.size(), 1);
    QCOMPARE(ins.size(), 1);
    QCOMPARE(rem[0].a, ins[0].a);
    QVERIFY(QStringLiteral("paragraph").contains(rem[0].text));
    QVERIFY(QStringLiteral("sentence").contains(ins[0].text));
    QVERIFY(rem[0].a >= 7);
}

void TstCanvasAccessibility::text_astral_char_keeps_surrogate_pair_whole()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b0 = blockOf(view, 0);
    // U+1F600 = F0 9F 98 80, inserted at byte 0 of "First paragraph.".
    MarkoffTest::A11yEventSpy spy;
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2ApplyBufferEdit(blocks[0], 0, 0, QByteArray("\xF0\x9F\x98\x80"), t);
    }
    doc.flushPendingD2Changed();
    const auto ins = typed(spy, QAccessible::TextInserted, b0);
    QCOMPARE(ins.size(), 1);
    QCOMPARE(ins[0].a, 0);
    QCOMPARE(ins[0].text.size(), 2);  // whole pair, QChar offsets
    QVERIFY(ins[0].text.at(0).isHighSurrogate());
}

void TstCanvasAccessibility::text_undo_emits_remove()
{
    // Undo mutates the buffer without touching the block's proxy counter —
    // the change token has to catch it via blockEditSequence.
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b1 = blockOf(view, 1);
    view.setCaretPosition(blocks[1], 6);
    QTest::keyClicks(&view, "Q");
    doc.flushPendingD2Changed();

    MarkoffTest::A11yEventSpy spy;
    doc.d2UndoLog().undo();
    doc.flushPendingD2Changed();
    const auto rem = typed(spy, QAccessible::TextRemoved, b1);
    QCOMPARE(rem.size(), 1);
    QCOMPARE(rem[0].a, 6);
    QCOMPARE(rem[0].text, QStringLiteral("Q"));
}

void TstCanvasAccessibility::structure_split_emits_created_and_head_remove()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *root = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *b1 = blockOf(view, 1);
    QCOMPARE(root->childCount(), 3);
    view.setCaretPosition(blocks[1], 6);  // "Second| paragraph."

    MarkoffTest::A11yEventSpy spy;
    QTest::keyClick(&view, Qt::Key_Return);
    doc.flushPendingD2Changed();
    QCOMPARE(root->childCount(), 4);
    // Exactly one new block, announced with ObjectCreated.
    const auto created = spy.eventsOfType(QAccessible::ObjectCreated);
    QCOMPARE(created.size(), 1);
    QAccessibleInterface *newBlock = root->child(2);
    QVERIFY(newBlock);
    QCOMPARE(created[0].iface, newBlock);
    QCOMPARE(newBlock->textInterface()->text(0, 40), QStringLiteral(" paragraph."));
    // The head lost its tail.
    const auto rem = typed(spy, QAccessible::TextRemoved, b1);
    QCOMPARE(rem.size(), 1);
    QCOMPARE(rem[0].a, 6);
    QCOMPARE(rem[0].text, QStringLiteral(" paragraph."));
    QCOMPARE(b1->textInterface()->text(0, 40), QStringLiteral("Second"));
    QCOMPARE(spy.countOfType(QAccessible::ObjectDestroyed), 0);
    QCOMPARE(root->indexOfChild(newBlock), 2);
}

void TstCanvasAccessibility::structure_merge_emits_destroyed_and_insert_and_evicts()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *root = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *b0 = blockOf(view, 0);
    QAccessibleInterface *b1 = blockOf(view, 1);
    const QAccessible::Id b1Id = QAccessible::uniqueId(b1);
    view.setCaretPosition(blocks[1], 0);

    MarkoffTest::A11yEventSpy spy;
    QTest::keyClick(&view, Qt::Key_Backspace);  // merge block 1 into block 0
    doc.flushPendingD2Changed();
    QCOMPARE(root->childCount(), 2);
    const auto destroyed = spy.eventsOfType(QAccessible::ObjectDestroyed);
    QCOMPARE(destroyed.size(), 1);
    QCOMPARE(destroyed[0].iface, b1);  // pointer compare only; it is gone now
    const auto ins = typed(spy, QAccessible::TextInserted, b0);
    QCOMPARE(ins.size(), 1);
    QCOMPARE(ins[0].a, 16);
    QCOMPARE(ins[0].text, QStringLiteral("Second paragraph."));
    QCOMPARE(b0->textInterface()->text(0, 60),
             QStringLiteral("First paragraph.Second paragraph."));
    // b1's interface is gone from Qt's cache.
    QVERIFY(!QAccessible::accessibleInterface(b1Id));
    for (int i = 0; i < root->childCount(); ++i)
        QVERIFY(root->child(i) != b1);
}

void TstCanvasAccessibility::structure_remote_edit_insert_remove_create_destroy()
{
    // Two replicas; B edits, its ops are applied to A (whose View is under
    // test) through applyRemoteOps — no local-input path involved.
    MarkoffDocument docA(quint16(301));
    MarkoffDocument docB(quint16(302));
    QList<Markoff::MarkoffOp> ops;
    Markoff::MarkoffBundleMeta meta;
    auto capture = QObject::connect(&docA, &MarkoffDocument::localOpsProduced, &docB,
        [&docB](QList<Markoff::MarkoffOp> o, Markoff::MarkoffBundleMeta m) {
            docB.applyRemoteOps(std::move(o), std::move(m));
        });
    BlockId first, second;
    {
        Markoff::UndoLog::Transaction t(docA.d2UndoLog());
        first = docA.d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
        docA.d2ApplyBufferEdit(first, 0, 0, QByteArrayLiteral("Hello world"), t);
        second = docA.d2InsertBlock(first, BlockKind::Paragraph, t);
        docA.d2ApplyBufferEdit(second, 0, 0, QByteArrayLiteral("Tail"), t);
    }
    docA.flushPendingD2Changed();
    QObject::disconnect(capture);
    QCOMPARE(docB.blockText(second), QByteArray("Tail"));

    View view;
    attachAndExpose(view, docA);
    QAccessibleInterface *root = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *bFirst = blockOf(view, 0);
    QAccessibleInterface *bSecond = blockOf(view, 1);
    QCOMPARE(root->childCount(), 2);

    auto fromB = [&](auto &&edit) {
        ops.clear();
        auto c = QObject::connect(&docB, &MarkoffDocument::localOpsProduced,
            [&](QList<Markoff::MarkoffOp> o, Markoff::MarkoffBundleMeta m) {
                ops = std::move(o);
                meta = std::move(m);
            });
        {
            Markoff::UndoLog::Transaction t(docB.d2UndoLog());
            edit(t);
        }
        docB.flushPendingD2Changed();
        QObject::disconnect(c);
        QVERIFY(!ops.isEmpty());
        docA.applyRemoteOps(ops, meta);
        docA.flushPendingD2Changed();
    };

    MarkoffTest::A11yEventSpy spy;
    // Remote insert.
    fromB([&](Markoff::UndoLog::Transaction &t) {
        docB.d2ApplyBufferEdit(first, 5, 0, QByteArrayLiteral(","), t);
    });
        auto ins = typed(spy, QAccessible::TextInserted, bFirst);
    QCOMPARE(ins.size(), 1);
    QCOMPARE(ins[0].a, 5);
    QCOMPARE(ins[0].text, QStringLiteral(","));
    // Remote remove.
    spy.clear();
    fromB([&](Markoff::UndoLog::Transaction &t) {
        docB.d2ApplyBufferEdit(first, 0, 1, QByteArray(), t);
    });
    auto rem = typed(spy, QAccessible::TextRemoved, bFirst);
    QCOMPARE(rem.size(), 1);
    QCOMPARE(rem[0].a, 0);
    QCOMPARE(rem[0].text, QStringLiteral("H"));
    // Remote block create.
    spy.clear();
    BlockId third;
    fromB([&](Markoff::UndoLog::Transaction &t) {
        third = docB.d2InsertBlock(second, BlockKind::Paragraph, t);
        docB.d2ApplyBufferEdit(third, 0, 0, QByteArrayLiteral("More"), t);
    });
    QCOMPARE(root->childCount(), 3);
    QCOMPARE(spy.eventsOfType(QAccessible::ObjectCreated).size(), 1);
    QCOMPARE(spy.eventsOfType(QAccessible::ObjectCreated)[0].iface, root->child(2));
    // Remote block destroy.
    spy.clear();
    const QAccessible::Id secondId = QAccessible::uniqueId(bSecond);
    fromB([&](Markoff::UndoLog::Transaction &t) { docB.d2RemoveBlock(second, t); });
    QCOMPARE(root->childCount(), 2);
    const auto gone = spy.eventsOfType(QAccessible::ObjectDestroyed);
    QCOMPARE(gone.size(), 1);
    QCOMPARE(gone[0].iface, bSecond);
    QVERIFY(!QAccessible::accessibleInterface(secondId));
}

void TstCanvasAccessibility::eviction_no_dangling_interface_after_removal()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *root = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *b0 = blockOf(view, 0);
    QAccessibleInterface *b1 = blockOf(view, 1);
    QAccessibleInterface *b2 = blockOf(view, 2);
    const QAccessible::Id id1 = QAccessible::uniqueId(b1);
    QVERIFY(QAccessible::accessibleInterface(id1) == b1);

    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2RemoveBlock(blocks[1], t);
    }
    doc.flushPendingD2Changed();

    QCOMPARE(root->childCount(), 2);
    QVERIFY(!QAccessible::accessibleInterface(id1));       // Qt's cache
    QCOMPARE(root->indexOfChild(b0), 0);
    QCOMPARE(root->indexOfChild(b2), 1);                    // survivors intact, same objects
    QCOMPARE(root->child(0), b0);
    QCOMPARE(root->child(1), b2);
    for (int i = -1; i < 4; ++i)
        QVERIFY(root->child(i) != b1);
    // Undo brings the block back: a FRESH accessible, not the freed one.
    doc.d2UndoLog().undo();
    doc.flushPendingD2Changed();
    QCOMPARE(root->childCount(), 3);
    QVERIFY(root->child(1));
    QVERIFY(root->child(1)->isValid());
}

void TstCanvasAccessibility::eviction_runs_while_inactive_and_emits_nothing()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *root = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *b1 = blockOf(view, 1);
    const QAccessible::Id id1 = QAccessible::uniqueId(b1);
    view.setCaretPosition(blocks[0], 0);

    MarkoffTest::A11yEventSpy spy;
    QAccessible::setActive(false);
    if (QAccessible::isActive())
        QSKIP("platform keeps QAccessible active; cannot test the inactive path");
    {
        Markoff::UndoLog::Transaction t(doc.d2UndoLog());
        doc.d2RemoveBlock(blocks[1], t);
    }
    doc.flushPendingD2Changed();
    QCOMPARE(spy.count(), 0);
    QCOMPARE(root->childCount(), 2);
    QVERIFY(!QAccessible::accessibleInterface(id1));  // still evicted

    // Text edits while inactive: snapshots stay current, so the first edit
    // after re-activation reports only ITS delta.
    QAccessibleInterface *b0 = blockOf(view, 0);
    view.setCaretPosition(blocks[0], 0);
    QTest::keyClick(&view, 'a');
    doc.flushPendingD2Changed();
    QCOMPARE(spy.count(), 0);
    QAccessible::setActive(true);
    QTest::keyClick(&view, 'b');
    doc.flushPendingD2Changed();
    const auto ins = typed(spy, QAccessible::TextInserted, b0);
    QCOMPARE(ins.size(), 1);
    QCOMPARE(ins[0].a, 1);
    QCOMPARE(ins[0].text, QStringLiteral("b"));
}

void TstCanvasAccessibility::eviction_document_swap_releases_every_block()
{
    MarkoffDocument docA, docB;
    docA.loadFromMarkdown(threeParagraphFixture());
    docB.loadFromMarkdown("One.\n\nTwo.\n");
    View view;
    attachAndExpose(view, docA);
    QAccessibleInterface *root = QAccessible::queryAccessibleInterface(&view);
    QList<QAccessible::Id> ids;
    for (int i = 0; i < 3; ++i)
        ids << QAccessible::uniqueId(blockOf(view, i));

    MarkoffTest::A11yEventSpy spy;
    view.setDocument(&docB);
    QCOMPARE(spy.eventsOfType(QAccessible::ObjectDestroyed).size(), 3);
    for (QAccessible::Id id : ids)
        QVERIFY(!QAccessible::accessibleInterface(id));
    QCOMPARE(root->childCount(), 2);
    view.setDocument(nullptr);
    QCOMPARE(root->childCount(), 0);
}

void TstCanvasAccessibility::eviction_view_destruction_after_churn_is_clean()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    {
        View view;
        attachAndExpose(view, doc);
        const auto blocks = doc.iterateBlocks();
        for (int i = 0; i < 3; ++i)
            blockOf(view, i);
        view.setCaretPosition(blocks[1], 0);
        QTest::keyClick(&view, Qt::Key_Backspace);  // merge
        QTest::keyClick(&view, Qt::Key_Return);     // split
        for (int i = 0; i < 3; ++i)
            blockOf(view, i);
    }  // ~View: container + surviving blocks released once each
    QVERIFY(true);
}

// ---- A4.1: folding ------------------------------------------------------

namespace {
const char *kFoldFixture =
    "# Section One\n"
    "para one\n\n"
    "para two\n\n"
    "# Section Two\n"
    "para three\n";

enum class StFlag { Any, Expanded, Expandable, Invisible };

int stateEventsWith(const MarkoffTest::A11yEventSpy &spy, QAccessibleInterface *iface,
                    StFlag flag = StFlag::Any)
{
    int n = 0;
    for (const auto &r : spy.eventsOfType(QAccessible::StateChanged, iface)) {
        const auto &c = r.changedStates;
        const bool hit = flag == StFlag::Any
            || (flag == StFlag::Expanded && c.expanded)
            || (flag == StFlag::Expandable && c.expandable)
            || (flag == StFlag::Invisible && c.invisible);
        if (hit)
            ++n;
    }
    return n;
}
}  // namespace

void TstCanvasAccessibility::fold_head_state_and_toggle_event()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(kFoldFixture);
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *h1 = blockOf(view, 0);
    QAccessibleInterface *h2 = blockOf(view, 3);
    QAccessibleInterface *p1 = blockOf(view, 1);

    QVERIFY(h1->state().expandable);
    QVERIFY(h1->state().expanded);
    QVERIFY(!h1->state().collapsed);
    QVERIFY(!p1->state().expandable);   // a plain paragraph is not a fold head

    MarkoffTest::A11yEventSpy spy;
    view.toggleFold(blocks[0]);
    QVERIFY(h1->state().expandable);
    QVERIFY(!h1->state().expanded);
    QVERIFY(h1->state().collapsed);
    QCOMPARE(stateEventsWith(spy, h1, StFlag::Expanded), 1);
    QCOMPARE(stateEventsWith(spy, h1), 1);            // nothing else on the head
    QCOMPARE(stateEventsWith(spy, h2), 0);            // unaffected head: silent

    spy.clear();
    view.toggleFold(blocks[0]);                        // unfold reverses
    QVERIFY(h1->state().expanded);
    QVERIFY(!h1->state().collapsed);
    QCOMPARE(stateEventsWith(spy, h1, StFlag::Expanded), 1);
}

void TstCanvasAccessibility::fold_body_invisible_events_and_stable_children()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(kFoldFixture);
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *root = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *p1 = blockOf(view, 1);
    QAccessibleInterface *p2 = blockOf(view, 2);
    QAccessibleInterface *h2 = blockOf(view, 3);
    const int count = root->childCount();
    QCOMPARE(count, 5);
    QVector<QAccessibleInterface *> before;
    for (int i = 0; i < count; ++i)
        before << root->child(i);

    MarkoffTest::A11yEventSpy spy;
    view.toggleFold(blocks[0]);
    QCOMPARE(root->childCount(), count);
    for (int i = 0; i < count; ++i) {
        QCOMPARE(root->child(i), before[i]);
        QCOMPARE(root->indexOfChild(before[i]), i);
    }
    QVERIFY(p1->state().invisible);
    QVERIFY(p2->state().invisible);
    QVERIFY(!h2->state().invisible);
    QCOMPARE(stateEventsWith(spy, p1, StFlag::Invisible), 1);
    QCOMPARE(stateEventsWith(spy, p2, StFlag::Invisible), 1);
    QCOMPARE(stateEventsWith(spy, h2), 0);

    spy.clear();
    view.toggleFold(blocks[0]);
    QCOMPARE(root->childCount(), count);
    QVERIFY(!p1->state().invisible);
    QVERIFY(!p2->state().invisible);
    QCOMPARE(stateEventsWith(spy, p1, StFlag::Invisible), 1);
    QCOMPARE(stateEventsWith(spy, p2, StFlag::Invisible), 1);
}

void TstCanvasAccessibility::fold_action_toggles_and_readonly_does_not_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(kFoldFixture);
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *h1 = blockOf(view, 0);
    QAccessibleActionInterface *act = h1->actionInterface();
    QVERIFY(act);
    QCOMPARE(act->actionNames(), QStringList{QAccessibleActionInterface::toggleAction()});
    QVERIFY(!act->localizedActionName(QAccessibleActionInterface::toggleAction()).isEmpty());
    const QString collapseDesc =
        act->localizedActionDescription(QAccessibleActionInterface::toggleAction());
    QVERIFY(!collapseDesc.isEmpty());

    MarkoffTest::A11yEventSpy spy;
    act->doAction(QAccessibleActionInterface::toggleAction());
    QVERIFY(view.isBlockFolded(blocks[0]));
    QVERIFY(!h1->state().expanded);
    QCOMPARE(stateEventsWith(spy, h1, StFlag::Expanded), 1);
    // description reflects the new direction
    QVERIFY(act->localizedActionDescription(QAccessibleActionInterface::toggleAction())
            != collapseDesc);

    // Unknown action names are ignored.
    act->doAction(QStringLiteral("no-such-action"));
    QVERIFY(view.isBlockFolded(blocks[0]));

    // Read-only does not block folding: fold is view state, not a mutation.
    view.setReadOnly(true);
    act->doAction(QAccessibleActionInterface::toggleAction());
    QVERIFY(!view.isBlockFolded(blocks[0]));
    QVERIFY(h1->state().expanded);
    act->doAction(QAccessibleActionInterface::toggleAction());
    QVERIFY(view.isBlockFolded(blocks[0]));
}

void TstCanvasAccessibility::fold_non_foldable_has_no_action()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(kFoldFixture);
    View view;
    attachAndExpose(view, doc);
    QAccessibleInterface *p1 = blockOf(view, 1);
    QVERIFY(p1->actionInterface() == nullptr);
    QVERIFY(!p1->state().expandable);
    QVERIFY(!p1->state().expanded);
    QVERIFY(!p1->state().collapsed);
}

void TstCanvasAccessibility::fold_restore_via_set_folded_head_indices()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(kFoldFixture);
    View view;
    attachAndExpose(view, doc);
    QAccessibleInterface *root = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *h1 = blockOf(view, 0);
    QAccessibleInterface *p1 = blockOf(view, 1);
    QAccessibleInterface *h2 = blockOf(view, 3);
    QAccessibleInterface *p3 = blockOf(view, 4);

    MarkoffTest::A11yEventSpy spy;
    view.setFoldedHeadIndices({0, 3});
    QCOMPARE(root->childCount(), 5);
    QVERIFY(!h1->state().expanded);
    QVERIFY(!h2->state().expanded);
    QVERIFY(p1->state().invisible);
    QVERIFY(p3->state().invisible);
    QCOMPARE(stateEventsWith(spy, h1, StFlag::Expanded), 1);
    QCOMPARE(stateEventsWith(spy, h2, StFlag::Expanded), 1);
    QCOMPARE(stateEventsWith(spy, p3, StFlag::Invisible), 1);

    spy.clear();
    view.setFoldedHeadIndices({3});   // partial restore: only h1 reopens
    QVERIFY(h1->state().expanded);
    QVERIFY(!p1->state().invisible);
    QVERIFY(p3->state().invisible);
    QCOMPARE(stateEventsWith(spy, h1, StFlag::Expanded), 1);
    QCOMPARE(stateEventsWith(spy, h2), 0);      // unchanged: silent
    QCOMPARE(stateEventsWith(spy, p3), 0);
}

void TstCanvasAccessibility::fold_caret_moved_out_of_body_emits_caret_events()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(kFoldFixture);
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *h1 = blockOf(view, 0);
    QAccessibleInterface *p1 = blockOf(view, 1);
    view.setCaretPosition(blocks[1], 3);
    QCOMPARE(view.caretBlock(), blocks[1]);

    MarkoffTest::A11yEventSpy spy;
    view.toggleFold(blocks[0]);
    // Existing View behavior: caret lands on the head at offset 0.
    QCOMPARE(view.caretBlock(), blocks[0]);
    const auto cur = spy.eventsOfType(QAccessible::TextCaretMoved, h1);
    QCOMPARE(cur.size(), 1);
    QCOMPARE(cur.first().a, 0);
    QCOMPARE(spy.countOfType(QAccessible::TextCaretMoved, p1), 0);
    QVERIFY(!p1->state().focused);
    QVERIFY(h1->state().focused);
}

void TstCanvasAccessibility::fold_hidden_title_invisible_never_expandable()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("# My Title\n\nbody one\n\nbody two\n");
    View view;
    attachAndExpose(view, doc);
    QAccessibleInterface *root = QAccessible::queryAccessibleInterface(&view);
    QAccessibleInterface *title = blockOf(view, 0);
    QVERIFY(title->state().expandable);        // foldable heading until hidden

    MarkoffTest::A11yEventSpy spy;
    view.setHideMatchingFirstHeadingAsTitle(true);
    view.setInlineTitle(QStringLiteral("My Title"));
    QCOMPARE(root->childCount(), 3);           // stays in the child list (D2)
    QVERIFY(title->state().invisible);
    QVERIFY(!title->state().expandable);       // never foldable while hidden
    QVERIFY(title->actionInterface() == nullptr);
    QCOMPARE(stateEventsWith(spy, title, StFlag::Invisible), 1);
    QCOMPARE(stateEventsWith(spy, title, StFlag::Expandable), 1);
}

void TstCanvasAccessibility::fold_edit_changing_foldability_announces_expandable()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("plain\n\nsecond\n");
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b0 = blockOf(view, 0);
    QVERIFY(!b0->state().expandable);

    MarkoffTest::A11yEventSpy spy;
    view.setCaretPosition(blocks[0], 0);
    QTest::keyClicks(&view, "# ");             // promote to a heading
    doc.flushPendingD2Changed();
    if (view.isBlockFoldable(blocks[0])) {
        QVERIFY(b0->state().expandable);
        QCOMPARE(stateEventsWith(spy, b0, StFlag::Expandable), 1);
    } else {
        QSKIP("typed '# ' did not promote to a foldable heading in this build");
    }
}

void TstCanvasAccessibility::editable_insert_mid_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b1 = blockOf(view, 1);
    QAccessibleEditableTextInterface *ed = b1->editableTextInterface();
    QVERIFY(ed != nullptr);
    ed->insertText(6, QStringLiteral("XY"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("SecondXY paragraph."));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("First paragraph."));
    // Caret ends after the inserted text, in this block.
    QCOMPARE(view.caretBlock(), blocks[1]);
    QCOMPARE(b1->textInterface()->cursorPosition(), 8);
}

void TstCanvasAccessibility::editable_delete_range()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b1 = blockOf(view, 1);
    b1->editableTextInterface()->deleteText(0, 7);  // "Second "
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("paragraph."));
    QCOMPARE(b1->textInterface()->cursorPosition(), 0);
}

void TstCanvasAccessibility::editable_replace_multibyte_offsets()
{
    // "a" + emoji (2 QChars, 4 bytes) + "é" (1 QChar, 2 bytes) + "z".
    MarkoffDocument doc;
    doc.loadFromMarkdown(QString::fromUtf8("a\xF0\x9F\x98\x80\xC3\xA9z\n").toUtf8());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b0 = blockOf(view, 0);
    QCOMPARE(b0->textInterface()->characterCount(), 5);
    QAccessibleEditableTextInterface *ed = b0->editableTextInterface();
    ed->replaceText(1, 3, QStringLiteral("X"));  // the emoji
    QCOMPARE(QString::fromUtf8(doc.blockText(blocks[0])), QString::fromUtf8("aX\xC3\xA9z"));
    ed->insertText(3, QString::fromUtf8("\xF0\x9F\x98\x80"));  // after "é"
    QCOMPARE(QString::fromUtf8(doc.blockText(blocks[0])),
             QString::fromUtf8("aX\xC3\xA9\xF0\x9F\x98\x80z"));
    ed->deleteText(3, 5);  // remove the emoji again
    QCOMPARE(QString::fromUtf8(doc.blockText(blocks[0])), QString::fromUtf8("aX\xC3\xA9z"));
}

void TstCanvasAccessibility::editable_read_only_rejects_all_three()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleInterface *b1 = blockOf(view, 1);
    view.setCaretPosition(blocks[0], 2);
    view.setReadOnly(true);
    MarkoffTest::A11yEventSpy spy;
    QAccessibleEditableTextInterface *ed = b1->editableTextInterface();
    QVERIFY(ed != nullptr);
    ed->insertText(2, QStringLiteral("XX"));
    ed->deleteText(0, 3);
    ed->replaceText(0, 3, QStringLiteral("QQ"));
    doc.flushPendingD2Changed();
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("Second paragraph."));
    QCOMPARE(spy.countOfType(QAccessible::TextInserted, b1), 0);
    QCOMPARE(spy.countOfType(QAccessible::TextRemoved, b1), 0);
    QCOMPARE(view.caretBlock(), blocks[0]);   // rejected before any caret move
    view.setReadOnly(false);
    ed->insertText(0, QStringLiteral("ok "));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("ok Second paragraph."));
}

void TstCanvasAccessibility::editable_one_undo_step_each()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleEditableTextInterface *ed = blockOf(view, 1)->editableTextInterface();
    const QByteArray orig = doc.blockText(blocks[1]);

    ed->insertText(6, QStringLiteral("XYZ"));   // multi-char: still one step
    QVERIFY(doc.blockText(blocks[1]) != orig);
    doc.d2UndoLog().undo();
    doc.flushPendingD2Changed();
    QCOMPARE(doc.blockText(blocks[1]), orig);

    ed->deleteText(0, 6);
    doc.d2UndoLog().undo();
    doc.flushPendingD2Changed();
    QCOMPARE(doc.blockText(blocks[1]), orig);

    ed->replaceText(0, 6, QStringLiteral("Other"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("Other paragraph."));
    doc.d2UndoLog().undo();
    doc.flushPendingD2Changed();
    QCOMPARE(doc.blockText(blocks[1]), orig);
}

void TstCanvasAccessibility::editable_events_fire_once_with_payload()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    QAccessibleInterface *b1 = blockOf(view, 1);
    QAccessibleEditableTextInterface *ed = b1->editableTextInterface();
    {
        MarkoffTest::A11yEventSpy spy;
        ed->insertText(6, QStringLiteral("XY"));
        const auto ins = typed(spy, QAccessible::TextInserted, b1);
        QCOMPARE(ins.size(), 1);
        QCOMPARE(ins[0].a, 6);
        QCOMPARE(ins[0].text, QStringLiteral("XY"));
        QCOMPARE(spy.countOfType(QAccessible::TextRemoved, b1), 0);
    }
    {
        MarkoffTest::A11yEventSpy spy;
        ed->deleteText(6, 8);
        const auto rem = typed(spy, QAccessible::TextRemoved, b1);
        QCOMPARE(rem.size(), 1);
        QCOMPARE(rem[0].a, 6);
        QCOMPARE(rem[0].text, QStringLiteral("XY"));
        QCOMPARE(spy.countOfType(QAccessible::TextInserted, b1), 0);
    }
    {
        MarkoffTest::A11yEventSpy spy;
        ed->replaceText(0, 6, QStringLiteral("Other"));
        const auto rem = typed(spy, QAccessible::TextRemoved, b1);
        const auto ins = typed(spy, QAccessible::TextInserted, b1);
        QCOMPARE(rem.size(), 1);
        QCOMPARE(rem[0].text, QStringLiteral("Second"));
        QCOMPARE(ins.size(), 1);
        QCOMPARE(ins[0].text, QStringLiteral("Other"));
    }
}

void TstCanvasAccessibility::editable_invalid_ranges_rejected()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown(threeParagraphFixture());
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleEditableTextInterface *ed = blockOf(view, 1)->editableTextInterface();
    const QByteArray orig = doc.blockText(blocks[1]);
    ed->insertText(-1, QStringLiteral("X"));
    ed->insertText(999, QStringLiteral("X"));
    ed->deleteText(3, 2);                 // inverted
    ed->deleteText(0, 999);
    ed->replaceText(-5, 2, QStringLiteral("X"));
    ed->insertText(2, QString());         // empty no-op
    doc.flushPendingD2Changed();
    QCOMPARE(doc.blockText(blocks[1]), orig);
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("First paragraph."));
    // Boundary offsets are valid: insert at 0 and at the end.
    ed->insertText(0, QStringLiteral(">"));
    ed->insertText(int(orig.size()) + 1, QStringLiteral("<"));
    QCOMPARE(doc.blockText(blocks[1]), QByteArray(">Second paragraph.<"));
}

void TstCanvasAccessibility::editable_newline_rejected_outside_code_block()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("para\n\n```\ncode\n```\n");
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QAccessibleEditableTextInterface *p = blockOf(view, 0)->editableTextInterface();
    p->insertText(2, QStringLiteral("a\nb"));
    QCOMPARE(doc.blockText(blocks[0]), QByteArray("para"));
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::CodeBlock);
    QAccessibleEditableTextInterface *c = blockOf(view, 1)->editableTextInterface();
    QVERIFY(c != nullptr);
    c->insertText(0, QStringLiteral("x\n"));
    QVERIFY(doc.blockText(blocks[1]).startsWith("x\n"));
}

void TstCanvasAccessibility::editable_interface_absent_for_no_text_kinds()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("para\n\n---\n\nafter\n");
    View view;
    attachAndExpose(view, doc);
    const auto blocks = doc.iterateBlocks();
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::HorizontalRule);
    QVERIFY(blockOf(view, 1)->editableTextInterface() == nullptr);
    QVERIFY(blockOf(view, 0)->editableTextInterface() != nullptr);
}

QTEST_MAIN(TstCanvasAccessibility)
#include "tst_canvas_accessibility.moc"
