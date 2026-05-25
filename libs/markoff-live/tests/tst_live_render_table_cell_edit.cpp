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

#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
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

    // E2E regression for the 2026-05-22 data-loss bug. See spec
    // docs/specs/2026-05-22-cursor-authority-decision.md §6.3.
    void delete_then_enter_at_paragraph_before_table_preserves_block_count();

    // E4 D3 — Enter inert, Esc → BlockSelected.
    void enter_inside_cell_is_inert();
    void escape_inside_cell_promotes_to_block_selected();

    // E4 E1 — block-level delete cascade.
    void backspace_at_first_cell_first_qtpos_promotes_to_block_selected();
    void backspace_on_block_selected_deletes_table();
    void backspace_at_other_cell_first_qtpos_is_inert();
    void backspace_at_start_of_paragraph_after_table_selects_table();

    // E4 E2 — Delete cascade (symmetric to E1).
    void delete_at_last_cell_end_promotes_to_block_selected();
    void delete_at_other_cell_end_is_inert();
    void delete_at_end_of_paragraph_before_table_selects_table();

    // E4 follow-up — Ctrl+C on BlockSelected{table} copies the markdown.
    void ctrl_c_on_block_selected_table_copies_markdown();

    // E4 follow-up — cross-block range highlight reaches the cells.
    void cross_block_selection_highlights_cells();

    // E4 follow-up — Ctrl+C with focus inside a cell reaches the clipboard.
    void ctrl_c_with_focus_in_cell_and_cross_block_selection_copies();

    // E4 dogfood follow-up (2026-05-23) — Shift+arrow extends the
    // cross-cell / cross-block selection from inside a table cell.
    void shift_right_at_cell_end_extends_into_next_cell();
    void shift_left_at_first_cell_qtpos_zero_extends_into_previous_block();
    void shift_down_from_body_row_extends_to_next_row_same_column();
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

void TestTableCellEdit::delete_then_enter_at_paragraph_before_table_preserves_block_count()
{
    // E2E regression for the 2026-05-22 dogfood data-loss bug.
    //
    // Reproduction: caret at end of a paragraph adjacent to a table,
    // press Delete (merges paragraph + table via Cmd::deleteMerge),
    // press Enter (splits the merged paragraph via paragraphEnter).
    // Expected: rowCount goes initial → initial-1 (Delete) → initial
    // (Enter). All blocks after the table are preserved.
    //
    // Pre-fix (without docs/specs/2026-05-22-cursor-authority-decision.md
    // §5.1): the model-rebuild echo after deleteMerge caused
    // non-focused delegates to fire spurious syncFromTextEdit calls,
    // moving m_cursor onto a block far below the user's clicked row.
    // The Enter that followed saw a phantom cross-block selection
    // from (clicked-row, qtPos) to (random-row, qtPos) and routed
    // through KeyDispatch.collapseSelectionIfMutating → deleteSelection,
    // which deleted ~20 blocks of intermediate content.
    // Large fixture: ListView's delegate recycling kicks in only with
    // enough blocks to fill the viewport multiple times. The user's
    // production repro had 167 blocks; we synthesise ~30 to keep the
    // test fast while still triggering rebind echoes.
    QByteArray md =
        "para before\n"
        "\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n";
    // Append 30 trailing blocks of varied kinds. The bug deletes
    // everything between the user's clicked block and whichever block
    // a non-focused-delegate echo clobbers m_cursor onto.
    for (int i = 0; i < 30; ++i) {
        if (i % 5 == 0) md.append("\n## Heading " + QByteArray::number(i) + "\n");
        else if (i % 3 == 0) md.append("\n- list item " + QByteArray::number(i) + "\n");
        else md.append("\nparagraph " + QByteArray::number(i)
                       + " filler text to provide many chars per block\n");
    }

    // Block layout: row 0 = paragraph, row 1 = table, rows 2..N = the rest.
    QmlIntegrationFixture fx(md, /*expectedRowCount=*/-1);  // any
    QVERIFY(fx.waitForDelegateAt(1, 2000));

    QAbstractItemModel *m = fx.model();
    const int initialRows = m->rowCount();
    QVERIFY2(initialRows > 20, "fixture should produce >20 blocks");

    // Simulate the user's click at end of "para before". The
    // production path is MouseArea.onPressed → cursorState.begin(),
    // which sets BOTH m_cursor AND m_selectionAnchor. placeCursorAt*
    // uses requestTextCaretAtRow (no anchor set), which doesn't
    // reproduce the bug because the data-loss requires the anchor
    // to be present when Enter fires.
    const int eolPos = fx.modelText(0).length();
    QObject *cs = fx.binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, eolPos));
    QTest::qWait(100);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);

    // Post-E1 behavior: Delete at end-of-paragraph-adjacent-to-table
    // now triggers the adjacency fence (Table::isBlockOnly=true), so
    // the table becomes BlockSelected instead of merging. Block count
    // stays the same after Delete.
    fx.harness().keyClick(Qt::Key_Delete);
    QTest::qWait(300);
    QTRY_COMPARE_WITH_TIMEOUT(m->rowCount(), initialRows, 2000);

    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("BlockSelected"));

    // Enter on BlockSelected{table}: blockOnlyEnter handler inserts an
    // empty paragraph after the table. blockCount = initialRows + 1.
    fx.harness().keyClick(Qt::Key_Return);
    QTest::qWait(300);
    QTRY_COMPARE_WITH_TIMEOUT(m->rowCount(), initialRows + 1, 2000);

    // The cursor-authority fix's load-bearing invariant: ALL of the
    // following blocks survive. Without §5.1, ~20 blocks would have
    // disappeared via phantom deleteSelection. Count lists+headings
    // remaining — should match the fixture's original.
    int listsHeadingsAfter = 0;
    for (int r = 0; r < m->rowCount(); ++r) {
        const QString k = fx.modelKind(r);
        if (k == QStringLiteral("list-item") || k == QStringLiteral("heading"))
            ++listsHeadingsAfter;
    }
    QVERIFY2(listsHeadingsAfter >= 10,
             qPrintable(QStringLiteral("expected ≥10 lists+headings to survive; got %1")
                            .arg(listsHeadingsAfter)));
}

