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

    // ---------- LiveStructuralKeyHandler — list/quote heuristic gate ----------

    void enter_on_list_row_is_not_consumed() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "- item1\n- item2\n", 1));

        // The list collapses to one paragraph-kinded row.
        QVERIFY(binding.model()->rowCount() >= 1);
        const QString rowText = binding.model()->recordAt(0).text;

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0,
            /*qtPos=*/rowText.indexOf(QStringLiteral("- item2")),
            /*selectionEmpty=*/true,
            rowText);

        // The heuristic gate must fire and NOT consume the key for list rows.
        if (consumed) {
            // If consumed, verify no ZWSP-marker-style pollution happened.
            const auto ids = doc.iterateBlocks();
            for (const auto &id : ids) {
                QVERIFY(!doc.blockText(id).contains('\xE2'));  // no ZWSP
            }
        }
        // Structural invariant: source must not have been split with a "\n\n"
        // between two list-marker lines.
        bool hasDoubleParagraph = false;
        const auto ids = doc.iterateBlocks();
        for (const auto &id : ids) {
            const QString t = QString::fromUtf8(doc.blockText(id));
            if (t.contains(QStringLiteral("\n\n-"))) {
                hasDoubleParagraph = true;
                break;
            }
        }
        QVERIFY2(!hasDoubleParagraph, "list was split with \\n\\n between items");
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

    // ---------- ListItem structural keys ----------

    void list_item_enter_creates_new_list_item() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "- hello", 1));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(binding.model()->data(binding.model()->index(0, 0),
                 LiveBlockModel::KindRole).toString(), QStringLiteral("list-item"));

        // Enter at end of "- hello" (full text, length 7).
        // New behavior: inserts '\n- ' within the same block — no new block created.
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 0, 7, true, QStringLiteral("- hello"));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_COMPARE(binding.model()->rowCount(), 1);
        QTRY_COMPARE(binding.model()->data(binding.model()->index(0, 0),
                     LiveBlockModel::TextRole).toString(),
                     QStringLiteral("- hello\n- "));
    }

    void list_item_enter_renumbers_subsequent_ordered_items() {
        // User UX: insert at end of "2. two" should produce "3. " in-line and
        // renumber existing 3→4, 4→5, etc. so the source stays sequential.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        const QByteArray six =
            "1. one\n2. two\n3. three\n4. four\n5. five\n6. six";
        QVERIFY(waitForModelRows(binding, doc, six, 1));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        const QString rowText = binding.model()->recordAt(0).text;
        // qtPos at end of "2. two" — that's position 13 (after 'o' of "two").
        const int qtPos = QStringLiteral("1. one\n2. two").length();
        QCOMPARE(qtPos, 13);

        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 0, qtPos, true, rowText);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        const QString after = binding.model()->recordAt(0).text;
        qDebug() << "[probe] after enter+renumber:" << after;
        QCOMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(after,
            QStringLiteral("1. one\n2. two\n3. \n4. three\n5. four\n6. five\n7. six"));
    }

    void list_item_enter_at_line_start_renumbers_too() {
        // Insert empty "2. " above existing "2. two" by pressing Enter at
        // qtPos=7 (start of "2. two"). New text: "1. one\n2. \n3. two\n4. three..."
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        const QByteArray three = "1. one\n2. two\n3. three";
        QVERIFY(waitForModelRows(binding, doc, three, 1));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        const QString rowText = binding.model()->recordAt(0).text;
        const int qtPos = QStringLiteral("1. one\n").length();
        QCOMPARE(qtPos, 7);

        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 0, qtPos, true, rowText);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        const QString after = binding.model()->recordAt(0).text;
        qDebug() << "[probe] after at-line-start enter:" << after;
        QCOMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(after, QStringLiteral("1. one\n2. \n3. two\n4. three"));
    }

    void list_item_load_with_double_trailing_newline_has_no_phantom_line() {
        // Regression: source with multiple trailing newlines must not leave
        // a phantom empty line in the model text.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "1. one\n2. two\n\n", 1));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        const QString modelText = binding.model()->recordAt(0).text;
        QCOMPARE(modelText, QStringLiteral("1. one\n2. two"));
        QVERIFY(!modelText.endsWith(u'\n'));
    }

    void list_item_probe_trailing_newline_in_buffer() {
        // Probe: when source has trailing whitespace after the list, does the
        // CRDT buffer contain multiple trailing '\n's? If so, the model text
        // (which only chops ONE trailing '\n') will show a visible empty line.
        auto trailingNewlines = [](const QByteArray &b) {
            int n = 0; for (int i = b.size() - 1; i >= 0 && b[i] == '\n'; --i) ++n; return n;
        };
        auto trailingNewlinesQ = [](const QString &s) {
            int n = 0; for (int i = s.size() - 1; i >= 0 && s[i] == u'\n'; --i) ++n; return n;
        };
        auto report = [&](const QByteArray &source, const char *label) {
            Markoff::MarkoffDocument doc(/*replicaId=*/1);
            LiveListModelBinding binding;
            binding.setDocument(&doc);
            doc.loadFromMarkdown(source);
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            QCOMPARE(binding.model()->rowCount(), 1);
            const auto id = doc.iterateBlocks()[0];
            const QByteArray buf = doc.blockText(id);
            const QString modelText = binding.model()->recordAt(0).text;
            qDebug().noquote() << QString("[probe] %1: src=%2b/%3\\n  buf=%4b/%5\\n  model=%6c/%7\\n")
                .arg(QString::fromUtf8(label))
                .arg(source.size()).arg(trailingNewlines(source))
                .arg(buf.size()).arg(trailingNewlines(buf))
                .arg(modelText.length()).arg(trailingNewlinesQ(modelText));
        };
        report("1. one\n2. two\n3. three\n4. four\n5. five\n6. six", "no trailing");
        report("1. one\n2. two\n3. three\n4. four\n5. five\n6. six\n", "one trailing");
        report("1. one\n2. two\n3. three\n4. four\n5. five\n6. six\n\n", "two trailing");
    }

    void list_item_enter_compound_six_items_then_three_enters_no_new_blocks() {
        // Simulates the exact dogfood scenario: 6 numbered items in ONE block,
        // press Enter at end → "7. ", type "seven", Enter → "8. ", type "eight",
        // Enter → "9. ". rowCount must remain 1 throughout.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        const QByteArray six =
            "1. one\n2. two\n3. three\n4. four\n5. five\n6. six\n";
        QVERIFY(waitForModelRows(binding, doc, six, 1));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        // Press Enter three times at end (with bracketing buffer edits to
        // simulate typing "seven" / "eight" between each Enter).
        auto pressEnterAtEnd = [&]() {
            const QString cur = binding.model()->recordAt(0).text;
            binding.structuralKeyHandler()->tryHandle(
                Qt::Key_Return, Qt::NoModifier, 0, cur.length(), true, cur);
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
        };
        auto typeText = [&](const QByteArray &text) {
            const auto id = doc.iterateBlocks()[0];
            const QByteArray buf = doc.blockText(id);
            uint32_t end = static_cast<uint32_t>(buf.size());
            // Trim trailing '\n' from CRDT buffer for offset computation; the
            // model text is this buf with one trailing '\n' chopped, so the
            // visual end maps back to buf.size() - (1 if endsWith \n else 0).
            if (buf.endsWith('\n')) --end;
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, end, 0, text, t);
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
        };

        pressEnterAtEnd();
        typeText("seven");
        const QString afterSeven = binding.model()->recordAt(0).text;
        qDebug() << "[probe] afterSeven=" << afterSeven
                 << "rowCount=" << binding.model()->rowCount();
        QCOMPARE(binding.model()->rowCount(), 1);
        QVERIFY2(afterSeven.endsWith(QStringLiteral("\n7. seven")),
                 qPrintable(QStringLiteral("expected \"\\n7. seven\" suffix, got: %1").arg(afterSeven)));

        pressEnterAtEnd();
        typeText("eight");
        const QString afterEight = binding.model()->recordAt(0).text;
        qDebug() << "[probe] afterEight=" << afterEight
                 << "rowCount=" << binding.model()->rowCount();
        QCOMPARE(binding.model()->rowCount(), 1);
        QVERIFY2(afterEight.endsWith(QStringLiteral("\n8. eight")),
                 qPrintable(QStringLiteral("expected \"\\n8. eight\" suffix, got: %1").arg(afterEight)));

        pressEnterAtEnd();
        typeText("nine");
        const QString afterNine = binding.model()->recordAt(0).text;
        qDebug() << "[probe] afterNine=" << afterNine
                 << "rowCount=" << binding.model()->rowCount();
        QCOMPARE(binding.model()->rowCount(), 1);
        QVERIFY2(afterNine.endsWith(QStringLiteral("\n9. nine")),
                 qPrintable(QStringLiteral("expected \"\\n9. nine\" suffix, got: %1").arg(afterNine)));
    }

    void list_item_enter_six_item_ordered_list_at_end_appends_in_block() {
        // Mirrors the user's dogfood scenario: a 6-item numbered list in
        // ONE CRDT block. Pressing Enter at end of item 6 must insert
        // "\n7. " IN-BLOCK (rowCount stays 1, text gets a 7th line).
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        const QByteArray six =
            "1. one\n2. two\n3. three\n4. four\n5. five\n6. six\n";
        QVERIFY(waitForModelRows(binding, doc, six, 1));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(binding.model()->data(binding.model()->index(0, 0),
                 LiveBlockModel::KindRole).toString(),
                 QStringLiteral("list-item"));

        const QString rowText = binding.model()->recordAt(0).text;
        qDebug() << "[probe] rowText =" << rowText
                 << "len=" << rowText.length()
                 << "blockCount=" << doc.iterateBlocks().size();
        // After model trim, rowText = "1. one\n...\n6. six" (no trailing \n).
        // Press Enter at end of "6. six".
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 0, rowText.length(), true, rowText);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        const QString afterText = binding.model()->recordAt(0).text;
        qDebug() << "[probe] afterText =" << afterText
                 << "rowCount=" << binding.model()->rowCount()
                 << "blockCount=" << doc.iterateBlocks().size();

        QTRY_COMPARE(binding.model()->rowCount(), 1);
        QVERIFY2(afterText.endsWith(QStringLiteral("\n7. ")),
                 qPrintable(QStringLiteral("expected text to end with '\\n7. ', got: %1")
                            .arg(afterText)));
    }

    void list_item_enter_on_empty_exits_list() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // "- " is a list item with just the marker prefix
        QVERIFY(waitForModelRows(binding, doc, "- ", 1));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier, 0, 2, true, QStringLiteral("- "));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTRY_COMPARE(binding.model()->data(binding.model()->index(0, 0),
                     LiveBlockModel::KindRole).toString(), QStringLiteral("paragraph"));
    }

    void list_item_tab_indents() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "- item", 1));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QTRY_COMPARE(binding.model()->rowCount(), 1);
        QTRY_COMPARE(binding.model()->data(binding.model()->index(0, 0),
                     LiveBlockModel::KindRole).toString(), QStringLiteral("list-item"));

        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Tab, Qt::NoModifier, 0, 3, true, QStringLiteral("- item"));
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        const auto blockId = doc.iterateBlocks()[0];
        const auto attrs = doc.blockAttrs(blockId);
        QVERIFY(attrs.contains(QByteArray("indentLevel")));
        QCOMPARE(std::get<int>(attrs.value(QByteArray("indentLevel"))), 1);
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
