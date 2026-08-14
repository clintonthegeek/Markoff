// SPDX-License-Identifier: GPL-3.0-or-later
//
// P5.5 — callouts + frontmatter + footnote defs.
//
//   - Callout blockquotes (`> [!note]`): BlockPresentation::presentationFor
//     reparses a BlockQuote's own buffer (CalloutBlocks::parseCallout, same
//     "canvas-local rule" precedent as MediaBlocks/CodeHighlighting — no
//     parser/core concept of a callout exists) into a typed icon+label
//     header band (View::paintEvent's style.isCallout branch) plus a wider
//     leftIndent than a plain quote's ("body indent"). View::isCalloutBlock
//     is a test/inspection-only accessor added alongside the P5.4 precedent
//     (isEmbedPlaceholderActive, etc.).
//   - Frontmatter: a leading NON-BLOCK band (View::frontmatterBandHeight/
//     paintFrontmatter/leadingBandHeight), same shape P4.9's inline-title
//     band established — frontmatter has no BlockId at all
//     (markoff-parser's `Document::extract` genuinely strips it out of
//     `extracted.body` before block parsing), so "caret-inside reveals raw
//     YAML" is implemented as a click-to-toggle (m_frontmatterExpanded)
//     rather than a real text caret.
//   - Footnote definitions: turned out to NOT need band treatment — a
//     surprise this test file's first draft caught. `Document::extract`
//     only COPIES footnote-def lines into its `footnotes` list for
//     numbering; it does not remove them from `extracted.body`, so
//     `[^1]: text` is a completely ordinary `BlockKind::Paragraph` block
//     with a real BlockId. Back-reference styling is therefore just
//     another BlockPresentation::presentationFor per-kind case
//     (FootnoteDefBlocks::parseFootnoteDef) — the marker-decoration slot
//     shows `[^label]`, Link-slot color + italic distinguish the block
//     from a plain paragraph. No new View.cpp paint path needed.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::View;
using Markoff::MarkoffDocument;
using Markoff::Theme;

class TstCanvasSideContent : public QObject {
    Q_OBJECT

private slots:
    void callout_note_is_flagged_and_taller_than_plain_quote();
    void callout_body_indent_exceeds_plain_quote_indent();
    void plain_blockquote_is_not_a_callout();
    void callout_types_map_to_distinct_theme_colors();
    void frontmatter_collapsed_shows_properties_band();
    void frontmatter_click_toggles_expanded_raw_view();
    void no_frontmatter_means_zero_band_height();
    void footnote_def_block_is_flagged_distinct_from_paragraph();
    void footnote_ref_paragraph_is_not_flagged();
};

void TstCanvasSideContent::callout_note_is_flagged_and_taller_than_plain_quote()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("> [!note] this is body text\n\nPlain paragraph.\n");
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::BlockQuote);
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::Paragraph);
    QVERIFY(view.isCalloutBlock(blocks[0]));
    QVERIFY(!view.isCalloutBlock(blocks[1]));

    // A callout block reserves header-band space above its own text start
    // (BlockPresentation's isCallout branch inflates topMargin), so it is
    // measurably TALLER than a same-content plain blockquote.
    const QRectF calloutRect = view.blockRect(blocks[0]);

    MarkoffDocument plainDoc;
    plainDoc.loadFromMarkdown("> plain quote body text\n");
    View plainView;
    plainView.resize(500, 400);
    plainView.setDocument(&plainDoc);
    plainView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&plainView));
    const BlockId plainBlock = plainDoc.iterateBlocks().front();
    QVERIFY(!plainView.isCalloutBlock(plainBlock));
    const QRectF plainRect = plainView.blockRect(plainBlock);
    QVERIFY(calloutRect.height() > plainRect.height());
}

// "Body indent" (plan wording): a callout's content starts further right
// than a plain quote's own depth-1 indent — verified via caretRect() at
// byte 0, which includes leftIndent (blockRect().x() does not).
void TstCanvasSideContent::callout_body_indent_exceeds_plain_quote_indent()
{
    MarkoffDocument calloutDoc;
    calloutDoc.loadFromMarkdown("> [!note] this is body text\n");
    View calloutView;
    calloutView.resize(500, 400);
    calloutView.setDocument(&calloutDoc);
    calloutView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&calloutView));

    MarkoffDocument plainDoc;
    plainDoc.loadFromMarkdown("> plain quote body text\n");
    View plainView;
    plainView.resize(500, 400);
    plainView.setDocument(&plainDoc);
    plainView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&plainView));

    const BlockId calloutBlock = calloutDoc.iterateBlocks().front();
    const BlockId plainBlock = plainDoc.iterateBlocks().front();
    calloutView.setCaretPosition(calloutBlock, 0);
    plainView.setCaretPosition(plainBlock, 0);
    QCOMPARE(calloutView.caretBlock(), calloutBlock);
    QCOMPARE(plainView.caretBlock(), plainBlock);

    QVERIFY(calloutView.caretRect().x() > plainView.caretRect().x());
}

// Falsification target: a plain blockquote (no `[!type]` marker) must NOT
// be flagged as a callout — CalloutBlocks::parseCallout must actually gate
// on the marker, not fire for every BlockQuote.
void TstCanvasSideContent::plain_blockquote_is_not_a_callout()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("> just a normal quote, no marker\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockKind(block), BlockKind::BlockQuote);
    QVERIFY(!view.isCalloutBlock(block));
}

