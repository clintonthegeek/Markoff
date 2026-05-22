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

}  // namespace Markoff::Live::Test

QTEST_MAIN(Markoff::Live::Test::TestTableCellEdit)
#include "tst_live_render_table_cell_edit.moc"
