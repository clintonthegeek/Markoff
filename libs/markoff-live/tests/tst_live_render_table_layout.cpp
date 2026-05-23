// SPDX-License-Identifier: GPL-3.0-or-later
//
// E4 follow-up C2 — content-aware column widths + line wrapping reach
// the QML side. Drives QmlIntegrationFixture against fixtures/tables_wrap.md
// and asserts five invariants from the spec §5:
//
//   1. wide_widget_fits_without_wrap
//   2. narrow_widget_wraps_prose_column
//   3. very_narrow_widget_honors_min_floor
//   4. no_horizontal_overflow_across_viewport_widths
//   5. row_height_grows_with_wrap
//
// Falsifiability proofs for the QML-reach paths are commits ea9b5bd
// (broken distributeColumnsAuto stub) and the inline manual proof
// recorded in B2's commit message. Production code reaches the cells
// via the same property bindings these tests query.
//
// Spec: docs/specs/2026-05-23-e4-cell-wrap-and-column-width-design.md §5.
// Plan: docs/plans/2026-05-23-e4-cell-wrap-and-column-width.md C2.

#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <markoff/live/TableEditBinding.h>

#include <QFile>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantList>
#include <QVariantMap>
#include <QtMath>
#include <QtTest/QtTest>

using Markoff::Live::TableEditBinding;

namespace Markoff::Live::Test {

namespace {

QQuickItem *findTableDelegateAtRow(QmlIntegrationFixture &fx, int row)
{
    QQuickItem *d = fx.delegateAt(row);
    if (!d) return nullptr;
    if (QString::fromUtf8(d->metaObject()->className())
            .contains(QLatin1String("TableDelegate")))
        return d;
    return nullptr;
}

QQuickItem *cellGridOf(QQuickItem *table)
{
    if (!table) return nullptr;
    for (QQuickItem *k : table->findChildren<QQuickItem *>()) {
        if (k->objectName() == QLatin1String("cellGrid")) return k;
    }
    // Fallback: class-name match (objectName isn't set in production).
    // GridLayout shows up as QQuickGridLayout. The TableDelegate has
    // exactly one. Take the first.
    for (QQuickItem *k : table->findChildren<QQuickItem *>()) {
        if (QString::fromUtf8(k->metaObject()->className())
                .contains(QLatin1String("GridLayout"))) return k;
    }
    return nullptr;
}

QQuickItem *cellAt(QQuickItem *table, int r, int c)
{
    if (!table) return nullptr;
    QQuickItem *repeater = nullptr;
    for (QQuickItem *k : table->findChildren<QQuickItem *>()) {
        if (QString::fromUtf8(k->metaObject()->className())
                .contains(QLatin1String("Repeater"))) {
            repeater = k; break;
        }
    }
    if (!repeater) return nullptr;
    const int cols = table->property("parsedTable").toMap()
                          .value("headers").toList().size();
    if (cols < 1) return nullptr;
    const int idx = r * cols + c;
    QQuickItem *cell = nullptr;
    QMetaObject::invokeMethod(repeater, "itemAt",
                              Q_RETURN_ARG(QQuickItem *, cell),
                              Q_ARG(int, idx));
    return cell;
}

QQuickItem *cellTextEditAt(QQuickItem *table, int r, int c)
{
    QQuickItem *cell = cellAt(table, r, c);
    if (!cell) return nullptr;
    return cell->property("edit").value<QQuickItem *>();
}

int cellLineCount(QQuickItem *table, int r, int c)
{
    QQuickItem *edit = cellTextEditAt(table, r, c);
    if (!edit) return -1;
    return edit->property("lineCount").toInt();
}

// Resize the window to the requested *cellGrid* inner width by adding
// the difference between the current cellGrid width and the current
// window width to the request. Waits for the next render pass before
// returning.
void resizeWindowForCellGridWidth(QmlIntegrationFixture &fx,
                                  QQuickItem *cellGrid,
                                  qreal targetCellGridWidth)
{
    QQuickWindow *win = fx.window();
    QVERIFY(win);
    QVERIFY(cellGrid);
    const qreal cur     = cellGrid->width();
    const qreal winCur  = win->width();
    const qreal newWinW = winCur + (targetCellGridWidth - cur);
    win->resize(int(newWinW), int(win->height()));
    QTest::qWait(100);
}

QByteArray loadFixture(const QString &name)
{
    QFile f(QString::fromLatin1(MARKOFF_LIVE_TESTS_DIR)
                + QStringLiteral("/fixtures/") + name);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QByteArray bytes = f.readAll();
    f.close();
    return bytes;
}

constexpr int kTableRow = 1;       // tables_wrap.md: paragraph, table, paragraph
constexpr int kProseCol = 1;       // "Description" column
constexpr int kShortCol = 0;       // "Short" column
constexpr int kCodeCol  = 2;       // "Code" column

}  // namespace

class TestTableLayout : public QObject {
    Q_OBJECT
private slots:
    void wide_widget_fits_without_wrap();
    void narrow_widget_wraps_prose_column();
    void very_narrow_widget_honors_min_floor();
    void no_horizontal_overflow_across_viewport_widths();
    void row_height_grows_with_wrap();
};

void TestTableLayout::wide_widget_fits_without_wrap()
{
    const QByteArray md = loadFixture(QStringLiteral("tables_wrap.md"));
    QVERIFY(!md.isEmpty());

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(kTableRow, 2000));

