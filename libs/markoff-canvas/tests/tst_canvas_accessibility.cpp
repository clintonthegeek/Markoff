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

#include <markoff/canvas/View.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>

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

QTEST_MAIN(TstCanvasAccessibility)
#include "tst_canvas_accessibility.moc"
