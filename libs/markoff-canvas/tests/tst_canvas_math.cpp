// SPDX-License-Identifier: GPL-3.0-or-later
//
// P5.3 — math blocks (jkqtmathtext). Two rendering surfaces, per the
// plan's own wording:
//   - Display Math (`$$...$$`, BlockKind::Math with DisplayMode true):
//     BlockLayoutCache::rebuildInline renders a jkqtmathtext QPixmap
//     whenever the caret is NOT in the block; View::paintEvent paints it
//     instead of the raw-source text layout. Caret-in-block reveals the
//     raw LaTeX source instead (same per-block reveal trigger as the
//     code-fence mechanism, P2.2) — the pixmap is dropped and the normal
//     text layout paints, delimiters and all.
//   - Inline `$...$` math spans inside a Paragraph: QTextLayout has no
//     inline-object-replacement path without a backing QTextDocument (C3
//     forbids one), so these render as a styled inline run instead
//     (monospace + Theme::Slot::Math foreground + CodeBlockBackground
//     background — "code-like" per the plan text), never a pixmap.
//
// Falsification target (plan's own protocol): see the plan's findings log
// entry for the throwaway commit SHAs that broke
// display_math_shows_pixmap_only_while_caret_is_outside_the_block.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>

using Markoff::BlockId;
using Markoff::BlockKind;
using Markoff::Canvas::View;
using Markoff::Theme;

namespace {

// Slot::Math has no explicit color in Theme::defaultLight()/defaultDark()
// (falls through to TextDefault) — same gap tst_canvas_code_highlight.cpp
// worked around for the Code* slots. Give it a distinct color so the
// styling assertions below actually exercise the wiring, not a
// coincidental TextDefault match.
Theme themeWithMathColor()
{
    Theme t = Theme::defaultLight();
    t.setColor(Theme::Slot::Math, QColor("#ff00ff"));
    return t;
}

/// Place the caret at byte 0 of `block` by clicking its rect, the way a
/// user would (same helper shape as tst_canvas_kind_transition.cpp's).
bool clickAtBlockStart(View &view, BlockId block)
{
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 1, int(rect.y()) + 8));
    return view.caretBlock() == block && view.caretByteOffset() == 0;
}

}  // namespace

class TstCanvasMath : public QObject {
    Q_OBJECT

private slots:
    void inline_dollar_span_gets_code_like_math_styling();
    void inline_dollar_delimiters_hidden_until_caret_enters_block();
    void display_math_shows_pixmap_only_while_caret_is_outside_the_block();
};

void TstCanvasMath::inline_dollar_span_gets_code_like_math_styling()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Text with $x^2$ math.\n");

    View view;
    view.resize(400, 300);
    view.setTheme(themeWithMathColor());
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    const QByteArray text = doc.blockText(block);
    const int xByte = text.indexOf("x^2");
    QVERIFY(xByte >= 0);

    // No caret in this block yet — content span still styled regardless of
    // the delimiters' own reveal state (a shown OR hidden delimiter run
    // doesn't change what the surrounding math content looks like).
    const Theme theme = view.theme();
    QCOMPARE(view.codeTokenColorAt(block, xByte), theme.color(Theme::Slot::Math));

    // Plain surrounding text stays untouched.
    const int textByte = text.indexOf("Text");
    QVERIFY(textByte >= 0);
    QVERIFY(!view.codeTokenColorAt(block, textByte).isValid());
}

void TstCanvasMath::inline_dollar_delimiters_hidden_until_caret_enters_block()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Text with $x^2$ math.\n\nOther paragraph.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2);
    const BlockId mathParagraph = blocks[0];
    const BlockId other         = blocks[1];

    const QByteArray text = doc.blockText(mathParagraph);
    const int dollarByte = text.indexOf('$');
    QVERIFY(dollarByte >= 0);

    // Realize the block AND put the caret in the OTHER block (not here) —
    // isDelimiterHiddenAt is only meaningful once the block has a real
    // layout to query (see tst_canvas_inline_formatting.cpp's own note),
    // and this doubles as "caret nowhere near this block": same per-block
    // reveal rule as code fences (P2.2) — hidden by default.
    QVERIFY(clickAtBlockStart(view, other));
    QCOMPARE(view.realizedBlockCount(), 2);
    QVERIFY(view.isDelimiterHiddenAt(mathParagraph, dollarByte));

    // Caret enters the block (anywhere in it, not just adjacent to the
    // '$' — the per-block rule, not the generic per-span parent-range
    // one): delimiters reveal.
    QVERIFY(clickAtBlockStart(view, mathParagraph));
    QVERIFY(!view.isDelimiterHiddenAt(mathParagraph, dollarByte));

    // Caret leaves again: hidden once more.
    QVERIFY(clickAtBlockStart(view, other));
    QVERIFY(view.isDelimiterHiddenAt(mathParagraph, dollarByte));
}

void TstCanvasMath::display_math_shows_pixmap_only_while_caret_is_outside_the_block()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n\nSecond.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2);
    const BlockId first  = blocks[0];
    const BlockId second = blocks[1];

    // Type "$$x^2$$" at the start of the first block, promoting it to
    // display Math (same typing sequence as
    // tst_canvas_kind_transition.cpp's typing_second_dollar_sets_display_
    // mode). The caret is still IN the block right after typing — source
    // must stay revealed (no pixmap) even though it's now display math.
    QVERIFY(clickAtBlockStart(view, first));
    QTest::keyClicks(&view, QStringLiteral("$$x^2$$"));
    QCOMPARE(doc.blockKind(first), BlockKind::Math);
    QVERIFY(!view.isMathPixmapActive(first));

    // Move the caret to the other block: the math block's source is no
    // longer being edited, so it should switch to the rendered pixmap.
    QVERIFY(clickAtBlockStart(view, second));
    QVERIFY(view.isMathPixmapActive(first));

    // And back: caret re-entering drops the pixmap, reveals source again.
    // Not via clickAtBlockStart(): while the pixmap is active, this
    // block's "$$" prefix is a hidden delimiter run (per-block math
    // reveal, same mechanism as code fences) — OMITTED from the layout
    // text entirely, so a click near the left edge lands on the first
    // VISIBLE character (past the "$$"), not byte 0.
    const QRectF rect = view.blockRect(first);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 1, int(rect.y()) + 8));
    QCOMPARE(view.caretBlock(), first);
    QVERIFY(!view.isMathPixmapActive(first));
}

QTEST_MAIN(TstCanvasMath)
#include "tst_canvas_math.moc"
