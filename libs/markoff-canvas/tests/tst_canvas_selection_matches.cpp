// SPDX-License-Identifier: GPL-3.0-or-later
//
// P7.2e (F1 #7) — highlight other occurrences of the current selection.
// `View::recomputeOccurrenceHighlights()`, hooked into
// `pushSelectionToSession()` (the existing selection-change chokepoint),
// rebuilds `m_occurrenceHighlightsByBlock` from the selected text: every
// OTHER occurrence of that exact text in the realized entries, case-
// sensitive, excluding the active selection's own span. CM
// `search/src/selection-match.ts` `highlightSelectionMatches` reference:
// `minSelectionLength` (here: 2, whitespace-only selections don't count
// either) and no `wholeWords` requirement by default.

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

class TstCanvasSelectionMatches : public QObject {
    Q_OBJECT

private slots:
    void selecting_a_word_highlights_its_other_occurrences();
    void trivial_and_whitespace_selections_are_not_highlighted();
    void collapsing_the_selection_clears_highlights();
};

void TstCanvasSelectionMatches::selecting_a_word_highlights_its_other_occurrences()
{
    // "cat" occurs at byte 0, 8, 16 (single block, 3 occurrences total).
    const QByteArray src = "cat sat cat mat cat\n";
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));
    const BlockId block = blocks[0];

    // Select the FIRST "cat" (byte 0..3) via caret + Shift+Right x3.
    view.setCaretPosition(block, 0);
    for (int i = 0; i < 3; ++i)
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorByteOffset(), 0);
    QCOMPARE(view.caretByteOffset(), 3);

    const QList<std::pair<int, int>> occ = view.occurrenceHighlightsForBlock(block);
    // Exactly the OTHER two "cat"s (byte 8 and byte 16) — not the active
    // selection's own span (byte 0).
    QCOMPARE(occ.size(), 2);
    QVERIFY(occ.contains(std::make_pair(8, 3)));
    QVERIFY(occ.contains(std::make_pair(16, 3)));
    for (const auto &[byteOffset, byteLength] : occ)
        QVERIFY(byteOffset != 0);
}

void TstCanvasSelectionMatches::trivial_and_whitespace_selections_are_not_highlighted()
{
    const QByteArray src = "aa bb aa   aa\n";
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const BlockId block = blocks[0];

    // Single-char selection ("a", byte 0..1) — below the min-length floor.
    view.setCaretPosition(block, 0);
    QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QVERIFY(view.hasSelection());
    QCOMPARE(view.occurrenceHighlightsForBlock(block).size(), 0);

    // Whitespace-only selection (the 3-space run "aa bb aa[   ]aa", byte
    // 8..11) — long enough, but whitespace-only.
    view.setCaretPosition(block, 8);
    for (int i = 0; i < 3; ++i)
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QVERIFY(view.hasSelection());
    QCOMPARE(view.occurrenceHighlightsForBlock(block).size(), 0);
}

void TstCanvasSelectionMatches::collapsing_the_selection_clears_highlights()
{
    const QByteArray src = "cat sat cat\n";
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    const BlockId block = blocks[0];

    view.setCaretPosition(block, 0);
    for (int i = 0; i < 3; ++i)
        QTest::keyClick(&view, Qt::Key_Right, Qt::ShiftModifier);
    QVERIFY(view.hasSelection());
    QCOMPARE(view.occurrenceHighlightsForBlock(block).size(), 1);

    // Plain Right collapses the selection to a bare caret.
    QTest::keyClick(&view, Qt::Key_Right);
    QVERIFY(!view.hasSelection());
    QCOMPARE(view.occurrenceHighlightsForBlock(block).size(), 0);
}

QTEST_MAIN(TstCanvasSelectionMatches)
#include "tst_canvas_selection_matches.moc"