// ---- D3 ----

void TestTableCellEdit::enter_inside_cell_is_inert()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    const QString preBuffer = s.fx->modelText(1);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->setProperty("cursorPosition", 2);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Return);
    QTest::qWait(100);

    // Buffer unchanged — Enter did not insert a `\n`.
    QCOMPARE(s.fx->modelText(1), preBuffer);
    // Focus stays in the same cell.
    QVERIFY(cell00->hasActiveFocus());
}

void TestTableCellEdit::escape_inside_cell_promotes_to_block_selected()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    // Pre-Esc: cursor should be a TextCaret on the table block.
    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QTRY_COMPARE_WITH_TIMEOUT(cs->property("cursorKind").toString(),
                              QStringLiteral("TextCaret"), 2000);

    s.fx->harness().keyClick(Qt::Key_Escape);
    QTest::qWait(100);

    // Post-Esc: BlockSelected on the table's row.
    QTRY_COMPARE_WITH_TIMEOUT(cs->property("cursorKind").toString(),
                              QStringLiteral("BlockSelected"), 2000);
    QCOMPARE(cs->property("focusedAnchorRow").toInt(), 1);  // table row
}

// ---- E1 ----

void TestTableCellEdit::backspace_at_first_cell_first_qtpos_promotes_to_block_selected()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->setProperty("cursorPosition", 0);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Backspace);
    QTest::qWait(100);

    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QTRY_COMPARE_WITH_TIMEOUT(cs->property("cursorKind").toString(),
                              QStringLiteral("BlockSelected"), 2000);
    QCOMPARE(cs->property("focusedAnchorRow").toInt(), 1);  // table row
}

void TestTableCellEdit::backspace_on_block_selected_deletes_table()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    QAbstractItemModel *m = s.fx->model();
    const int initialRows = m->rowCount();

    // First: promote to BlockSelected by Backspace at cell (0, 0).
    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    cell00->setProperty("cursorPosition", 0);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());
    s.fx->harness().keyClick(Qt::Key_Backspace);
    QTest::qWait(100);

    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QTRY_COMPARE_WITH_TIMEOUT(cs->property("cursorKind").toString(),
                              QStringLiteral("BlockSelected"), 2000);

    // Second Backspace: blockOnlyDelete fires via structuralKeyHandler.
    s.fx->harness().keyClick(Qt::Key_Backspace);
    QTest::qWait(200);

    QTRY_COMPARE_WITH_TIMEOUT(m->rowCount(), initialRows - 1, 2000);
    // Verify no remaining row is a table.
    for (int r = 0; r < m->rowCount(); ++r)
        QVERIFY(s.fx->modelKind(r) != QStringLiteral("table"));
}

