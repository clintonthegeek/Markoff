// SPDX-License-Identifier: GPL-3.0-or-later
//
// E4 F1 — per-cell InlineHighlighter wiring inside a TableDelegate.
//
// Each cell's TextEdit is its own QTextDocument; inline spans live on
// the block buffer in block-relative QString-char offsets. F1 wires an
// `InlineHighlighterAttached` to each cell with the subset of
// `model.inlineSpans` that overlap the cell's char range, translated
// to cell-relative offsets. This file asserts:
//
//   1. A `**bold**` token inside a body cell paints bold inside that
//      cell's text document.
//   2. The bold range is cell-relative (not block-relative).
//   3. Spans outside the cell do not paint inside the cell.
//
// Spec: docs/specs/2026-05-22-e4-tables-design.md.
// Plan: docs/plans/2026-05-22-e4-tables.md Phase F Task F1.

#include "QmlIntegrationFixture.h"
#include "RecordingLinkService.h"

#include <markoff/core/MarkoffDocument.h>
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/parser/SourceSpan.h>

#include <QCoreApplication>
#include <QFont>
#include <QPointF>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QRectF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest/QtTest>

namespace Markoff::Live::Test {

namespace {

QQuickItem *findTableDelegate(QmlIntegrationFixture &fx)
{
    for (int row = 0; row < 6; ++row) {
        QQuickItem *d = fx.delegateAt(row);
        if (!d) continue;
        if (QString::fromUtf8(d->metaObject()->className())
                .contains("TableDelegate"))
            return d;
    }
    return nullptr;
}

QQuickItem *cellAt(QQuickItem *table, int r, int c)
{
    if (!table) return nullptr;
    QQuickItem *repeater = nullptr;
    for (QQuickItem *k : table->findChildren<QQuickItem *>()) {
        if (QString::fromUtf8(k->metaObject()->className()).contains("Repeater")) {
            repeater = k; break;
        }
    }
    if (!repeater) return nullptr;
    const int cols = table->property("parsedTable").toMap()
                         .value("headers").toList().size();
    if (cols < 1) return nullptr;
    const int idx = r * cols + c;
    QQuickItem *cellRect = nullptr;
    QMetaObject::invokeMethod(repeater, "itemAt",
                              Q_RETURN_ARG(QQuickItem *, cellRect),
                              Q_ARG(int, idx));
    return cellRect;
}

QQuickItem *cellEditAt(QQuickItem *table, int r, int c)
{
    QQuickItem *cell = cellAt(table, r, c);
    if (!cell) return nullptr;
    return cell->property("edit").value<QQuickItem *>();
}

template <class Pred>
QPair<int,int> findFormatRange(const QTextDocument *doc, Pred pred)
{
    int start = -1, end = -1;
    QTextBlock block = doc->firstBlock();
    while (block.isValid()) {
        const int blockPos = block.position();
        const auto layoutFmts = block.layout()->formats();
        for (const QTextLayout::FormatRange &fr : layoutFmts) {
            if (pred(fr.format)) {
                if (start < 0) start = blockPos + fr.start;
                end = blockPos + fr.start + fr.length;
            }
        }
        block = block.next();
    }
    if (start < 0) return {-1, 0};
    return {start, end - start};
}

}  // namespace

class TestTableInlineFormatting : public QObject {
    Q_OBJECT
private slots:
    void bold_in_body_cell_paints_bold_at_cell_relative_offset();
    void span_outside_cell_does_not_paint_inside_other_cell();