void TstCanvasSideContent::callout_types_map_to_distinct_theme_colors()
{
    // Distinct Theme::Slot colors per type (Theme.cpp's new CalloutNote/
    // Warning/Tip/Important/Caution defaults, P5.5) — the ground truth
    // View::paintEvent's style.isCallout branch reads its header/bar color
    // from (BlockPresentation.cpp: `theme.color(callout.slot)`).
    const Theme theme = Theme::defaultLight();
    QVERIFY(theme.color(Theme::Slot::CalloutNote).isValid());
    QVERIFY(theme.color(Theme::Slot::CalloutWarning).isValid());
    QVERIFY(theme.color(Theme::Slot::CalloutTip).isValid());
    QVERIFY(theme.color(Theme::Slot::CalloutImportant).isValid());
    QVERIFY(theme.color(Theme::Slot::CalloutCaution).isValid());
    QVERIFY(theme.color(Theme::Slot::CalloutNote) != theme.color(Theme::Slot::CalloutWarning));
    QVERIFY(theme.color(Theme::Slot::CalloutWarning) != theme.color(Theme::Slot::CalloutTip));
    QVERIFY(theme.color(Theme::Slot::CalloutNote) != theme.color(Theme::Slot::CalloutTip));

    const Theme dark = Theme::defaultDark();
    QVERIFY(dark.color(Theme::Slot::CalloutNote).isValid());
    QVERIFY(dark.color(Theme::Slot::CalloutNote) != dark.color(Theme::Slot::CalloutWarning));
}

void TstCanvasSideContent::frontmatter_collapsed_shows_properties_band()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("---\ntitle: Hello\ntags: x\n---\n\nBody paragraph.\n");
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QVERIFY(doc.frontmatterValue(QByteArrayLiteral("raw")).has_value());

    // documentHeight() folds the leading band in (scroll-range
    // correctness) — strictly greater than the single real block's own
    // height, since there is a frontmatter band ahead of it.
    QVERIFY(view.documentHeight() > view.blockRect(doc.iterateBlocks().front()).height());
}

void TstCanvasSideContent::frontmatter_click_toggles_expanded_raw_view()
{
    MarkoffDocument doc;
    // `tags:` has no scalar value on its own line — parseFrontmatterProperties
    // still counts it as a recognized top-level row (key="tags", value=""),
    // but the two INDENTED list-item lines under it are skipped (not
    // top-level `key: value` shape) — collapsed sees 3 rows (title, tags,
    // author), expanded sees all 5 raw lines, so the two heights are
    // guaranteed to differ for this fixture (unlike a flat all-scalar
    // frontmatter, where rows-parsed == raw-lines by coincidence).
    doc.loadFromMarkdown(
        "---\ntitle: Hello\ntags:\n  - x\n  - y\nauthor: Me\n---\n\nBody.\n");
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const qreal collapsedDocHeight = view.documentHeight();
    QVERIFY(collapsedDocHeight > 0.0);

    // Click inside the band (near the top — no title band in this fixture,
    // so document y=0 is the frontmatter band's own top; any real band is
    // taller than a handful of DIPs) toggles reveal — same "caret/click
    // reveals source" role code-fence/math per-block reveal plays for a
    // real block, adapted to a click since this band has no BlockId for a
    // caret to enter (plan wording: "caret-inside reveals raw YAML").
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(10, 5));

    const qreal expandedDocHeight = view.documentHeight();
    QVERIFY(expandedDocHeight != collapsedDocHeight);

    // Click again: toggles back.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(10, 5));
    QCOMPARE(view.documentHeight(), collapsedDocHeight);
}

void TstCanvasSideContent::no_frontmatter_means_zero_band_height()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Just a paragraph, no frontmatter.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QVERIFY(!doc.frontmatterValue(QByteArrayLiteral("raw")).has_value());
    // No title band (off by default) and no frontmatter: documentHeight()
    // is exactly the single block's own height — no leading band at all.
    QCOMPARE(view.documentHeight(), view.blockRect(doc.iterateBlocks().front()).height());
}

// Falsification target: a footnote-definition Paragraph must render
// distinctly (marker + color + italic) from an ordinary Paragraph — a
// FootnoteDefBlocks::parseFootnoteDef that never fires (or fires for
// everything) would collapse this distinction.
void TstCanvasSideContent::footnote_def_block_is_flagged_distinct_from_paragraph()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Body text with a ref[^1].\n\n[^1]: The footnote content.\n");
    View view;
    view.resize(500, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // Surprise this test file's first draft caught (see the header
    // comment): the footnote-def line is NOT stripped out of the parsed
    // body — it is a real, second `BlockKind::Paragraph` block.
    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));
    QCOMPARE(doc.blockKind(blocks[0]), BlockKind::Paragraph);
    QCOMPARE(doc.blockKind(blocks[1]), BlockKind::Paragraph);
    QCOMPARE(doc.blockText(blocks[1]), QByteArray("[^1]: The footnote content."));

    QVERIFY(!view.isFootnoteDefBlock(blocks[0]));
    QVERIFY(view.isFootnoteDefBlock(blocks[1]));
}

void TstCanvasSideContent::footnote_ref_paragraph_is_not_flagged()
{
    MarkoffDocument doc;
    doc.loadFromMarkdown("Plain paragraph, no footnote marker here.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    QCOMPARE(doc.blockKind(blocks.front()), BlockKind::Paragraph);
    QVERIFY(!view.isFootnoteDefBlock(blocks.front()));
}

QTEST_MAIN(TstCanvasSideContent)
#include "tst_canvas_side_content.moc"