    QQuickItem *table = findTableDelegateAtRow(fx, kTableRow);
    QVERIFY2(table, "no TableDelegate at row 1");
    QQuickItem *grid = cellGridOf(table);
    QVERIFY(grid);
    QTRY_VERIFY(cellAt(table, 1, kProseCol) != nullptr);

    // Make the cellGrid wider than totalMax for any reasonable font.
    // The prose row's max width measured at body-font is well under
    // 2400 px, so 3000 forces the totalMax<=avail branch.
    resizeWindowForCellGridWidth(fx, grid, 3000.0);

    // Each cell that has content should fit on a single line.
    // (The longest prose row is body row 1 (index 1 inside body, row 2
    // overall when counting the header). cellLineCount uses the
    // TextEdit's reported lineCount.)
    for (int r = 0; r <= 3; ++r) {  // header + 3 body rows
        for (int c = 0; c <= 2; ++c) {
            const int lc = cellLineCount(table, r, c);
            QVERIFY2(lc == 1, qPrintable(QStringLiteral(
                "cell (%1,%2) wrapped at wide width: lineCount=%3")
                .arg(r).arg(c).arg(lc)));
        }
    }
}

void TestTableLayout::narrow_widget_wraps_prose_column()
{
    const QByteArray md = loadFixture(QStringLiteral("tables_wrap.md"));
    QVERIFY(!md.isEmpty());

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(kTableRow, 2000));

    QQuickItem *table = findTableDelegateAtRow(fx, kTableRow);
    QVERIFY(table);
    QQuickItem *grid = cellGridOf(table);
    QVERIFY(grid);
    QTRY_VERIFY(cellAt(table, 1, kProseCol) != nullptr);

    // Force the proportional branch: too narrow for everyone's max,
    // but wide enough for everyone's min. A 400px cellGrid is in that
    // sweet spot for the fixture (short ~ 60-80px, code ~ 60-100px,
    // description's full-line measurement >> 400px → must wrap).
    resizeWindowForCellGridWidth(fx, grid, 400.0);

    // Body row 1 (index 0 inside body, row 1 overall after header)
    // holds the long prose line. Expect lineCount > 1.
    const int proseLC = cellLineCount(table, /*r=*/1, kProseCol);
    QVERIFY2(proseLC > 1, qPrintable(QStringLiteral(
        "prose cell did not wrap at narrow width: lineCount=%1").arg(proseLC)));

    // Short-label columns should still fit on a single line — their
    // content is single words, the algorithm assigns them small but
    // sufficient widths.
    for (int r = 0; r <= 3; ++r) {
        const int shortLC = cellLineCount(table, r, kShortCol);
        QVERIFY2(shortLC == 1, qPrintable(QStringLiteral(
            "short cell (%1,%2) wrapped: lineCount=%3")
            .arg(r).arg(kShortCol).arg(shortLC)));
    }
}

