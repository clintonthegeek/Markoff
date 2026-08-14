// SPDX-License-Identifier: GPL-3.0-or-later
//
// T7 — inline spans + delimiter visibility (exit E7).
//
// Assertions run against the production widget (Markoff::Canvas::View)
// through its real event path: show it, click/arrow-key to place the
// caret, drive real QTest::keyClicks. No test-only render or edit entry
// point — View::isDelimiterHiddenAt() only inspects the layout state the
// production paint path already reads (spec §7, invariant 5).

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

class TstCanvasInlineFormatting : public QObject {
    Q_OBJECT

private slots:
    void delimiter_visibility_follows_caret();
    void heading_marker_hides_per_block();
    void code_fence_hides_per_block();
};

void TstCanvasInlineFormatting::delimiter_visibility_follows_caret()
{
    Markoff::MarkoffDocument doc;
    // Byte offsets: a=0, ' '=1, '*'=2, '*'=3, b=4, '*'=5, '*'=6, ' '=7, c=8.
    doc.loadFromMarkdown("a **b** c\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId block = doc.iterateBlocks().front();
    QCOMPARE(doc.blockText(block), QByteArray("a **b** c"));

    // Click at byte 0, then walk the caret to byte 0 exactly via Home —
    // the caret starts outside the "**b**" span either way.
    const QRectF rect = view.blockRect(block);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 1, int(rect.y()) + 8));
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.caretBlock(), block);
    QCOMPARE(view.caretByteOffset(), 0);
    QCOMPARE(view.realizedBlockCount(), 1);  // isDelimiterHiddenAt below is only
                                              // meaningful once the block has a
                                              // real layout to query.

    // Caret outside the span: both "**" delimiter runs are hidden.
    QVERIFY(view.isDelimiterHiddenAt(block, 2));
    QVERIFY(view.isDelimiterHiddenAt(block, 5));

    // Walk the caret into the span, one ASCII char per Right (1 QChar ==
    // 1 byte for this whole fixture, so byte offset == keypress count).
    for (int i = 0; i < 4; ++i)
        QTest::keyClick(&view, Qt::Key_Right);
    QCOMPARE(view.caretByteOffset(), 4);  // right before 'b'

    // Caret inside the span: delimiters reveal.
    QVERIFY(!view.isDelimiterHiddenAt(block, 2));
    QVERIFY(!view.isDelimiterHiddenAt(block, 5));

    // Type while revealed; the buffer round-trips at the caret's byte
    // position like any other insert (T2's contract, unaffected by T7).
    QTest::keyClicks(&view, QStringLiteral("x"));
    QCOMPARE(doc.blockText(block), QByteArray("a **xb** c"));
    QCOMPARE(view.caretByteOffset(), 5);

    // Move the caret back out to byte 0: delimiters hide again.
    QTest::keyClick(&view, Qt::Key_Home);
    QCOMPARE(view.caretByteOffset(), 0);
    QVERIFY(view.isDelimiterHiddenAt(block, 2));
}

// P2.2 — heading prefix omission, per-BLOCK reveal (spec §4.2): unlike
// emphasis/strong, the "# " marker reveals when the caret is ANYWHERE in
// the heading line, not just adjacent to the marker's own two bytes —
// Obsidian parity. No canvas code change was needed for this case (the
// parser already gives the ATX marker span a parent range spanning the
// whole heading line — verified against
// tst_parser_inline_span_bake.cpp's heading_marker_has_parent_range_
// without_trailing_newline); this test is the canvas-side proof.
void TstCanvasInlineFormatting::heading_marker_hides_per_block()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("# Title\n\nOther paragraph\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2);
    const BlockId heading = blocks[0];
    const BlockId other   = blocks[1];
    QCOMPARE(doc.blockText(heading), QByteArray("# Title"));

    // Caret elsewhere entirely: the marker is hidden.
    const QRectF otherRect = view.blockRect(other);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(otherRect.x()) + 1, int(otherRect.y()) + 8));
    QCOMPARE(view.caretBlock(), other);
    QVERIFY(view.isDelimiterHiddenAt(heading, 0));

    // Click into the heading block, then walk to its END (byte 7, right
    // after "Title" — nowhere near the marker's own two bytes at [0,2)):
    // still revealed, because reveal is per-block here, not per-span.
    const QRectF headingRect = view.blockRect(heading);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(headingRect.x()) + 1, int(headingRect.y()) + 8));
    QTest::keyClick(&view, Qt::Key_End);
    QCOMPARE(view.caretBlock(), heading);
    QCOMPARE(view.caretByteOffset(), 7);
    QVERIFY(!view.isDelimiterHiddenAt(heading, 0));

    // Back out: hidden again.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(otherRect.x()) + 1, int(otherRect.y()) + 8));
    QVERIFY(view.isDelimiterHiddenAt(heading, 0));
}

// P2.2 — code fence omission, per-BLOCK reveal (spec §4.2): the opening
// fence's "```cpp" is a canvas-local special case (delimiterShouldHide),
// since the parser never gives fenced_code_block_delimiter/info_string
// spans a parent range at all (they aren't part of any inline tree) — so
// unlike headings, this one needed a real code change, not just a proof.
void TstCanvasInlineFormatting::code_fence_hides_per_block()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("```cpp\nint main() {}\n```\n\nOther\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), 2);
    const BlockId code  = blocks[0];
    const BlockId other = blocks[1];
    QVERIFY(doc.blockText(code).startsWith("```cpp"));

    // Caret elsewhere: the opening fence's backticks are hidden.
    const QRectF otherRect = view.blockRect(other);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(otherRect.x()) + 1, int(otherRect.y()) + 8));
    QCOMPARE(view.caretBlock(), other);
    QVERIFY(view.isDelimiterHiddenAt(code, 0));

    // Click into the code block itself (second line, "int main..."): the
    // caret is anywhere in the block, not adjacent to the fence glyphs —
    // still reveals, per-block.
    const QRectF codeRect = view.blockRect(code);
    const QFontMetricsF fm(view.theme().font(Markoff::Theme::FontRole::Monospace));
    const qreal lineHeight = fm.lineSpacing();
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(codeRect.x()) + 1, int(codeRect.y() + lineHeight) + 4));
    QCOMPARE(view.caretBlock(), code);
    QVERIFY(!view.isDelimiterHiddenAt(code, 0));

    // Back out: hidden again.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(otherRect.x()) + 1, int(otherRect.y()) + 8));
    QVERIFY(view.isDelimiterHiddenAt(code, 0));
}

QTEST_MAIN(TstCanvasInlineFormatting)
#include "tst_canvas_inline_formatting.moc"
