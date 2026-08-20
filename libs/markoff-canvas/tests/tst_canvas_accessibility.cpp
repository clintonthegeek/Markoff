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

QTEST_MAIN(TstCanvasAccessibility)
#include "tst_canvas_accessibility.moc"
