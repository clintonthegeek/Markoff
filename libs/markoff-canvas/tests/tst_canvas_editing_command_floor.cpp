// SPDX-License-Identifier: GPL-3.0-or-later
//
// P7.2b (F1 #1) — the editing-command floor: word-wise motion + selection
// (Ctrl+Left/Right, Ctrl+Shift+Left/Right), word-wise delete (Ctrl+
// Backspace/Delete), Ctrl+Home/End (document start/end, caret AND scroll),
// delete-line (Ctrl+Shift+K), move-line up/down (Alt+Up/Alt+Down),
// select-line (Alt+L), and Esc-simplify-selection.
//
// Word boundaries are computed with QTextBoundaryFinder (the plan's own
// requirement, F1's note) — the exact stop positions asserted below were
// derived empirically from a standalone QTextBoundaryFinder probe against
// each test string, not assumed.

#include <QScrollBar>
#include <QTest>

#include <markoff/canvas/View.h>
#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffDocument.h>

using Markoff::BlockId;
using Markoff::Canvas::View;

class TstCanvasEditingCommandFloor : public QObject {
    Q_OBJECT

private slots:
    void word_motion_selection_and_delete();
    void document_start_end();
    void delete_move_select_line();
    void escape_simplifies_selection();
};

void TstCanvasEditingCommandFloor::word_motion_selection_and_delete()
{
    // "Hello world foo." (16 bytes) + "Second block." — forward word
    // boundaries in block 0 (probed): 5, 11, 15, 16, then cross into block 1.
    const QByteArray src = "Hello world foo.\n\nSecond block.\n";
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(2));

    view.setCaretPosition(blocks[0], 0);

    const int expectedFwd[] = {5, 11, 15, 16};
    for (int expected : expectedFwd) {
        QTest::keyClick(&view, Qt::Key_Right, Qt::ControlModifier);
        QCOMPARE(view.caretBlock(), blocks[0]);
        QCOMPARE(view.caretByteOffset(), expected);
    }

    // One more Ctrl+Right at block end crosses into block 1, byte 0 — same
    // "land at the adjacent visible block's own edge" convention
    // moveCaretHorizontally already uses for plain char motion.
    QTest::keyClick(&view, Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(view.caretBlock(), blocks[1]);
    QCOMPARE(view.caretByteOffset(), 0);

    // Ctrl+Shift+Left extends the selection backward by word, crossing back
    // into block 0's end (16) — anchor stays at block 1 byte 0.
    QTest::keyClick(&view, Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorBlock(), blocks[1]);
    QCOMPARE(view.selectionAnchorByteOffset(), 0);
    QCOMPARE(view.caretBlock(), blocks[0]);
    QCOMPARE(view.caretByteOffset(), 16);

    // ---- Word-wise delete (Ctrl+Backspace), separate document -----------
    // "alpha beta gamma" (16 bytes); previousWordBoundary(16) == 11 (start
    // of "gamma") — Ctrl+Backspace from the end removes exactly "gamma".
    Markoff::MarkoffDocument doc2;
    doc2.loadFromMarkdown("alpha beta gamma\n");
    View view2;
    view2.resize(400, 300);
    view2.setDocument(&doc2);
    view2.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view2));
    const auto blocks2 = doc2.iterateBlocks();
    QCOMPARE(blocks2.size(), size_t(1));
    view2.setCaretPosition(blocks2[0], 16);

    QTest::keyClick(&view2, Qt::Key_Backspace, Qt::ControlModifier);
    QCOMPARE(doc2.blockText(blocks2[0]), QByteArray("alpha beta "));
    QCOMPARE(view2.caretByteOffset(), 11);

    // Ctrl+Delete from byte 0 removes the next word ("alpha") only, not the
    // trailing space (nextWordBoundary excludes it).
    view2.setCaretPosition(blocks2[0], 0);
    QTest::keyClick(&view2, Qt::Key_Delete, Qt::ControlModifier);
    QCOMPARE(doc2.blockText(blocks2[0]), QByteArray(" beta "));
}

void TstCanvasEditingCommandFloor::document_start_end()
{
    const QByteArray src = "First block.\n\nMiddle block.\n\nLast block.\n";
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown(src);
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(3));

    // Start mid-document, on the middle block.
    view.setCaretPosition(blocks[1], 3);

    QTest::keyClick(&view, Qt::Key_End, Qt::ControlModifier);
    QCOMPARE(view.caretBlock(), blocks[2]);
    QCOMPARE(view.caretByteOffset(), doc.blockText(blocks[2]).size());
    QCOMPARE(view.verticalScrollBar()->value(), view.verticalScrollBar()->maximum());

    QTest::keyClick(&view, Qt::Key_Home, Qt::ControlModifier);
    QCOMPARE(view.caretBlock(), blocks[0]);
    QCOMPARE(view.caretByteOffset(), 0);
    QCOMPARE(view.verticalScrollBar()->value(), view.verticalScrollBar()->minimum());

    // Ctrl+Shift+End extends a selection from the current caret to doc end.
    QTest::keyClick(&view, Qt::Key_End, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(view.hasSelection());
    QCOMPARE(view.selectionAnchorBlock(), blocks[0]);
    QCOMPARE(view.selectionAnchorByteOffset(), 0);
    QCOMPARE(view.caretBlock(), blocks[2]);
    QCOMPARE(view.caretByteOffset(), doc.blockText(blocks[2]).size());
}

