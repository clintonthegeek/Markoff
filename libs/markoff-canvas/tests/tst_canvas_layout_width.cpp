// SPDX-License-Identifier: GPL-3.0-or-later
//
// P4.5 — readable-line-width content column policy: FullWidth vs a
// centered FixedColumn (Obsidian "readable line length"), live-resizable,
// scroll stays anchored to the top-visible block across both a viewport
// resize and a live policy toggle.

#include <QScrollBar>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::ContentWidthPolicy;
using Markoff::Canvas::View;

namespace {

QByteArray manyBlocks(int n)
{
    QByteArray src;
    for (int i = 0; i < n; ++i) {
        src += "Paragraph number " + QByteArray::number(i)
             + " with enough words in it to occupy a line.\n\n";
    }
    return src;
}

}  // namespace

class TstCanvasLayoutWidth : public QObject {
    Q_OBJECT

private slots:
    void full_width_policy_spans_the_viewport();
    void fixed_column_centers_and_narrows();
    void fixed_column_shrinks_to_fit_a_narrow_viewport();
    void resize_relayouts_and_keeps_scroll_anchored();
    void live_policy_toggle_keeps_scroll_anchored();
};

void TstCanvasLayoutWidth::full_width_policy_spans_the_viewport()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("A short paragraph.\n");

    View view;
    view.resize(800, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QCOMPARE(view.contentWidthPolicy().kind, ContentWidthPolicy::FullWidth);

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(int(blocks.size()), 1);
    const QRectF r = view.blockRect(blocks.front());

    // FullWidth: the column fills the viewport minus the (small, fixed)
    // page margins on either side — nowhere near a 700px "readable" cap.
    // Margin widened 16 -> 28 in P5.6 (room for the fold-affordance glyph
    // alongside the marker/checkbox decoration slot); threshold bumped to
    // match — still "small relative to an 800px viewport", just not <20.
    QVERIFY2(r.width() > 700.0,
             qPrintable(QStringLiteral("FullWidth column is %1px, expected > 700")
                            .arg(r.width())));
    QVERIFY2(r.x() < 40.0,
             qPrintable(QStringLiteral("FullWidth column x is %1, expected near 0")
                            .arg(r.x())));
}

void TstCanvasLayoutWidth::fixed_column_centers_and_narrows()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("A short paragraph.\n");

    View view;
    view.resize(1000, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    view.setContentWidthPolicy(ContentWidthPolicy::fixedColumn(400.0));
    QCOMPARE(view.contentWidthPolicy().kind, ContentWidthPolicy::FixedColumn);
    QCOMPARE(view.contentWidthPolicy().fixedColumnWidth, 400.0);

    const auto blocks = doc.iterateBlocks();
    const QRectF r = view.blockRect(blocks.front());

    QVERIFY2(qAbs(r.width() - 400.0) < 0.5,
             qPrintable(QStringLiteral("fixed column width is %1, expected 400").arg(r.width())));

    // Centered: left margin (r.x()) must roughly equal the right margin
    // (viewport width - r.right()), and both must be well past the
    // minimum page margin FullWidth would use.
    const qreal leftMargin  = r.x();
    const qreal rightMargin = view.viewport()->width() - r.right();
    QVERIFY2(qAbs(leftMargin - rightMargin) < 1.0,
             qPrintable(QStringLiteral("left margin %1 vs right margin %2 — not centered")
                            .arg(leftMargin).arg(rightMargin)));
    QVERIFY2(leftMargin > 50.0,
             qPrintable(QStringLiteral("left margin %1 too small to be a centered 400px "
                                       "column in a 1000px viewport")
                            .arg(leftMargin)));
}

void TstCanvasLayoutWidth::fixed_column_shrinks_to_fit_a_narrow_viewport()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("A short paragraph.\n");

    View view;
    view.resize(300, 400);
    view.setDocument(&doc);
    view.setContentWidthPolicy(ContentWidthPolicy::fixedColumn(700.0));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const QRectF r = view.blockRect(blocks.front());

    // The viewport (300px) is narrower than the requested 700px column —
    // the column must shrink to fit rather than overflow it.
    QVERIFY2(r.width() < 300.0,
             qPrintable(QStringLiteral("fixed column %1px did not shrink to fit a 300px viewport")
                            .arg(r.width())));
    QVERIFY2(r.right() <= view.viewport()->width() + 0.5,
             qPrintable(QStringLiteral("column right edge %1 overflows viewport width %2")
                            .arg(r.right()).arg(view.viewport()->width())));
}

void TstCanvasLayoutWidth::resize_relayouts_and_keeps_scroll_anchored()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(manyBlocks(200));

    View view;
    view.resize(900, 400);
    view.setDocument(&doc);
    view.setContentWidthPolicy(ContentWidthPolicy::fixedColumn(700.0));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // Scroll partway into the document and note the anchor block.
    view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum() / 2);
    view.repaint();
    const auto [anchorIdxBefore, fractionBefore] = view.scrollAnchor();
    QVERIFY(anchorIdxBefore >= 0);
    const BlockId anchorBlock = view.blockIdAt(anchorIdxBefore);

    const QRectF beforeRect = view.blockRect(anchorBlock);

    // Narrow the viewport: the fixed column (700px) no longer fits at
    // full width, so it must shrink — a real relayout, not a no-op.
    view.resize(500, 400);
    view.repaint();

    const QRectF afterRect = view.blockRect(anchorBlock);
    QVERIFY2(qAbs(afterRect.width() - beforeRect.width()) > 1.0,
             "resize must relayout the realized entries at the new width");

    // The same block is still (at least approximately) at the top of the
    // viewport — the anchor must survive the resize, not just "some
    // block or other".
    const auto [anchorIdxAfter, fractionAfter] = view.scrollAnchor();
    QVERIFY(anchorIdxAfter >= 0);
    QCOMPARE(view.blockIdAt(anchorIdxAfter), anchorBlock);
    QVERIFY2(qAbs(fractionAfter - fractionBefore) < 0.35,
             qPrintable(QStringLiteral("anchor fraction drifted from %1 to %2 across resize")
                            .arg(fractionBefore).arg(fractionAfter)));
}

void TstCanvasLayoutWidth::live_policy_toggle_keeps_scroll_anchored()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(manyBlocks(200));

    View view;
    view.resize(900, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum() / 2);
    view.repaint();
    const auto [anchorIdxBefore, fractionBefore] = view.scrollAnchor();
    QVERIFY(anchorIdxBefore >= 0);
    const BlockId anchorBlock = view.blockIdAt(anchorIdxBefore);

    // Toggle FullWidth -> FixedColumn live, same as a user flipping the
    // reader-mode setting mid-scroll.
    view.setContentWidthPolicy(ContentWidthPolicy::fixedColumn(700.0));
    view.repaint();

    const auto [anchorIdxAfter, fractionAfter] = view.scrollAnchor();
    QVERIFY(anchorIdxAfter >= 0);
    QCOMPARE(view.blockIdAt(anchorIdxAfter), anchorBlock);
    QVERIFY2(qAbs(fractionAfter - fractionBefore) < 0.35,
             qPrintable(QStringLiteral("anchor fraction drifted from %1 to %2 across policy toggle")
                            .arg(fractionBefore).arg(fractionAfter)));
}

QTEST_MAIN(TstCanvasLayoutWidth)
#include "tst_canvas_layout_width.moc"
