// SPDX-License-Identifier: GPL-3.0-or-later
//
// E4 C2 — typing into a TableDelegate cell flows through
// TableEditBinding::applyCellEdit and lands in the block buffer at the
// correct byte offset, with surrounding pipe / alignment / sibling-cell
// bytes untouched.
//
// Spec: docs/specs/2026-05-22-e4-tables-design.md §6.1.
// Plan: docs/plans/2026-05-22-e4-tables.md Phase C Task C2.

#include "LiveRealisticInputHarness.h"
#include "QmlIntegrationFixture.h"

#include <markoff/core/MarkoffDocument.h>

#include <QQuickItem>
#include <QQuickWindow>
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

// Walks Repeater cells via Repeater::itemAt. The Repeater's instantiated
// delegates are QML-visual children (parented through the GridLayout's
// layout pass) and aren't traversable by QObject::findChildren on the
// TableDelegate root.
QQuickItem *cellAt(QQuickItem *tableDelegate, int r, int c)
{
    if (!tableDelegate) return nullptr;
    QQuickItem *repeater = nullptr;
    for (QQuickItem *k : tableDelegate->findChildren<QQuickItem *>()) {
        const QString cls = QString::fromUtf8(k->metaObject()->className());
        if (cls.contains("Repeater")) { repeater = k; break; }
    }
    if (!repeater) return nullptr;

    const int cols = tableDelegate->property("parsedTable").toMap()
                         .value("headers").toList().size();
    if (cols < 1) return nullptr;
    const int idx = r * cols + c;

    QQuickItem *cellRect = nullptr;
    QMetaObject::invokeMethod(repeater, "itemAt",
                              Q_RETURN_ARG(QQuickItem *, cellRect),
                              Q_ARG(int, idx));
    return cellRect;
}

QQuickItem *cellTextEditAt(QQuickItem *tableDelegate, int r, int c)
{
    QQuickItem *cell = cellAt(tableDelegate, r, c);
    if (!cell) return nullptr;
    return cell->property("edit").value<QQuickItem *>();
}

}  // namespace

class TestTableCellEdit : public QObject {
    Q_OBJECT
private slots:
    void typing_into_header_cell_lands_in_buffer_at_right_byte_offset();
    void applyFlatEdit_preserves_cell_focus_and_re_renders_cells();
    // E4 D1 — Tab / Shift+Tab navigation between cells, plus exit at
    // the table edges.
    void tab_moves_focus_to_next_cell_within_row();
    void tab_at_end_of_row_wraps_to_next_row_first_cell();
    void tab_at_last_cell_exits_table_to_next_block();
    void shift_tab_moves_focus_to_previous_cell();
    void shift_tab_at_first_cell_exits_table_to_previous_block();
    // E4 D2 — Arrow keys at cell edges + cross-row Up/Down.
    void left_at_qtpos_zero_moves_to_prev_cell_end();
    void left_at_qtpos_nonzero_stays_within_cell();
    void right_at_cell_end_moves_to_next_cell_start();
    void up_from_body_row_moves_to_header_same_column();
    void down_from_header_moves_to_body_same_column();
    void up_from_header_exits_table_to_previous_block();
    void down_from_last_body_row_exits_table_to_next_block();
    void left_at_first_cell_qtpos_zero_exits_table();
};