    // F2 — wikilink + standard-link Ctrl+click + Ctrl+hover inside cells.
    void ctrl_click_on_wikilink_in_cell_dispatches_activation();
    void plain_click_on_wikilink_in_cell_does_not_activate();
    void ctrl_click_on_standard_link_in_cell_dispatches_activation();
    void ctrl_hover_on_wikilink_in_cell_emits_hover_and_flips_cursor();
};

void TestTableInlineFormatting
    ::bold_in_body_cell_paints_bold_at_cell_relative_offset()
{
    // Single-column body cell containing `**bold**`. Pre-padding policy
    // (p1) preserves the leading/trailing space, so the cell's text
    // (and thus the cell's TextEdit QTextDocument content) is:
    //
    //   " **bold** "          // 10 chars
    //    0123456789
    //     ^^^^^^^^             // bold span at cell-relative charOffset 1, length 8
    //
    // The block-buffer position of the bold span (from the parser) is
    // 14 (`| H |\n|---|\n|` is 13 chars, then space at 13, then `**` at 14).
    // The cellCharRange for (1, 0) starts at char 14 in the block buffer
    // (between the two pipes of the body row). Cell-relative offset is
    // therefore (14 - 14) = 0… wait. Actually `|` at 13, space at 14,
    // `**` at 15. cellRange.start = 14 (just after the opening pipe).
    // So the bold span in the cell document lives at char 1, length 8.
    //
    // The exact arithmetic doesn't matter for the assertion — we only
    // need: the cell's QTextDocument has a Bold layout-format range
    // whose [start, length] equals the block-level span's offset minus
    // cellCharRange[r][c].start.
    const QByteArray md =
        "para before\n"
        "\n"
        "| H |\n"
        "|---|\n"
        "| **bold** |\n"
        "\n"
        "para after\n";

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    QQuickItem *table = findTableDelegate(fx);
    QVERIFY2(table, "no TableDelegate at row 1");
    QTRY_VERIFY(cellAt(table, /*r=*/1, /*c=*/0) != nullptr);

    QQuickItem *cellEdit = cellEditAt(table, /*r=*/1, /*c=*/0);
    QVERIFY2(cellEdit, "no body cell TextEdit at (1, 0)");

    // Sanity: cell text matches what we expect.
    QCOMPARE(cellEdit->property("text").toString(),
             QStringLiteral(" **bold** "));

    // Compute the expected cell-relative span. Walk parsedTable.cellCharRanges
    // and the block's inlineSpans to discover the bold span's block-relative
    // charOffset, then subtract the cell range start.
    const auto blockIds = fx.document()->iterateBlocks();
    QVERIFY(blockIds.size() >= 2);
    const Markoff::BlockId tableId = blockIds[1];   // row 1 is the table block
    const QList<Markoff::SourceSpan> spans =
        fx.document()->inlineSpansFor(tableId);

    int blockBoldOffset = -1;
    int blockBoldLength = -1;
    for (const auto &s : spans) {
        if (s.bold && !s.isDelimiter && s.charLength > 0) {
            blockBoldOffset = s.charOffset;
            blockBoldLength = s.charLength;
            break;
        }
    }
    QVERIFY2(blockBoldOffset >= 0,
             qPrintable(QStringLiteral("no non-delimiter bold span on the table "
                                       "block; parser emitted %1 spans")
                            .arg(spans.size())));

    const QVariantMap parsed = table->property("parsedTable").toMap();
    const QVariantList ccr   = parsed["cellCharRanges"].toList();
    QVERIFY(ccr.size() >= 2);
    const QVariantList row1 = ccr[1].toList();
    QVERIFY(row1.size() >= 1);
    const QVariantMap cell10 = row1[0].toMap();
    const int cellStart = cell10["start"].toInt();
    const int expectedCellRelativeOffset = blockBoldOffset - cellStart;
    QVERIFY2(expectedCellRelativeOffset >= 0,
             qPrintable(QStringLiteral("bold span block-offset %1 < cellStart %2")
                            .arg(blockBoldOffset).arg(cellStart)));

    // Pull the QTextDocument out of the cell's TextEdit and walk format
    // ranges. QSyntaxHighlighter stores per-line format ranges on
    // QTextBlock::layout()->formats(), so we must read via layout.
    QQuickTextDocument *qtd =
        cellEdit->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY2(qtd, "cell TextEdit has no QQuickTextDocument");
    QTextDocument *doc = qtd->textDocument();
    QVERIFY(doc);

    // The highlighter wiring may not have settled the first frame after
    // delegate creation; give it a brief chance to install.
    QPair<int,int> range{-1, 0};
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        range = findFormatRange(doc, [](const QTextCharFormat &f) {
            return f.fontWeight() == QFont::Bold;
        });
        return range.first >= 0;
    }()), 2000);

    QCOMPARE(range.first, expectedCellRelativeOffset);
    QCOMPARE(range.second, blockBoldLength);
}