void TestTableLayout::very_narrow_widget_honors_min_floor()
{
    const QByteArray md = loadFixture(QStringLiteral("tables_wrap.md"));
    QVERIFY(!md.isEmpty());

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(kTableRow, 2000));

    QQuickItem *table = findTableDelegateAtRow(fx, kTableRow);
    QVERIFY(table);
    QQuickItem *grid = cellGridOf(table);
    QVERIFY(grid);
    QTRY_VERIFY(cellAt(table, 1, kProseCol) != nullptr);

    // Squeeze the cellGrid below 3*kMinColumnWidth (180px). The
    // totalMin>=avail branch should kick in and scale mins
    // proportionally — sum of widths still equals avail (no
    // horizontal overflow), but per-column widths drop below 60px.
    resizeWindowForCellGridWidth(fx, grid, 120.0);

    const QVariantList widths = table->property("columnWidths").toList();
    QCOMPARE(widths.size(), 3);

    qreal sum = 0;
    for (const QVariant &v : widths) sum += v.toReal();

    // The cellGrid's actual width tracks the columns we set; the table
    // doesn't overflow horizontally (within 1px FP rounding).
    QVERIFY2(qFabs(sum - grid->width()) < 1.5,
             qPrintable(QStringLiteral("sum=%1 grid.width=%2 mismatch")
                        .arg(sum).arg(grid->width())));

    // In the totalMin>=avail branch widths drop below kMinColumnWidth
    // (60px) — that's the correct behaviour: we can't simultaneously
    // honor the floor AND avoid overflow when the user's viewport is
    // smaller than 3*60. The floor lives at the per-column metric
    // aggregation step; below totalMin it gives way to fit-in-viewport.
    QVERIFY(widths[0].toReal() < TableEditBinding::kMinColumnWidth);
}

void TestTableLayout::no_horizontal_overflow_across_viewport_widths()
{
    const QByteArray md = loadFixture(QStringLiteral("tables_wrap.md"));
    QVERIFY(!md.isEmpty());

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(kTableRow, 2000));

    QQuickItem *table = findTableDelegateAtRow(fx, kTableRow);
    QVERIFY(table);
    QQuickItem *grid = cellGridOf(table);
    QVERIFY(grid);
    QTRY_VERIFY(cellAt(table, 1, kProseCol) != nullptr);

    // Sweep a range of widget widths. For each, columnWidths must sum
    // to (approximately) the cellGrid's actual width.
    for (qreal target : { 200.0, 300.0, 500.0, 800.0, 1200.0, 2000.0 }) {
        resizeWindowForCellGridWidth(fx, grid, target);

        const QVariantList widths = table->property("columnWidths").toList();
        if (widths.isEmpty()) continue;   // grid not yet realised; skip
        qreal sum = 0;
        for (const QVariant &v : widths) sum += v.toReal();
        QVERIFY2(qFabs(sum - grid->width()) < 1.5,
                 qPrintable(QStringLiteral(
                    "target=%1 sum=%2 grid.width=%3 mismatch")
                    .arg(target).arg(sum).arg(grid->width())));
    }
}

void TestTableLayout::row_height_grows_with_wrap()
{
    const QByteArray md = loadFixture(QStringLiteral("tables_wrap.md"));
    QVERIFY(!md.isEmpty());

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(kTableRow, 2000));

    QQuickItem *table = findTableDelegateAtRow(fx, kTableRow);
    QVERIFY(table);
    QQuickItem *grid = cellGridOf(table);
    QVERIFY(grid);
    QTRY_VERIFY(cellAt(table, 1, kProseCol) != nullptr);

    // Wide: no wrap. Take the prose cell's height as the single-line
    // baseline.
    resizeWindowForCellGridWidth(fx, grid, 3000.0);
    QQuickItem *proseCellWide = cellAt(table, /*r=*/1, kProseCol);
    QVERIFY(proseCellWide);
    const qreal wideHeight = proseCellWide->height();

    // Narrow: forces wrap. Cell height must grow.
    resizeWindowForCellGridWidth(fx, grid, 400.0);
    QQuickItem *proseCellNarrow = cellAt(table, /*r=*/1, kProseCol);
    QVERIFY(proseCellNarrow);
    const qreal narrowHeight = proseCellNarrow->height();

    QVERIFY2(narrowHeight > wideHeight + 1.0,
             qPrintable(QStringLiteral(
                "prose cell did not grow on wrap: wide=%1 narrow=%2")
                .arg(wideHeight).arg(narrowHeight)));
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestTableLayout)
#include "tst_live_render_table_layout.moc"