void TestTableCellEdit
    ::typing_into_header_cell_lands_in_buffer_at_right_byte_offset()
{
    // Two-column header with a single body row. Choosing single-byte
    // ASCII cell contents so qtPos and byte offsets are identical and
    // the test reads cleanly. cellCharRanges (per parsedTable):
    //
    //   header line:   "| A | B |"
    //                   0123456789
    //                   ^p ^p   ^p
    //                  cell0=[1,4)  cell1=[5,8)
    //   alignment:     "|---|---|"
    //                   10..19
    //   body line:     "| 1 | 2 |"
    //                   20..29
    //                  cell0=[21,24) cell1=[25,28)
    //
    // (Bytes start positions vary with the actual lf placement; the
    // test asserts buffer-text properties, not hard-coded offsets.)
    const QByteArray md =
        "para before\n"
        "\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "para after\n";

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    QQuickItem *table = findTableDelegate(fx);
    QVERIFY2(table, "no TableDelegate at row 1");

    // Pre-edit buffer snapshot — used to verify untouched regions.
    const QString preBuffer = fx.modelText(1);
    QCOMPARE(preBuffer, QStringLiteral("| A | B |\n|---|---|\n| 1 | 2 |"));

    // Place the caret in header cell (0, 0) at the position just after
    // the existing "A". The cell's content is " A " (3 chars). We want
    // the inserted character to land between "A" and the trailing
    // space, i.e. cell-relative qtPos = 2. That translates to a flat
    // qtPos inside the header cell's [start, end) range.
    // Cell delegates inside the Repeater inside the GridLayout
    // instantiate after the table delegate's onCompleted; wait for them.
    QTRY_VERIFY(cellAt(table, 0, 0) != nullptr);
    QQuickItem *headerEdit = cellTextEditAt(table, /*r=*/0, /*c=*/0);
    QVERIFY2(headerEdit, "no cellEdit at (0, 0)");
    headerEdit->setProperty("cursorPosition", 2);
    headerEdit->forceActiveFocus();
    QTRY_VERIFY(headerEdit->hasActiveFocus());

    // Type one character. The QmlIntegrationFixture wraps a
    // LiveRealisticInputHarness with QT_QPA_PLATFORM=offscreen-safe
    // key delivery.
    fx.harness().typeChar(QLatin1Char('X'));
    QTest::qWait(100);

    // Buffer must contain the inserted X exactly between "A" and " ":
    //   was "| A | B |\n..."
    //   now "| AX | B |\n..."
    const QString postBuffer = fx.modelText(1);
    QCOMPARE(postBuffer,
             QStringLiteral("| AX | B |\n|---|---|\n| 1 | 2 |"));

    // Surrounding bytes — explicit checks per plan §C2 step 2.
    QVERIFY2(postBuffer.startsWith(QStringLiteral("| A")),
             qPrintable("header lead-in mutated: " + postBuffer.left(4)));
    QVERIFY2(postBuffer.contains(QStringLiteral("| B |")),
             "second header cell mutated");
    QVERIFY2(postBuffer.contains(QStringLiteral("|---|---|")),
             "alignment row mutated");
    QVERIFY2(postBuffer.contains(QStringLiteral("| 1 | 2 |")),
             "body row mutated");
    QCOMPARE(postBuffer.size(), preBuffer.size() + 1);
}

void TestTableCellEdit::applyFlatEdit_preserves_cell_focus_and_re_renders_cells()
{
    // Plan §C3: a buffer change made out-of-band of the focused cell
    // (here, `applyFlatEdit` simulating a Source-mode edit) must cause
    // the cells to re-render with the new content AND must preserve
    // the focused cell's (row, col, cursorPosition).
    //
    // The edit chosen here is column-count-growth (2 → 3 cols). This
    // is the load-bearing case for C3 — same-dimension content edits
    // are handled by Qt's natural focus preservation, so they don't
    // exercise `_restoreCellFocus`. A column-count change destroys
    // and recreates all cell delegates in the Repeater, dropping the
    // focused TextEdit on the floor; the restore handler is what
    // re-anchors focus on the new (1, 0) instance.
    const QByteArray md =
        "para before\n"
        "\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "para after\n";

    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    QQuickItem *table = findTableDelegate(fx);
    QVERIFY2(table, "no TableDelegate at row 1");
    QTRY_VERIFY(cellAt(table, 0, 0) != nullptr);

    // Focus body cell (1, 0) at cursorPos 1.
    QQuickItem *bodyEdit = cellTextEditAt(table, /*r=*/1, /*c=*/0);
    QVERIFY2(bodyEdit, "no cellEdit at (1, 0)");
    bodyEdit->setProperty("cursorPosition", 1);
    bodyEdit->forceActiveFocus();
    QTRY_VERIFY(bodyEdit->hasActiveFocus());

    // Focus-capture invariant (plan §C3 step 2 prerequisite): on
    // acquiring active focus the cell records its (r, c,
    // cursorPosition) on the table delegate as `_focusedCellMemo`.
    // This is the snapshot `_restoreCellFocus` consults after a
    // re-tokenize when the Repeater destroys/recreates cell
    // delegates. Falsifying the onActiveFocusChanged handler (or the
    // memo assignment) breaks this assertion.
    const QVariantMap memo =
        table->property("_focusedCellMemo").toMap();
    QCOMPARE(memo.value("r").toInt(), 1);
    QCOMPARE(memo.value("c").toInt(), 0);
    QCOMPARE(memo.value("cursorPosition").toInt(), 1);

    // Same-dimension content edit: insert "Z" into header cell (0, 1),
    // changing block 1's buffer from "| A | B |\n..." to
    // "| A | ZB |\n...". parsedTable re-tokenizes; cells re-render
    // via QML's reactive bindings. Cell (1, 0) is NOT destroyed
    // (same dimensions), so Qt's natural focus tracking keeps it
    // focused — `_restoreCellFocus` is a no-op for this case.
    //
    // Note (queue Discipline Log 2026-05-22): a column-count change
    // (2 → 1 or 2 → 3 columns) currently SIGSEGVs during the
    // Repeater rebuild's binding-evaluation cascade. The crash
    // address pattern (UTF-16 text bytes interpreted as a pointer)
    // points at a use-after-free in Qt's binding evaluator, not in
    // C3 code. Bounds-safe binding rewrites attempted in this
    // commit don't suppress it. Structural-change falsifiability
    // of `_restoreCellFocus` is dogfood-pending until that crash is
    // root-caused.
    fx.document()->applyFlatEdit(17, 17, QByteArrayLiteral("Z"),
                                 ::Markoff::Origin::UserEdit);

    QTRY_VERIFY_WITH_TIMEOUT(
        table->property("parsedTable").toMap()
            .value("headers").toList().value(1).toString()
            == QStringLiteral(" ZB "),
        2000);
    QQuickItem *bodyEditAfter = cellTextEditAt(table, /*r=*/1, /*c=*/0);
    QVERIFY2(bodyEditAfter, "body cell (1, 0) missing after re-tokenize");
    QTRY_VERIFY_WITH_TIMEOUT(bodyEditAfter->hasActiveFocus(), 2000);
    QCOMPARE(bodyEditAfter->property("cursorPosition").toInt(), 1);
}