void TestTableInlineFormatting
    ::span_outside_cell_does_not_paint_inside_other_cell()
{
    // Two-column body row: `**bold**` lives in cell (1, 0). Cell (1, 1)
    // contains plain text. Cell (1, 1)'s document must NOT carry a bold
    // format from the (1, 0) span. Catches an implementation that passes
    // the full block-level inlineSpans list through without filtering.
    const QByteArray md =
        "para before\n"
        "\n"
        "| H1 | H2 |\n"
        "|---|---|\n"
        "| **bold** | plain |\n"
        "\n"
        "para after\n";

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    QQuickItem *table = findTableDelegate(fx);
    QVERIFY2(table, "no TableDelegate at row 1");
    QTRY_VERIFY(cellAt(table, 1, 1) != nullptr);

    QQuickItem *bold  = cellEditAt(table, 1, 0);
    QQuickItem *plain = cellEditAt(table, 1, 1);
    QVERIFY(bold);
    QVERIFY(plain);

    // Wait for the bold cell to actually paint bold first — that
    // confirms the highlighter has run a cycle and the assertion below
    // (no bold on the plain cell) is meaningful.
    QQuickTextDocument *boldQtd =
        bold->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY(boldQtd);
    QTextDocument *boldDoc = boldQtd->textDocument();
    QVERIFY(boldDoc);
    QPair<int,int> boldRange{-1, 0};
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        boldRange = findFormatRange(boldDoc, [](const QTextCharFormat &f) {
            return f.fontWeight() == QFont::Bold;
        });
        return boldRange.first >= 0;
    }()), 2000);

    QQuickTextDocument *plainQtd =
        plain->property("textDocument").value<QQuickTextDocument *>();
    QVERIFY(plainQtd);
    QTextDocument *plainDoc = plainQtd->textDocument();
    QVERIFY(plainDoc);
    const QPair<int,int> plainRange =
        findFormatRange(plainDoc, [](const QTextCharFormat &f) {
            return f.fontWeight() == QFont::Bold;
        });
    QCOMPARE(plainRange.first, -1);
}

// ============================================================================
// F2 — Ctrl+click + Ctrl+hover on links inside cells
// ============================================================================

