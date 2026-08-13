// SPDX-License-Identifier: GPL-3.0-or-later
//
// T1 — read-only render, lazy layout, scrolling.
//
// Assertions run against the production widget (Markoff::Canvas::View)
// through its real event path: show it, let it paint, drive real key and
// wheel events. No test-only render entry point.

#include <QScrollBar>
#include <QTest>
#include <QWheelEvent>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

namespace {

QByteArray mixedKindFixture()
{
    return
        "# Heading one\n"
        "\n"
        "A paragraph of ordinary prose that is long enough to be worth\n"
        "laying out properly.\n"
        "\n"
        "## Heading two\n"
        "\n"
        "- first list item\n"
        "- second list item\n"
        "\n"
        "```cpp\n"
        "int main() { return 0; }\n"
        "```\n"
        "\n"
        "Closing paragraph.\n";
}

QByteArray manyBlocks(int n)
{
    QByteArray src;
    for (int i = 0; i < n; ++i) {
        src += "Paragraph number " + QByteArray::number(i)
             + " with enough words in it to occupy a line.\n\n";
    }
    return src;
}

/// Rows of the grabbed image containing at least one pixel that differs
/// from the image's top-left (background) pixel.
int paintedRowCount(const QImage &img)
{
    if (img.isNull())
        return 0;
    const QRgb bg = img.pixel(0, 0);
    int rows = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (img.pixel(x, y) != bg) {
                ++rows;
                break;
            }
        }
    }
    return rows;
}

}  // namespace

class TstCanvasRender : public QObject {
    Q_OBJECT

private slots:
    void constructs_and_shows_without_document();
    void accepts_and_reports_a_document();
    void fixture_renders_every_kind();
    void geometry_is_monotonic_and_contiguous();
    void per_kind_presentation_differs();
    void newlines_inside_a_block_break_lines();
    void layout_is_lazy_on_a_large_document();
    void scrolling_realizes_on_demand_and_stays_lazy();
    void wheel_scrolls_the_viewport();
};

void TstCanvasRender::constructs_and_shows_without_document()
{
    View view;
    view.resize(400, 300);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QCOMPARE(view.document(), nullptr);
    QCOMPARE(view.blockCount(), 0);
    QCOMPARE(view.documentHeight(), 0.0);
    QVERIFY(view.paintCount() > 0);  // painted its background, did not crash
}

void TstCanvasRender::accepts_and_reports_a_document()
{
    Markoff::MarkoffDocument doc;
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    QCOMPARE(view.document(), &doc);

    view.setDocument(nullptr);
    QCOMPARE(view.document(), nullptr);
    QCOMPARE(view.blockCount(), 0);
}

void TstCanvasRender::fixture_renders_every_kind()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(mixedKindFixture());

    View view;
    view.resize(600, 800);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QCOMPARE(view.blockCount(), int(doc.iterateBlocks().size()));
    QVERIFY(view.blockCount() >= 6);

    // The whole fixture fits in 800px, so everything is realized and drawn.
    QCOMPARE(view.realizedBlockCount(), view.blockCount());
    QVERIFY(view.documentHeight() > 0);

    const int rows = paintedRowCount(view.grab().toImage());
    QVERIFY2(rows > 20, qPrintable(QStringLiteral("painted rows: %1").arg(rows)));
}

void TstCanvasRender::geometry_is_monotonic_and_contiguous()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(mixedKindFixture());

    View view;
    view.resize(600, 800);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    qreal expectedY = 0;
    for (const BlockId id : doc.iterateBlocks()) {
        const QRectF r = view.blockRect(id);
        QVERIFY2(!r.isNull(), "every document block must have a rect");
        QVERIFY2(r.height() > 0, "no zero-height blocks");
        // Blocks tile the document top to bottom with no gaps or overlap.
        QVERIFY2(qAbs(r.y() - expectedY) < 0.01,
                 qPrintable(QStringLiteral("y %1 != expected %2")
                                .arg(r.y()).arg(expectedY)));
        expectedY += r.height();
    }
    QVERIFY(qAbs(view.documentHeight() - expectedY) < 0.01);
}

void TstCanvasRender::per_kind_presentation_differs()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("# H\n\nP\n");

    View view;
    view.resize(600, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(int(blocks.size()), 2);
    QCOMPARE(doc.blockKind(blocks[0]), Markoff::BlockKind::Heading);

    // Same single character, different kind: the heading must be taller.
    // This is the cheap proof that the per-kind switch reached the layout.
    QVERIFY2(view.blockRect(blocks[0]).height() > view.blockRect(blocks[1]).height(),
             "heading should lay out taller than a paragraph");
}