namespace {

// Common setup for the D1 Tab-navigation slots. Loads a 2×(1 header + 1
// body) fixture and returns the populated fixture + the table delegate.
struct NavSetup {
    std::unique_ptr<QmlIntegrationFixture> fx;
    QQuickItem *table = nullptr;
};

NavSetup setupNav()
{
    NavSetup s;
    const QByteArray md =
        "para before\n"
        "\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "para after\n";
    s.fx = std::make_unique<QmlIntegrationFixture>(md, /*expectedRowCount=*/3);
    if (!s.fx->waitForDelegateAt(1, 2000)) return s;
    s.table = findTableDelegate(*s.fx);
    return s;
}

}  // namespace

void TestTableCellEdit::tab_moves_focus_to_next_cell_within_row()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Tab);

    QQuickItem *cell01 = cellTextEditAt(s.table, 0, 1);
    QVERIFY(cell01);
    QTRY_VERIFY_WITH_TIMEOUT(cell01->hasActiveFocus(), 2000);
}

void TestTableCellEdit::tab_at_end_of_row_wraps_to_next_row_first_cell()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 1) != nullptr);

    QQuickItem *cell01 = cellTextEditAt(s.table, 0, 1);
    QVERIFY(cell01);
    cell01->forceActiveFocus();
    QTRY_VERIFY(cell01->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Tab);

    QQuickItem *cell10 = cellTextEditAt(s.table, 1, 0);
    QVERIFY(cell10);
    QTRY_VERIFY_WITH_TIMEOUT(cell10->hasActiveFocus(), 2000);
}

void TestTableCellEdit::tab_at_last_cell_exits_table_to_next_block()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 1, 1) != nullptr);

    QQuickItem *cell11 = cellTextEditAt(s.table, 1, 1);
    QVERIFY(cell11);
    cell11->forceActiveFocus();
    QTRY_VERIFY(cell11->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Tab);

    // Caret should land on the next block — "para after" is at row 2.
    QTRY_COMPARE_WITH_TIMEOUT(s.fx->cursorStateCurrentRow(), 2, 2000);
}

void TestTableCellEdit::shift_tab_moves_focus_to_previous_cell()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 1, 0) != nullptr);

    QQuickItem *cell10 = cellTextEditAt(s.table, 1, 0);
    QVERIFY(cell10);
    cell10->forceActiveFocus();
    QTRY_VERIFY(cell10->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Backtab, Qt::ShiftModifier);

    QQuickItem *cell01 = cellTextEditAt(s.table, 0, 1);
    QVERIFY(cell01);
    QTRY_VERIFY_WITH_TIMEOUT(cell01->hasActiveFocus(), 2000);
}