namespace {

// Helper: compute a scene-coordinate point inside the visible text of the
// first inline span matching `pred` within the cell at (r, c). Mirrors the
// row-0 paragraph helper in QmlIntegrationFixture but addresses a specific
// cell's TextEdit. Returns a null point if no match.
template <class Pred>
QPoint scenePointAtCellSpan(QmlIntegrationFixture &fx, int blockRow,
                            int r, int c, Pred pred)
{
    QQuickItem *table = findTableDelegate(fx);
    if (!table) return {};
    QQuickItem *cellEdit = cellEditAt(table, r, c);
    if (!cellEdit) return {};

    const auto blockIds = fx.document()->iterateBlocks();
    if (blockRow >= static_cast<int>(blockIds.size())) return {};
    const Markoff::BlockId bid = blockIds[blockRow];
    const QList<Markoff::SourceSpan> spans = fx.document()->inlineSpansFor(bid);

    const QVariantMap parsed = table->property("parsedTable").toMap();
    const QVariantList ccr   = parsed["cellCharRanges"].toList();
    if (ccr.size() <= r) return {};
    const QVariantList row   = ccr[r].toList();
    if (row.size() <= c) return {};
    const QVariantMap cellRange = row[c].toMap();
    const int cellStart = cellRange["start"].toInt();
    const int cellEnd   = cellRange["end"].toInt();

    int spanOffset = -1;
    int spanLen    = 0;
    // Prefer non-delimiter content spans; fall back to delimiters.
    for (const auto &s : spans) {
        if (!pred(s) || s.charLength <= 0) continue;
        const int spanStart = s.charOffset;
        const int spanEndCh = s.charOffset + s.charLength;
        if (spanStart < cellStart || spanEndCh > cellEnd) continue;
        if (!s.isDelimiter) {
            spanOffset = spanStart - cellStart;
            spanLen    = s.charLength;
            break;
        }
        if (spanOffset < 0) {
            spanOffset = spanStart - cellStart;
            spanLen    = s.charLength;
        }
    }
    if (spanOffset < 0) return {};

    const int charPos = spanOffset + spanLen / 2;
    QRectF localRect;
    if (!QMetaObject::invokeMethod(cellEdit, "positionToRectangle",
                                   Qt::DirectConnection,
                                   Q_RETURN_ARG(QRectF, localRect),
                                   Q_ARG(int, charPos)))
        return {};
    const QPointF scenePt = cellEdit->mapToScene(localRect.center());
    return scenePt.toPoint();
}

}  // namespace

void TestTableInlineFormatting
    ::ctrl_click_on_wikilink_in_cell_dispatches_activation()
{
    // `[[Page]]` lives in cell (1, 0). LiveView's outer MouseArea Ctrl+click
    // routes through `root.hit(x, y)` → `TableDelegate.positionAt(x, y)` →
    // flat block-buffer qtPos → `binding.activateLinkAt(blockAnchor,
    // flatQtPos, mods)`. The wikilink span (added to the block by the F1
    // parser-side fix routing `pipe_table_cell` content through the inline
    // grammar) is found at the flat qtPos and dispatched to LinkService.
    const QByteArray md =
        "para before\n"
        "\n"
        "| H |\n"
        "|---|\n"
        "| [[Page]] |\n"
        "\n"
        "para after\n";
    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    Markoff::LiveTest::RecordingLinkService svc;
    auto *lb = qobject_cast<Markoff::Live::LiveListModelBinding *>(fx.binding());
    QVERIFY(lb);
    lb->setLinkService(&svc);

    QQuickItem *table = findTableDelegate(fx);
    QVERIFY(table);
    QTRY_VERIFY(cellAt(table, 1, 0) != nullptr);

    const QPoint clickPt = scenePointAtCellSpan(fx, /*blockRow=*/1,
                                                /*r=*/1, /*c=*/0,
        [](const Markoff::SourceSpan &s) { return s.isWikilink; });
    QVERIFY2(!clickPt.isNull(), "could not compute scene point for cell wikilink");

    QTest::mouseClick(fx.window(), Qt::LeftButton, Qt::ControlModifier, clickPt);
    QTRY_COMPARE_WITH_TIMEOUT(svc.activations.size(), 1, 2000);
    QCOMPARE(svc.activations.first().page, QStringLiteral("Page"));
}

void TestTableInlineFormatting
    ::plain_click_on_wikilink_in_cell_does_not_activate()
{
    const QByteArray md =
        "para before\n"
        "\n"
        "| H |\n"
        "|---|\n"
        "| [[Page]] |\n"
        "\n"
        "para after\n";
    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    Markoff::LiveTest::RecordingLinkService svc;
    auto *lb = qobject_cast<Markoff::Live::LiveListModelBinding *>(fx.binding());
    QVERIFY(lb);
    lb->setLinkService(&svc);

    QQuickItem *table = findTableDelegate(fx);
    QVERIFY(table);
    QTRY_VERIFY(cellAt(table, 1, 0) != nullptr);

    const QPoint clickPt = scenePointAtCellSpan(fx, /*blockRow=*/1,
                                                /*r=*/1, /*c=*/0,
        [](const Markoff::SourceSpan &s) { return s.isWikilink; });
    QVERIFY(!clickPt.isNull());

    QTest::mouseClick(fx.window(), Qt::LeftButton, Qt::NoModifier, clickPt);
    QTest::qWait(100);
    QCoreApplication::processEvents();
    QCOMPARE(svc.activations.size(), 0);
}

