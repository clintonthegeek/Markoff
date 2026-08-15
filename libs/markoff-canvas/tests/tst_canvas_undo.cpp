// SPDX-License-Identifier: GPL-3.0-or-later
//
// T4 — undo/redo caret survival (exit E3).
//
// There is no UndoLog cursor/selection state (plan T4, queue #10 item 2):
// undoD2()/redoD2() mutate the document and nothing else. The T2 "nearest
// surviving block" clamp (View::clampCaret, wired generically through
// onDocumentChanged) is the whole mechanism. Exact position restoration is
// explicitly NOT a criterion here — only that the caret is never left
// pointing at a dead block or an out-of-range byte offset.

#include <algorithm>

#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

class TstCanvasUndo : public QObject {
    Q_OBJECT

private slots:
    void undo_redo_never_strand_caret();
    void consecutive_printable_keys_coalesce_into_one_undo_entry();
};

namespace {
/// Fails the test if the view's caret does not reference a live block at a
/// valid byte offset. Called after every undo/redo step.
void assertCaretSound(Markoff::MarkoffDocument &doc, const View &view)
{
    const BlockId caretBlock = view.caretBlock();
    const auto blocks = doc.iterateBlocks();
    QVERIFY(std::find(blocks.begin(), blocks.end(), caretBlock) != blocks.end());

    const int byteOffset = view.caretByteOffset();
    QVERIFY(byteOffset >= 0);
    QVERIFY(byteOffset <= doc.blockText(caretBlock).size());
}
}  // namespace

void TstCanvasUndo::undo_redo_never_strand_caret()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId firstBlock = doc.iterateBlocks().front();

    // Click mid-paragraph, same spot the T3 structural test uses.
    const QRectF rect = view.blockRect(firstBlock);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 30, int(rect.y()) + 8));
    QCOMPARE(view.caretBlock(), firstBlock);
    QVERIFY(view.caretByteOffset() > 0);

    // type, split, type — three separate undoable transactions.
    QTest::keyClick(&view, Qt::Key_A);
    assertCaretSound(doc, view);

    QTest::keyClick(&view, Qt::Key_Return);
    assertCaretSound(doc, view);
    QCOMPARE(doc.iterateBlocks().size(), size_t(2));

    QTest::keyClick(&view, Qt::Key_B);
    assertCaretSound(doc, view);

    // Undo x3: back to the original single, unedited block.
    for (int i = 0; i < 3; ++i) {
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier);
        assertCaretSound(doc, view);
    }
    QCOMPARE(doc.iterateBlocks().size(), size_t(1));
    QCOMPARE(doc.blockText(doc.iterateBlocks().front()), QByteArray("Hello world."));

    // Redo x3: forward again to the split, edited state.
    for (int i = 0; i < 3; ++i) {
        QTest::keyClick(&view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        assertCaretSound(doc, view);
    }
    QCOMPARE(doc.iterateBlocks().size(), size_t(2));
}

// P7.2a (F1 gap #3) — View::insertPrintable must route through
// Cmd::insertCharacter so consecutive same-block printable keystrokes
// coalesce (UndoLog::maybeCoalesceOrTransaction, 1000ms window) instead of
// each keystroke opening its own bare Transaction. No wall-clock sleep
// needed: the coalesce window is checked against QDateTime timestamps at
// call time, and two QTest::keyClick calls in a row land well inside it.
void TstCanvasUndo::consecutive_printable_keys_coalesce_into_one_undo_entry()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");

    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const BlockId firstBlock = doc.iterateBlocks().front();
    const QRectF rect = view.blockRect(firstBlock);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(int(rect.x()) + 30, int(rect.y()) + 8));
    QCOMPARE(view.caretBlock(), firstBlock);

    const size_t before = doc.d2UndoLog().entryCount();

    QTest::keyClick(&view, Qt::Key_X);
    QTest::keyClick(&view, Qt::Key_Y);

    // Two printable keystrokes, same block, well within the 1000ms
    // coalesce window: exactly one new undo entry, not two.
    QCOMPARE(doc.d2UndoLog().entryCount(), before + 1);
}

QTEST_MAIN(TstCanvasUndo)
#include "tst_canvas_undo.moc"