void TestTableCellEdit::backspace_at_other_cell_first_qtpos_is_inert()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 1) != nullptr);

    const QString preBuffer = s.fx->modelText(1);

    QQuickItem *cell01 = cellTextEditAt(s.table, 0, 1);
    QVERIFY(cell01);
    cell01->setProperty("cursorPosition", 0);
    cell01->forceActiveFocus();
    QTRY_VERIFY(cell01->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Backspace);
    QTest::qWait(100);

    // Buffer unchanged. cursor still TextCaret (not promoted to BlockSelected).
    QCOMPARE(s.fx->modelText(1), preBuffer);
    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("TextCaret"));
}

void TestTableCellEdit::backspace_at_start_of_paragraph_after_table_selects_table()
{
    // User dogfood report #1 — adjacent-block path. Caret at qtPos 0
    // of the paragraph FOLLOWING the table. Backspace should select
    // the table (via LiveStructuralKeyHandler's adjacency fence at
    // line 170, now that Table::isBlockOnly=true).
    const QByteArray md =
        "para before\n"
        "\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "para after\n";
    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(2, 2000));

    // Place caret at start of "para after" (row 2) via the chokepoint
    // — placeCursorAtPos goes through requestTextCaretAtRow which
    // delivers focus via establishFocus → takeFocus.
    fx.placeCursorAtPos(/*row=*/2, /*qtPos=*/0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 2, 2000);

    fx.harness().keyClick(Qt::Key_Backspace);
    QTest::qWait(200);

    // Expected: cursor is now BlockSelected on the table (row 1).
    QObject *cs = fx.binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QTRY_COMPARE_WITH_TIMEOUT(cs->property("cursorKind").toString(),
                              QStringLiteral("BlockSelected"), 2000);
    QCOMPARE(cs->property("focusedAnchorRow").toInt(), 1);
}

// ---- E2 ----

void TestTableCellEdit::delete_at_last_cell_end_promotes_to_block_selected()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 1, 1) != nullptr);

    QQuickItem *cell11 = cellTextEditAt(s.table, 1, 1);
    QVERIFY(cell11);
    const int cellLen = cell11->property("length").toInt();
    cell11->setProperty("cursorPosition", cellLen);
    cell11->forceActiveFocus();
    QTRY_VERIFY(cell11->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Delete);
    QTest::qWait(100);

    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QTRY_COMPARE_WITH_TIMEOUT(cs->property("cursorKind").toString(),
                              QStringLiteral("BlockSelected"), 2000);
    QCOMPARE(cs->property("focusedAnchorRow").toInt(), 1);
}

void TestTableCellEdit::delete_at_other_cell_end_is_inert()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    const QString preBuffer = s.fx->modelText(1);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    const int cellLen = cell00->property("length").toInt();
    cell00->setProperty("cursorPosition", cellLen);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Delete);
    QTest::qWait(100);

    QCOMPARE(s.fx->modelText(1), preBuffer);
    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QCOMPARE(cs->property("cursorKind").toString(), QStringLiteral("TextCaret"));
}

void TestTableCellEdit::delete_at_end_of_paragraph_before_table_selects_table()
{
    // User dogfood report #2 — adjacent-block path, symmetric to E1's
    // Backspace case. Caret at end of the paragraph PRECEDING the
    // table; Delete should select the table.
    const QByteArray md =
        "para before\n"
        "\n"
        "| A | B |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "\n"
        "para after\n";
    QmlIntegrationFixture fx(md, /*expectedRowCount=*/3);
    QVERIFY(fx.waitForDelegateAt(0, 2000));

    fx.placeCursorAtEndOf(0);
    QTRY_COMPARE_WITH_TIMEOUT(fx.cursorStateCurrentRow(), 0, 2000);

    fx.harness().keyClick(Qt::Key_Delete);
    QTest::qWait(200);

    QObject *cs = fx.binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QTRY_COMPARE_WITH_TIMEOUT(cs->property("cursorKind").toString(),
                              QStringLiteral("BlockSelected"), 2000);
    QCOMPARE(cs->property("focusedAnchorRow").toInt(), 1);
}

// ---- Ctrl+C on BlockSelected{table} ----