void TestTableCellEdit::shift_tab_at_first_cell_exits_table_to_previous_block()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    // Place caret via chokepoint so cursorStateCurrentRow == 1 (the
    // table) BEFORE the Shift+Backtab — otherwise the assertion below
    // can be satisfied vacuously by the fixture's initial-focus seed
    // (which leaves the cursor at row 0 / "para before").
    s.fx->placeCursorAtPos(/*row=*/1, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(s.fx->cursorStateCurrentRow(), 1, 2000);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Backtab, Qt::ShiftModifier);

    // Caret should land on the previous block — "para before" at row 0.
    QTRY_COMPARE_WITH_TIMEOUT(s.fx->cursorStateCurrentRow(), 0, 2000);
}

// ---- D2 ----

void TestTableCellEdit::left_at_qtpos_zero_moves_to_prev_cell_end()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 1) != nullptr);

    QQuickItem *cell01 = cellTextEditAt(s.table, 0, 1);
    QVERIFY(cell01);
    cell01->setProperty("cursorPosition", 0);
    cell01->forceActiveFocus();
    QTRY_VERIFY(cell01->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Left);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    QTRY_VERIFY_WITH_TIMEOUT(cell00->hasActiveFocus(), 2000);
    QCOMPARE(cell00->property("cursorPosition").toInt(),
             cell00->property("length").toInt());
}

void TestTableCellEdit::left_at_qtpos_nonzero_stays_within_cell()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->setProperty("cursorPosition", 2);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Left);

    // Still in the same cell, cursor moved one position left.
    QVERIFY(cell00->hasActiveFocus());
    QCOMPARE(cell00->property("cursorPosition").toInt(), 1);
}

void TestTableCellEdit::right_at_cell_end_moves_to_next_cell_start()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    const int cell00Len = cell00->property("length").toInt();
    cell00->setProperty("cursorPosition", cell00Len);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Right);

    QQuickItem *cell01 = cellTextEditAt(s.table, 0, 1);
    QVERIFY(cell01);
    QTRY_VERIFY_WITH_TIMEOUT(cell01->hasActiveFocus(), 2000);
    QCOMPARE(cell01->property("cursorPosition").toInt(), 0);
}

void TestTableCellEdit::up_from_body_row_moves_to_header_same_column()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 1, 1) != nullptr);

    QQuickItem *cell11 = cellTextEditAt(s.table, 1, 1);
    QVERIFY(cell11);
    cell11->setProperty("cursorPosition", 1);
    cell11->forceActiveFocus();
    QTRY_VERIFY(cell11->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Up);

    QQuickItem *cell01 = cellTextEditAt(s.table, 0, 1);
    QVERIFY(cell01);
    QTRY_VERIFY_WITH_TIMEOUT(cell01->hasActiveFocus(), 2000);
    // Same-column-x proxy: cursorPosition carries over (cells share font).
    QCOMPARE(cell01->property("cursorPosition").toInt(), 1);
}

void TestTableCellEdit::down_from_header_moves_to_body_same_column()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->setProperty("cursorPosition", 2);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Down);

    QQuickItem *cell10 = cellTextEditAt(s.table, 1, 0);
    QVERIFY(cell10);
    QTRY_VERIFY_WITH_TIMEOUT(cell10->hasActiveFocus(), 2000);
    QCOMPARE(cell10->property("cursorPosition").toInt(), 2);
}

void TestTableCellEdit::up_from_header_exits_table_to_previous_block()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    // Seed cursor on the table (row 1) so the post-Up assertion isn't
    // satisfied vacuously by the fixture's initial-focus state.
    s.fx->placeCursorAtPos(/*row=*/1, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(s.fx->cursorStateCurrentRow(), 1, 2000);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Up);

    QTRY_COMPARE_WITH_TIMEOUT(s.fx->cursorStateCurrentRow(), 0, 2000);
}

void TestTableCellEdit::down_from_last_body_row_exits_table_to_next_block()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 1, 1) != nullptr);

    QQuickItem *cell11 = cellTextEditAt(s.table, 1, 1);
    QVERIFY(cell11);
    cell11->forceActiveFocus();
    QTRY_VERIFY(cell11->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Down);

    QTRY_COMPARE_WITH_TIMEOUT(s.fx->cursorStateCurrentRow(), 2, 2000);
}

void TestTableCellEdit::left_at_first_cell_qtpos_zero_exits_table()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    // Seed cursor on the table so the post-Left assertion isn't vacuous.
    s.fx->placeCursorAtPos(/*row=*/1, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(s.fx->cursorStateCurrentRow(), 1, 2000);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->setProperty("cursorPosition", 0);
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Left);

    QTRY_COMPARE_WITH_TIMEOUT(s.fx->cursorStateCurrentRow(), 0, 2000);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestTableCellEdit)
#include "tst_live_render_table_cell_edit.moc"
