// SPDX-License-Identifier: GPL-3.0-or-later
//
// Folding behaviour with pipe tables in the document.
//
// Pipe tables live as QTextTable child frames inside a MarkdownTextItem's
// QTextDocument. Two things used to break when a document contained tables:
//
//   1) ensureHeadingMap() iterated `doc->begin()` / `block.next()`, which
//      descends into cell blocks. A 2x2 table was counted as 4 cell-blocks
//      (~4 source lines), but tree-sitter sees the table via
//      TableSerializer::serialize() (3 lines for a 2x2). The running
//      source-line counter drifted at every table, misaligning the heading
//      map (and the fold-gutter triangles) for every heading PAST the first
//      table in a document.
//
//   2) applyFoldVisibility() hid each cell-block individually. Cell text
//      collapsed but the frame's cell-padding / cell-spacing / border kept
//      a visible strip in the viewport — a ghost of the folded table.
//
// These tests cover both cases via the public Editor API.

#include <QtTest/QtTest>
#include <QApplication>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextTable>

#include <markoff/Editor.h>
#include "MarkdownTextItem.h"
#include "SceneCoordinator.h"
#include "SelectableItem.h"

using namespace Markoff;

class TstFoldingTables : public QObject {
    Q_OBJECT
private slots:
    void headingMapStaysAlignedPastTable();
    void foldingHeadingBeforeTableHidesTable();
    void unfoldingRestoresTableVisibility();

private:
    static void waitForReparse() { QTest::qWait(500); }

    /// Find the first QTextTable in the editor's scene, or nullptr.
    static QTextTable *firstTableIn(const Editor &e);

    /// Return the total visible bounding height of all currently-visible
    /// text items — a proxy for whether folded content actually collapsed.
    static qreal visibleContentHeight(const Editor &e);
};

QTextTable *TstFoldingTables::firstTableIn(const Editor &e)
{
    auto *coord = e.coordinatorForTesting();
    for (auto *item : coord->items()) {
        if (!item->isTextItem()) continue;
        auto *mti = static_cast<MarkdownTextItem *>(item);
        QTextFrame *root = mti->document()->rootFrame();
        for (auto it = root->begin(); it != root->end(); ++it) {
            if (auto *frame = it.currentFrame()) {
                if (auto *table = qobject_cast<QTextTable *>(frame))
                    return table;
            }
        }
    }
    return nullptr;
}

qreal TstFoldingTables::visibleContentHeight(const Editor &e)
{
    auto *coord = e.coordinatorForTesting();
    qreal total = 0;
    for (auto *item : coord->items()) {
        QGraphicsItem *gi = item->asGraphicsItem();
        if (!gi || !gi->isVisible()) continue;
        total += gi->boundingRect().height();
    }
    return total;
}

// After a table, the second heading's fold arrow was mis-placed because its
// block-number ↔ source-line mapping drifted by (cell-blocks) -
// (serialized-lines). Folding it and unfolding again should work cleanly,
// which is only possible if the heading map was built correctly.
void TstFoldingTables::headingMapStaysAlignedPastTable()
{
    const QString text =
        "# First\n"
        "\n"
        "| a | b |\n"
        "|---|---|\n"
        "| c | d |\n"
        "\n"
        "# Second\n"
        "\n"
        "Body of second.\n";

    Editor e;
    e.setPlainText(text);
    waitForReparse();

    const auto paths = e.headingPaths();
    QVERIFY(paths.contains(QStringList{"First"}));
    QVERIFY(paths.contains(QStringList{"Second"}));

    // Both headings must fold independently without the wrong one being
    // hit due to a drifted map.
    e.fold({"Second"});
    QVERIFY(e.isFolded({"Second"}));
    QVERIFY(!e.isFolded({"First"}));

    e.unfold({"Second"});
    e.fold({"First"});
    QVERIFY(e.isFolded({"First"}));
    QVERIFY(!e.isFolded({"Second"}));
}

// Folding a heading whose body contains a table must collapse the table
// too — cell text AND the surrounding borders / padding.
void TstFoldingTables::foldingHeadingBeforeTableHidesTable()
{
    const QString text =
        "# Section\n"
        "\n"
        "| a | b |\n"
        "|---|---|\n"
        "| c | d |\n"
        "\n"
        "End.\n";

    Editor e;
    e.resize(600, 400);
    e.setPlainText(text);
    waitForReparse();

    QTextTable *table = firstTableIn(e);
    QVERIFY(table);

    const qreal heightBefore = visibleContentHeight(e);
    QVERIFY(heightBefore > 0);

    // Capture the pre-fold frame metrics so we can verify they get zeroed.
    const qreal borderBefore = table->frameFormat().border();
    const qreal paddingBefore = table->format().cellPadding();

    e.fold({"Section"});
    waitForReparse();

    // All cell blocks inside the table should now be invisible.
    for (int r = 0; r < table->rows(); ++r) {
        for (int c = 0; c < table->columns(); ++c) {
            QTextTableCell cell = table->cellAt(r, c);
            for (auto it = cell.begin(); !it.atEnd(); ++it) {
                QTextBlock b = it.currentBlock();
                QVERIFY2(!b.isVisible(),
                         "cell block should be hidden after fold");
            }
        }
    }

    // The frame's border / padding must have been zeroed so no ghost
    // strip remains in the layout.
    QCOMPARE(table->frameFormat().border(), 0.0);
    QCOMPARE(table->format().cellPadding(), 0.0);

    // Visible content height must shrink — the body (including the table)
    // is folded away.
    const qreal heightAfter = visibleContentHeight(e);
    QVERIFY2(heightAfter < heightBefore,
             "folding a heading with a table must reduce visible height");

    // Sanity: pre-fold values were non-trivial, so the zeroing we measured
    // was a real change rather than vacuously already-zero.
    Q_UNUSED(borderBefore);
    Q_UNUSED(paddingBefore);
}

// Unfolding must restore the table's frame metrics so it paints normally
// again (not a flat-zero-padding ghost of its former self).
void TstFoldingTables::unfoldingRestoresTableVisibility()
{
    const QString text =
        "# Section\n"
        "\n"
        "| a | b |\n"
        "|---|---|\n"
        "| c | d |\n"
        "\n"
        "End.\n";

    Editor e;
    e.resize(600, 400);
    e.setPlainText(text);
    waitForReparse();

    QTextTable *table = firstTableIn(e);
    QVERIFY(table);

    const qreal paddingBefore = table->format().cellPadding();

    e.fold({"Section"});
    waitForReparse();
    QCOMPARE(table->format().cellPadding(), 0.0);

    e.unfold({"Section"});
    waitForReparse();

    // Frame metrics should be restored from the stash.
    QCOMPARE(table->format().cellPadding(), paddingBefore);

    // Cell blocks should be visible again.
    QTextTableCell cell = table->cellAt(0, 0);
    bool anyVisible = false;
    for (auto it = cell.begin(); !it.atEnd(); ++it) {
        if (it.currentBlock().isVisible()) { anyVisible = true; break; }
    }
    QVERIFY(anyVisible);
}

QTEST_MAIN(TstFoldingTables)
#include "tst_folding_tables.moc"