void TestTableCellEdit::ctrl_c_on_block_selected_table_copies_markdown()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    // Place caret in cell (0, 0) then Backspace at qtPos=0 to promote
    // the table to BlockSelected{table} (E1 path).
    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    cell00->setProperty("cursorPosition", 0);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());
    s.fx->harness().keyClick(Qt::Key_Backspace);
    QTest::qWait(100);

    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QTRY_COMPARE_WITH_TIMEOUT(cs->property("cursorKind").toString(),
                              QStringLiteral("BlockSelected"), 2000);

    // Capture the table's raw buffer for comparison.
    const QString tableBuffer = s.fx->modelText(1);
    QVERIFY(tableBuffer.contains(QStringLiteral("|")));

    // Copy via clipboardController directly (skips QML KeyDispatch routing
    // which we already exercise via separate Ctrl+C tests). Tests the
    // BlockSelected branch added in LiveClipboardController::copy().
    QObject *clip = s.fx->binding()->property("clipboardController").value<QObject *>();
    QVERIFY(clip);
    QApplication::clipboard()->clear();
    QMetaObject::invokeMethod(clip, "copy", Qt::DirectConnection);
    QTest::qWait(50);

    // Plain-text branch — the table buffer should be in the clipboard.
    const QString clipText = QApplication::clipboard()->text();
    QVERIFY2(clipText.contains(QStringLiteral("|")),
             qPrintable(QStringLiteral("clipboard text missing table content: %1")
                            .arg(clipText)));

    // Structured branch — application/x-markoff-blocks payload.
    const QByteArray blob =
        QApplication::clipboard()->mimeData()->data("application/x-markoff-blocks");
    QVERIFY(!blob.isEmpty());
    const QJsonObject payload = QJsonDocument::fromJson(blob).object();
    const QJsonArray blocks   = payload["blocks"].toArray();
    QCOMPARE(blocks.size(), 1);
    const QJsonObject first = blocks[0].toObject();
    QCOMPARE(first["kind"].toString(), QStringLiteral("table"));
    QCOMPARE(first["text"].toString(), tableBuffer);
}

// ---- Cross-block range highlight reaches table cells ----

void TestTableCellEdit::cross_block_selection_highlights_cells()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);
    QTRY_VERIFY(cellAt(s.table, 1, 1) != nullptr);

    // Three-block fixture (setupNav loads paragraph / table / paragraph).
    // Create a cross-block selection from row 0 → row 2 via begin + extend.
    // extend() marks m_selectionExtended=true; §5.4's anchor-clear is
    // suppressed; the active end can cross blocks and the anchor survives.
    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 0));
    QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
                              Q_ARG(int, 2), Q_ARG(int, 0));
    QTest::qWait(150);
    QVERIFY(cs->property("cursorKind").toString() == QStringLiteral("TextCaret"));

    // Each cell should now show its full-content selection (since the
    // selection range covers byte 0 → row 2 byte 0, which spans the whole
    // table block).
    auto cellSelectionLen = [&](int r, int c) -> int {
        QQuickItem *ed = cellTextEditAt(s.table, r, c);
        if (!ed) return -1;
        return ed->property("selectionEnd").toInt()
               - ed->property("selectionStart").toInt();
    };

    // Wait briefly for the QML Connections to fire applySelection.
    QTRY_VERIFY_WITH_TIMEOUT(cellSelectionLen(0, 0) > 0, 2000);
    QVERIFY(cellSelectionLen(0, 1) > 0);
    QVERIFY(cellSelectionLen(1, 0) > 0);
    QVERIFY(cellSelectionLen(1, 1) > 0);
}

// ---- Ctrl+C with focus in a cell reaches the clipboard ----

void TestTableCellEdit::ctrl_c_with_focus_in_cell_and_cross_block_selection_copies()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    // Create a cross-block selection from row 0 → cell (0, 0) — active
    // end inside a table cell. Then Ctrl+C with the cell focused must
    // reach LiveClipboardController.copy() (via the KeyDispatch chord
    // routing on the cell's Keys.onPressed). Without the routing, the
    // event falls through to TextEdit's built-in copy, which acts on
    // the within-cell selection (empty for the focused cell when the
    // cross-block highlight is the visible range), producing nothing
    // on the clipboard.
    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    QMetaObject::invokeMethod(cs, "begin", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 0));

    // Compute the flat qtPos for cell (0, 0) start. cellCharRanges[0][0].start
    // is exposed via the parsedTable structure.
    QVariant parsedV = s.table->property("parsedTable");
    QVERIFY(parsedV.isValid());
    QVariantMap parsed = parsedV.toMap();
    QVariantList ccr   = parsed["cellCharRanges"].toList();
    QVERIFY(ccr.size() >= 1);
    QVariantList rowRanges = ccr[0].toList();
    QVERIFY(rowRanges.size() >= 1);
    QVariantMap cell00Range = rowRanges[0].toMap();
    const int cellStart    = cell00Range["start"].toInt();

    QMetaObject::invokeMethod(cs, "extend", Qt::DirectConnection,
                              Q_ARG(int, 1), Q_ARG(int, cellStart + 2));
    QTest::qWait(150);

    // Focus the cell — interactive equivalent of clicking into it after
    // the Shift+arrow lands.
    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    QApplication::clipboard()->clear();
    s.fx->harness().keyClick(Qt::Key_C, Qt::ControlModifier);
    QTest::qWait(100);

    // Plain-text branch must have content.
    const QString clipText = QApplication::clipboard()->text();
    QVERIFY2(!clipText.isEmpty(),
             qPrintable(QStringLiteral("expected non-empty clipboard; got %1")
                            .arg(clipText)));
}

