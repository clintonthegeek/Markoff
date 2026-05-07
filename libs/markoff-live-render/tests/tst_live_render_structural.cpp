// SPDX-License-Identifier: GPL-3.0-or-later
//
// D2 migration: structural key tests use loadFromMarkdown + structureChanged
// to populate the model. UndoCoalescer tests are removed (UndoCoalescer is
// retired in D2 — coalescing is now built into Cmd::insertCharacter via
// UndoLog::maybeCoalesceOrTransaction). All handlers now use Cmd::* calls.

#include <QTest>
#include <QSignalSpy>

#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/Cursor.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/CrdtProxies.h>
#include <markoff-foundation/BlockAnchor.h>

using namespace Markoff::LiveRender;

// Helper: load markdown into a D2 document and wait for the model to populate.
// loadFromMarkdown fires structureChanged synchronously; rows are typically
// populated immediately.
static bool waitForModelRows(LiveListModelBinding &binding,
                              Markoff::MarkoffDocument &doc,
                              const QByteArray &content,
                              int expectedRows,
                              int timeoutMs = 2000)
{
    doc.loadFromMarkdown(content);
    if (binding.model()->rowCount() == expectedRows)
        return true;
    QSignalSpy spy(doc.idListProxy(), &Markoff::IdListProxy::structureChanged);
    if (!spy.wait(timeoutMs))
        return binding.model()->rowCount() == expectedRows;
    return binding.model()->rowCount() == expectedRows;
}