void TestTableInlineFormatting
    ::ctrl_click_on_standard_link_in_cell_dispatches_activation()
{
    // Standard markdown link `[text](https://example.com)` in cell (1, 0).
    // The path is identical to wikilinks — `binding.activateLinkAt` reads
    // span.linkTarget from the spans cache, classifies (External in this
    // case), and dispatches.
    const QByteArray md =
        "para before\n"
        "\n"
        "| H |\n"
        "|---|\n"
        "| [click](https://example.com) |\n"
        "\n"
        "para after\n";
    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    Markoff::LiveTest::RecordingLinkService svc;
    auto *lb = qobject_cast<Markoff::Live::LiveListModelBinding *>(fx.binding());
    QVERIFY(lb);
    lb->setLinkService(&svc);

    QQuickItem *table = findTableDelegate(fx);
    QVERIFY(table);
    QTRY_VERIFY(cellAt(table, 1, 0) != nullptr);

    const QPoint clickPt = scenePointAtCellSpan(fx, /*blockRow=*/1,
                                                /*r=*/1, /*c=*/0,
        [](const Markoff::SourceSpan &s) {
            return s.isLink && !s.isWikilink;
        });
    QVERIFY2(!clickPt.isNull(), "could not compute scene point for cell link");

    QTest::mouseClick(fx.window(), Qt::LeftButton, Qt::ControlModifier, clickPt);
    QTRY_COMPARE_WITH_TIMEOUT(svc.activations.size(), 1, 2000);
    const Markoff::LinkActivation &act = svc.activations.first();
    QVERIFY2(act.resolvedTarget.toString().contains(QStringLiteral("example.com"))
                 || act.rawText.contains(QStringLiteral("example.com")),
             qPrintable(QStringLiteral("link payload missing example.com; "
                                       "rawText=%1 resolvedTarget=%2")
                            .arg(act.rawText, act.resolvedTarget.toString())));
}

void TestTableInlineFormatting
    ::ctrl_hover_on_wikilink_in_cell_emits_hover_and_flips_cursor()
{
    // Mirror tst_live_link_qml_integration::ctrl_hover_emits_hover but
    // address a cell-resident wikilink. The outer MouseArea's
    // onPositionChanged with ControlModifier calls
    // `binding.hoverLinkAt(blockAnchor, flatQtPos, mods, scenePoint)` —
    // since hit-test routes through TableDelegate.positionAt, the path is
    // the same as for paragraph blocks.
    const QByteArray md =
        "para before\n"
        "\n"
        "| H |\n"
        "|---|\n"
        "| [[Page]] |\n"
        "\n"
        "para after\n";
    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    Markoff::LiveTest::RecordingLinkService svc;
    auto *lb = qobject_cast<Markoff::Live::LiveListModelBinding *>(fx.binding());
    QVERIFY(lb);
    lb->setLinkService(&svc);

    QQuickItem *table = findTableDelegate(fx);
    QVERIFY(table);
    QTRY_VERIFY(cellAt(table, 1, 0) != nullptr);

    const QPoint pt = scenePointAtCellSpan(fx, /*blockRow=*/1,
                                           /*r=*/1, /*c=*/0,
        [](const Markoff::SourceSpan &s) { return s.isWikilink; });
    QVERIFY(!pt.isNull());

    fx.simulateCtrlHoverAt(pt);
    QTRY_COMPARE_WITH_TIMEOUT(svc.hovers.size(), 1, 2000);
    QCOMPARE(svc.hovers.first().page, QStringLiteral("Page"));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestTableInlineFormatting)
#include "tst_live_render_table_inline_formatting.moc"