void TstCanvasEditingCommandFloor::delete_move_select_line()
{
    // ---- select-line (Alt+L) ---------------------------------------------
    {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown("Alpha.\n\nBeta.\n\nGamma.\n");
        View view;
        view.resize(400, 300);
        view.setDocument(&doc);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(blocks.size(), size_t(3));

        view.setCaretPosition(blocks[1], 2);
        QTest::keyClick(&view, Qt::Key_L, Qt::AltModifier);
        QVERIFY(view.hasSelection());
        QCOMPARE(view.selectionAnchorBlock(), blocks[1]);
        QCOMPARE(view.selectionAnchorByteOffset(), 0);
        QCOMPARE(view.caretBlock(), blocks[1]);
        QCOMPARE(view.caretByteOffset(), doc.blockText(blocks[1]).size());
    }

    // ---- delete-line (Ctrl+Shift+K) ---------------------------------------
    {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown("Alpha.\n\nBeta.\n\nGamma.\n");
        View view;
        view.resize(400, 300);
        view.setDocument(&doc);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto blocks = doc.iterateBlocks();

        view.setCaretPosition(blocks[1], 2);
        QTest::keyClick(&view, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(doc.blockText(blocks[1]).isEmpty());
        QCOMPARE(view.caretBlock(), blocks[1]);
        QCOMPARE(view.caretByteOffset(), 0);
        // Sibling blocks untouched.
        QCOMPARE(doc.blockText(blocks[0]), QByteArray("Alpha."));
        QCOMPARE(doc.blockText(blocks[2]), QByteArray("Gamma."));
    }

    // ---- move-line down / up (Alt+Down / Alt+Up) --------------------------
    {
        Markoff::MarkoffDocument doc;
        doc.loadFromMarkdown("Alpha.\n\nBeta.\n\nGamma.\n");
        View view;
        view.resize(400, 300);
        view.setDocument(&doc);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(blocks.size(), size_t(3));

        // Caret in block 0 ("Alpha."), Alt+Down swaps it with block 1.
        view.setCaretPosition(blocks[0], 3);
        QTest::keyClick(&view, Qt::Key_Down, Qt::AltModifier);

        QCOMPARE(doc.blockText(blocks[0]), QByteArray("Beta."));
        QCOMPARE(doc.blockText(blocks[1]), QByteArray("Alpha."));
        QCOMPARE(doc.blockText(blocks[2]), QByteArray("Gamma."));
        // Document order (BlockId identity) unchanged — only content moved.
        const auto blocksAfterDown = doc.iterateBlocks();
        QCOMPARE(blocksAfterDown, blocks);
        // The caret follows its content to block 1, same byte offset.
        QCOMPARE(view.caretBlock(), blocks[1]);
        QCOMPARE(view.caretByteOffset(), 3);

        // Alt+Up from block 1 swaps it back with block 0.
        QTest::keyClick(&view, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(doc.blockText(blocks[0]), QByteArray("Alpha."));
        QCOMPARE(doc.blockText(blocks[1]), QByteArray("Beta."));
        QCOMPARE(view.caretBlock(), blocks[0]);
        QCOMPARE(view.caretByteOffset(), 3);

        // Alt+Up at the document's first block is a no-op (nothing to swap
        // with).
        view.setCaretPosition(blocks[0], 0);
        QTest::keyClick(&view, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(doc.blockText(blocks[0]), QByteArray("Alpha."));
        QCOMPARE(view.caretBlock(), blocks[0]);
    }
}

void TstCanvasEditingCommandFloor::escape_simplifies_selection()
{
    Markoff::MarkoffDocument doc;
    doc.loadFromMarkdown("Hello world.\n");
    View view;
    view.resize(400, 300);
    view.setDocument(&doc);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    const auto blocks = doc.iterateBlocks();
    QCOMPARE(blocks.size(), size_t(1));

    QTest::keyClick(&view, Qt::Key_A, Qt::ControlModifier);
    QVERIFY(view.hasSelection());
    const BlockId activeBlock = view.caretBlock();
    const int activeByte = view.caretByteOffset();

    QTest::keyClick(&view, Qt::Key_Escape);
    QVERIFY(!view.hasSelection());
    // The caret stays at the selection's active end (the end selectAll()
    // left it at), not the anchor.
    QCOMPARE(view.caretBlock(), activeBlock);
    QCOMPARE(view.caretByteOffset(), activeByte);

    // A second Escape with nothing to simplify is a harmless no-op.
    QTest::keyClick(&view, Qt::Key_Escape);
    QVERIFY(!view.hasSelection());
    QCOMPARE(view.caretBlock(), activeBlock);
    QCOMPARE(view.caretByteOffset(), activeByte);
}

QTEST_MAIN(TstCanvasEditingCommandFloor)
#include "tst_canvas_editing_command_floor.moc"