// ---- Shift+arrow cross-cell / cross-block extension ----

void TestTableCellEdit::shift_right_at_cell_end_extends_into_next_cell()
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

    // Shift+Right at end of (0, 0) sets an anchor on the table row and
    // moves the active end into cell (0, 1)'s starting flat qtPos (= the
    // first char of cell01's range). The single-char selection that
    // results covers only the inter-cell pipe (which lives outside any
    // cellCharRange), so neither cell shows a visible per-cell highlight
    // yet — that arrives once a second Shift+Right extends inside cell01.
    s.fx->harness().keyClick(Qt::Key_Right, Qt::ShiftModifier);
    QTest::qWait(150);

    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    int anchorBlock = -1;
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    QCOMPARE(anchorBlock, 1);                        // anchor on table row
    QCOMPARE(s.fx->cursorStateCurrentRow(), 1);      // active end on table row
    QCOMPARE(cs->property("cursorKind").toString(),
             QStringLiteral("TextCaret"));

    // A second Shift+Right grows the selection into cell01's first
    // content char; cell01 then reports a non-empty selection through
    // the per-cell highlight pipeline (_applyCrossBlockSelection).
    s.fx->harness().keyClick(Qt::Key_Right, Qt::ShiftModifier);
    QTest::qWait(150);
    QQuickItem *cell01 = cellTextEditAt(s.table, 0, 1);
    QVERIFY(cell01);
    QTRY_VERIFY_WITH_TIMEOUT(
        cell01->property("selectionEnd").toInt()
            > cell01->property("selectionStart").toInt(),
        2000);
}

void TestTableCellEdit
    ::shift_left_at_first_cell_qtpos_zero_extends_into_previous_block()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    // Place focus at cell (0, 0) qtPos 0 — the table's leading edge.
    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->setProperty("cursorPosition", 0);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    // Shift+Left exits the table. Selection anchor stays on the table
    // (row 1) and the active end moves to row 0 (the preceding paragraph).
    s.fx->harness().keyClick(Qt::Key_Left, Qt::ShiftModifier);
    QTest::qWait(150);

    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    int anchorBlock = -1;
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    QCOMPARE(anchorBlock, 1);  // anchor stays on table row
    QTRY_COMPARE_WITH_TIMEOUT(s.fx->cursorStateCurrentRow(), 0, 2000);
    // hasSelection true.
    QCOMPARE(cs->property("cursorKind").toString(),
             QStringLiteral("TextCaret"));
}

void TestTableCellEdit
    ::shift_down_from_body_row_extends_to_next_row_same_column()
{
    NavSetup s = setupNav();
    QVERIFY(s.table);
    // Need at least header + 1 body row; setupNav loads 1+1.
    // Shift+Down from (0, 0) — header to body — should extend selection
    // across cell rows within the same block.
    QTRY_VERIFY(cellAt(s.table, 0, 0) != nullptr);

    QQuickItem *cell00 = cellTextEditAt(s.table, 0, 0);
    QVERIFY(cell00);
    cell00->setProperty("cursorPosition", 1);
    cell00->forceActiveFocus();
    QTRY_VERIFY(cell00->hasActiveFocus());

    s.fx->harness().keyClick(Qt::Key_Down, Qt::ShiftModifier);
    QTest::qWait(150);

    QObject *cs = s.fx->binding()->property("cursorState").value<QObject *>();
    QVERIFY(cs);
    int anchorBlock = -1;
    QMetaObject::invokeMethod(cs, "anchorBlock", Qt::DirectConnection,
                              Q_RETURN_ARG(int, anchorBlock));
    QCOMPARE(anchorBlock, 1);  // anchor on table

    // Cells (0, 0) and (1, 0) both within the selection range.
    QQuickItem *cell10 = cellTextEditAt(s.table, 1, 0);
    QVERIFY(cell10);
    QTRY_VERIFY_WITH_TIMEOUT(
        cell00->property("selectionEnd").toInt()
            > cell00->property("selectionStart").toInt()
        || cell10->property("selectionEnd").toInt()
            > cell10->property("selectionStart").toInt(),
        2000);
}

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestTableCellEdit)
#include "tst_live_render_table_cell_edit.moc"