void TstCanvasRender::newlines_inside_a_block_break_lines()
{
    // A code block's buffer keeps its fences and its interior newlines
    // (verified against the core: unlike ListItem/BlockQuote, CodeBlock is
    // not narrowed to content). QTextLayout ignores '\n', so without the
    // LineSeparator substitution in BlockLayoutCache both of these render
    // as a single run-on line and have the same height.
    Markoff::MarkoffDocument shortDoc;
    shortDoc.loadFromMarkdown("```\na\n```\n");
    Markoff::MarkoffDocument tallDoc;
    tallDoc.loadFromMarkdown("```\na\nb\nc\nd\n```\n");

    View shortView;
    shortView.resize(600, 800);
    shortView.setDocument(&shortDoc);
    shortView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&shortView));

    View tallView;
    tallView.resize(600, 800);
    tallView.setDocument(&tallDoc);
    tallView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tallView));

    const auto shortBlocks = shortDoc.iterateBlocks();
    const auto tallBlocks  = tallDoc.iterateBlocks();
    QCOMPARE(shortDoc.blockKind(shortBlocks.front()), Markoff::BlockKind::CodeBlock);

    const qreal shortH = shortView.blockRect(shortBlocks.front()).height();
    const qreal tallH  = tallView.blockRect(tallBlocks.front()).height();

    // Three extra source lines must produce a meaningfully taller block.
    QVERIFY2(tallH > shortH * 1.5,
             qPrintable(QStringLiteral("3-line code block %1px vs 6-line %2px — "
                                       "newlines are not breaking lines")
                            .arg(shortH).arg(tallH)));
}

void TstCanvasRender::layout_is_lazy_on_a_large_document()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(manyBlocks(200));

    View view;
    view.resize(600, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QCOMPARE(view.blockCount(), 200);
    QVERIFY(view.paintCount() > 0);

    const int realized = view.realizedBlockCount();
    QVERIFY2(realized > 0, "the visible blocks must be realized");
    QVERIFY2(realized < view.blockCount(),
             qPrintable(QStringLiteral("first paint realized %1 of %2 blocks — "
                                       "layout is not lazy")
                            .arg(realized).arg(view.blockCount())));

    // Unrealized blocks still contribute an estimated height, so the
    // scroll range covers the whole document from the first paint.
    QVERIFY(view.documentHeight() > view.viewport()->height());
    QVERIFY(view.verticalScrollBar()->maximum() > 0);
}

void TstCanvasRender::scrolling_realizes_on_demand_and_stays_lazy()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(manyBlocks(200));

    View view;
    view.resize(600, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const int realizedAtTop = view.realizedBlockCount();

    // Page down through a few pages via real key events.
    for (int i = 0; i < 3; ++i)
        QTest::keyClick(&view, Qt::Key_PageDown);
    QVERIFY(view.verticalScrollBar()->value() > 0);

    // Jump to the end.
    QTest::keyClick(&view, Qt::Key_End, Qt::ControlModifier);
    QCOMPARE(view.verticalScrollBar()->value(), view.verticalScrollBar()->maximum());
    view.repaint();

    const int realizedAtEnd = view.realizedBlockCount();
    QVERIFY2(realizedAtEnd > realizedAtTop, "scrolling must realize new blocks");
    QVERIFY2(realizedAtEnd < view.blockCount(),
             qPrintable(QStringLiteral("scrolling to the end realized %1 of %2 — "
                                       "the middle should still be estimated")
                            .arg(realizedAtEnd).arg(view.blockCount())));

    // The last block is on screen and realized after Ctrl+End.
    const auto blocks = doc.iterateBlocks();
    const QRectF last = view.blockRect(blocks.back());
    const qreal scrollY = view.verticalScrollBar()->value();
    QVERIFY(last.bottom() > scrollY);
    QVERIFY(last.y() < scrollY + view.viewport()->height());

    QTest::keyClick(&view, Qt::Key_Home, Qt::ControlModifier);
    QCOMPARE(view.verticalScrollBar()->value(), 0);
}

void TstCanvasRender::wheel_scrolls_the_viewport()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(manyBlocks(200));

    View view;
    view.resize(600, 400);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QCOMPARE(view.verticalScrollBar()->value(), 0);

    QWheelEvent wheel(QPointF(300, 200), view.viewport()->mapToGlobal(QPoint(300, 200)),
                      QPoint(0, -120), QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QVERIFY(QApplication::sendEvent(view.viewport(), &wheel));

    QVERIFY2(view.verticalScrollBar()->value() > 0, "wheel must scroll the view");
}

QTEST_MAIN(TstCanvasRender)
#include "tst_canvas_render.moc"