class TstLiveRenderStructural : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---------- LiveStructuralKeyHandler — paragraph Enter (all positions) ----------

    void enter_at_end_of_paragraph_creates_new_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "hello world", 1));

        const auto *handler = binding.structuralKeyHandler();
        QVERIFY(handler);

        const int blocksBefore = static_cast<int>(doc.iterateBlocks().size());

        const bool consumed = const_cast<LiveStructuralKeyHandler*>(handler)->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/11,   // end of "hello world"
            /*selectionEmpty=*/true,
            QStringLiteral("hello world"));
        QVERIFY(consumed);

        // A new block should be born.
        QCOMPARE(static_cast<int>(doc.iterateBlocks().size()), blocksBefore + 1);

        // Wait for model update and verify cursor.
        QTRY_COMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.cursorState()->focusedAnchorRow(), 1);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 0);
    }

    void enter_in_middle_of_paragraph_splits_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "hello world", 1));

        const Markoff::BlockId origBlock = binding.model()->recordAt(0).blockAnchor;

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("hello world"));
        QVERIFY(consumed);

        // Two blocks should now exist.
        QCOMPARE(static_cast<int>(doc.iterateBlocks().size()), 2);

        // Original block text should be "hello", new block " world".
        QCOMPARE(doc.blockText(origBlock), QByteArrayLiteral("hello"));

        const auto ids = doc.iterateBlocks();
        QCOMPARE(doc.blockText(ids[1]), QByteArrayLiteral(" world"));

        // Cursor should land in the new block (row 1).
        QTRY_COMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.cursorState()->focusedAnchorRow(), 1);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 0);
    }

    void enter_at_start_of_paragraph_inserts_block_before() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "hello world", 1));

        const Markoff::BlockId origBlock = binding.model()->recordAt(0).blockAnchor;

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("hello world"));
        QVERIFY(consumed);

        // Two blocks: new empty block first, original "hello world" second.
        QCOMPARE(static_cast<int>(doc.iterateBlocks().size()), 2);

        // Original block content must be preserved.
        QCOMPARE(doc.blockText(origBlock), QByteArrayLiteral("hello world"));

        // Cursor stays at the new empty block (same visual row), not the
        // shifted content. origBlock is now at row 1.
        QTRY_COMPARE(binding.model()->rowCount(), 2);
        QTRY_VERIFY(binding.cursorState()->focusedAnchorRow() >= 0);
        const int row = binding.cursorState()->focusedAnchorRow();
        // Row 0 is the new empty block; row 1 is "hello world".
        QCOMPARE(row, 0);
        QVERIFY(binding.model()->recordAt(row).blockAnchor != origBlock);
    }

    // ---------- LiveStructuralKeyHandler — paragraph Backspace at row-start ----------

    void backspace_at_start_of_paragraph_merges_with_previous() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "alpha\n\nbeta", 2));
        QCOMPARE(binding.model()->rowCount(), 2);

        const Markoff::BlockId block0 = binding.model()->recordAt(0).blockAnchor;
        const Markoff::BlockId block1 = binding.model()->recordAt(1).blockAnchor;

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/1, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("beta"));
        QVERIFY(consumed);

        // After merge: one block, content is "alpha" + "beta".
        QCOMPARE(static_cast<int>(doc.iterateBlocks().size()), 1);

        // The merged block should contain both texts concatenated.
        const QByteArray merged = doc.blockText(block0);
        QVERIFY(merged.contains("alpha"));
        QVERIFY(merged.contains("beta"));

        // block1 should be gone.
        Q_UNUSED(block1)

        QTRY_COMPARE(binding.model()->rowCount(), 1);
    }

    void backspace_at_start_of_first_block_is_not_consumed() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "alpha", 1));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("alpha"));
        QVERIFY(!consumed);
        // No change.
        QCOMPARE(static_cast<int>(doc.iterateBlocks().size()), 1);
    }

    // ---------- LiveStructuralKeyHandler — paragraph Shift-Enter (soft break) ----------

    void shift_enter_inserts_soft_break() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "hello world", 1));

        const Markoff::BlockId block = binding.model()->recordAt(0).blockAnchor;

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::ShiftModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("hello world"));
        QVERIFY(consumed);

        // Still one block — a newline was inserted within the block.
        QCOMPARE(static_cast<int>(doc.iterateBlocks().size()), 1);
        const QByteArray text = doc.blockText(block);
        QVERIFY(text.contains('\n'));  // newline was inserted at position 5
    }

    // ---------- LiveStructuralKeyHandler — paragraph Delete at row-end ----------

    void delete_at_end_of_paragraph_merges_with_next() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "alpha\n\nbeta", 2));

        const Markoff::BlockId block0 = binding.model()->recordAt(0).blockAnchor;

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Delete, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("alpha"));
        QVERIFY(consumed);

        // After merge: one block.
        QCOMPARE(static_cast<int>(doc.iterateBlocks().size()), 1);

        const QByteArray merged = doc.blockText(block0);
        QVERIFY(merged.contains("alpha"));
        QVERIFY(merged.contains("beta"));

        QTRY_COMPARE(binding.model()->rowCount(), 1);
    }

    void delete_at_end_of_last_block_is_not_consumed() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "alpha", 1));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Delete, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("alpha"));
        QVERIFY(!consumed);
        QCOMPARE(static_cast<int>(doc.iterateBlocks().size()), 1);
    }

    // ---------- Code-block structural keys ----------

    void code_block_enter_is_not_consumed_by_structural_handler() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "```\ncode\n```", 1));
        QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::CodeBlock);

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/4,    // mid-body
            /*selectionEmpty=*/true,
            QStringLiteral("code\n"));
        QVERIFY(!consumed);
    }

    void codeblock_tab_inserts_4_spaces() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "```\nhello\n```", 1));
        QTRY_COMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::CodeBlock);

        const Markoff::BlockId blockId = binding.model()->recordAt(0).blockAnchor;

        // Tab at position 0 inside the code block body.
        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Tab, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("hello\n"));
        QVERIFY(consumed);

        // Block should now start with 4 spaces.
        QTRY_VERIFY(doc.blockText(blockId).startsWith("    "));
    }

    void backspace_at_start_of_code_block_merges_with_previous() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "alpha\n\n```\ncode\n```", 2));
        QCOMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.model()->recordAt(1).kind, BlockKind::CodeBlock);

        const int blocksBefore = static_cast<int>(doc.iterateBlocks().size());

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/1, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("code\n"));
        QVERIFY(consumed);

        // One block was removed.
        QCOMPARE(static_cast<int>(doc.iterateBlocks().size()), blocksBefore - 1);
    }

    // ---------- HorizontalRule structural keys ----------

    void hr_delete_removes_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "hello\n\n---\n\nworld\n", 3));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 3);
        QCOMPARE(binding.model()->data(binding.model()->index(1, 0),
                 LiveBlockModel::KindRole).toString(), QStringLiteral("hr"));

        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Delete, Qt::NoModifier, 1, -1, true, QStringLiteral("---"));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_COMPARE(binding.model()->rowCount(), 2);
    }

    // ---------- ListItem structural keys (per-item blocks) ----------

    void list_item_loads_one_block_per_item() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "1. one\n2. two\n3. three\n", 3));
        QCOMPARE(binding.model()->rowCount(), 3);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(binding.model()->data(binding.model()->index(i, 0),
                     LiveBlockModel::KindRole).toString(),
                     QStringLiteral("list-item"));
    }

    void list_item_enter_at_end_creates_next_item_and_renumbers() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "1. one\n2. two\n", 2));

        // Enter at end of "two" (qtPos=3, content is "two")
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 1, 3, true, QStringLiteral("two"));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_COMPARE(binding.model()->rowCount(), 3);
        QCOMPARE(binding.model()->data(binding.model()->index(2, 0),
                 LiveBlockModel::MarkerNumberRole).toInt(), 3);
    }

    void list_item_enter_at_end_of_middle_item_renumbers_below() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "1. one\n2. two\n3. three\n", 3));

        // Enter at end of "one" (block 0, qtPos=3)
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 0, 3, true, QStringLiteral("one"));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_COMPARE(binding.model()->rowCount(), 4);
        // Now: "1. one", "2. (new empty)", "3. two", "4. three"
        QCOMPARE(binding.model()->data(binding.model()->index(0, 0),
                 LiveBlockModel::MarkerNumberRole).toInt(), 1);
        QCOMPARE(binding.model()->data(binding.model()->index(1, 0),
                 LiveBlockModel::MarkerNumberRole).toInt(), 2);
        QCOMPARE(binding.model()->data(binding.model()->index(2, 0),
                 LiveBlockModel::MarkerNumberRole).toInt(), 3);
        QCOMPARE(binding.model()->data(binding.model()->index(3, 0),
                 LiveBlockModel::MarkerNumberRole).toInt(), 4);
    }

    void list_item_enter_on_empty_exits_to_paragraph() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "1. one\n2. \n", 2));

        // Enter on empty item (block 1, content is "")
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 1, 0, true, QString());
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_COMPARE(binding.model()->data(binding.model()->index(1, 0),
                     LiveBlockModel::KindRole).toString(),
                     QStringLiteral("paragraph"));
    }

    void list_item_tab_indents_and_renumbers() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "1. one\n2. two\n", 2));

        // Tab on block 1 — indents it under block 0
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Tab, Qt::NoModifier, 1, 0, true, QStringLiteral("two"));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QCOMPARE(binding.model()->data(binding.model()->index(1, 0),
                 LiveBlockModel::IndentLevelRole).toInt(), 1);
    }

    // ---------- Blockquote structural keys ----------

    void blockquote_enter_creates_new_blockquote() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("> quote text\n");
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(binding.model()->data(binding.model()->index(0, 0),
                 LiveBlockModel::KindRole).toString(), QStringLiteral("blockquote"));

        const QString text = binding.model()->recordAt(0).text;
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 0,
            text.length(), true, text);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_COMPARE(binding.model()->rowCount(), 2);
        QTRY_COMPARE(binding.model()->data(binding.model()->index(1, 0),
                     LiveBlockModel::KindRole).toString(), QStringLiteral("blockquote"));
    }

    void blockquote_enter_on_empty_exits() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("> \n");
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 0, 2, true, QStringLiteral("> "));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_COMPARE(binding.model()->data(binding.model()->index(0, 0),
                     LiveBlockModel::KindRole).toString(), QStringLiteral("paragraph"));
    }

    // ---------- F2 / Escape — BlockInternalEdit transitions ----------

    void f2_on_math_block_enters_internal_edit() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        doc.loadFromMarkdown("$x^2$\n");
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 1);
        QTRY_COMPARE(binding.model()->data(binding.model()->index(0, 0),
                     LiveBlockModel::KindRole).toString(), QStringLiteral("math"));

        // Set BlockSelected cursor on the math block
        const Markoff::LiveRender::BlockSelected sel{
            binding.model()->recordAt(0).blockAnchor
        };
        binding.cursorState()->request(sel);
        QCOMPARE(binding.cursorState()->cursorKind(), QStringLiteral("BlockSelected"));

        // F2 should transition to BlockInternalEdit
        const bool handled = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_F2, Qt::NoModifier, 0, -1, true, QStringLiteral("$x^2$"));
        QVERIFY(handled);
        QCOMPARE(binding.cursorState()->cursorKind(), QStringLiteral("BlockInternalEdit"));

        // Escape should return to BlockSelected
        const bool handled2 = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Escape, Qt::NoModifier, 0, -1, true, QStringLiteral("$x^2$"));
        QVERIFY(handled2);
        QCOMPARE(binding.cursorState()->cursorKind(), QStringLiteral("BlockSelected"));
    }

    // ---------- Heading level-change via Ctrl+Shift+1-6/0 ----------

    void heading_level_change_via_ctrl_shift() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "## Hello\n", 1));
        QTRY_COMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(binding.model()->data(binding.model()->index(0, 0),
                 LiveBlockModel::HeadingLevelRole).toInt(), 2);

        // Ctrl+Shift+1 → H1
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_1, Qt::ControlModifier | Qt::ShiftModifier,
            0, 0, true, QStringLiteral("## Hello"));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_COMPARE(binding.model()->data(binding.model()->index(0, 0),
                     LiveBlockModel::HeadingLevelRole).toInt(), 1);

        // Ctrl+Shift+0 → demote to paragraph
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_0, Qt::ControlModifier | Qt::ShiftModifier,
            0, 0, true, QStringLiteral("# Hello"));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_VERIFY(binding.model()->data(binding.model()->index(0, 0),
                    LiveBlockModel::KindRole).toString() != QStringLiteral("heading"));
    }
};

QTEST_MAIN(TstLiveRenderStructural)
#include "tst_live_render_structural.moc"
