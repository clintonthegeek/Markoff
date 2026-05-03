// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/live-render/UndoCoalescer.h>
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff/live-render/LiveProxyBlockModel.h>
#include <markoff/live-render/Cursor.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/TextAnchor.h>

using namespace Markoff::LiveRender;

// Helper: synthesise a non-default BlockAnchor for tests by carving one
// out of a real MarkoffDocument's first block. The CRDT internals are
// opaque; we only need anchors that compare equal to themselves and
// differently to others.
static Markoff::BlockAnchor anchorAtFirstBlock(Markoff::MarkoffDocument &doc)
{
    auto opt = doc.blockAnchorAt(0);
    return opt.value_or(Markoff::BlockAnchor{});
}

class TstLiveRenderStructural : public QObject {
    Q_OBJECT

private Q_SLOTS:

    // ---------- UndoCoalescer ----------

    void coalescer_first_printable_does_not_coalesce() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        UndoCoalescer coalescer(&doc);

        Markoff::BlockAnchor a = Markoff::BlockAnchor{};
        bool didCoalesce = coalescer.recordPrintable(a);
        QVERIFY(!didCoalesce);
    }

    void coalescer_consecutive_printables_same_anchor_coalesce() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        // Drive two real applyLocalEdits so the buffer has two undo entries.
        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QCOMPARE(doc.undoDepth(), 1);
        QVERIFY(!coalescer.recordPrintable(a));

        Markoff::MarkoffEdit e2; e2.oldStart = 6; e2.oldEnd = 6; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QCOMPARE(doc.undoDepth(), 2);
        QVERIFY(coalescer.recordPrintable(a));
        // After coalesce, depth back to 1.
        QCOMPARE(doc.undoDepth(), 1);
    }

    void coalescer_different_anchor_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("alpha\n\nbeta", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a0 = doc.blockAnchorAt(0).value();
        Markoff::BlockAnchor a1 = doc.blockAnchorAt(1).value();

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a0));

        Markoff::MarkoffEdit e2; e2.oldStart = 12; e2.oldEnd = 12; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a1));  // different block — no coalesce
        QCOMPARE(doc.undoDepth(), 2);
    }

    void coalescer_structural_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a));

        Markoff::MarkoffEdit es; es.oldStart = 6; es.oldEnd = 6; es.newText = "\n\n";
        doc.applyLocalEdit({ es });
        coalescer.recordStructural();
        // Structural does NOT coalesce its own undo; depth stays 2.
        QCOMPARE(doc.undoDepth(), 2);

        Markoff::MarkoffEdit e2; e2.oldStart = 8; e2.oldEnd = 8; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a));  // structural broke the chain
        QCOMPARE(doc.undoDepth(), 3);
    }

    void coalescer_focus_change_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a));

        coalescer.notifyFocusChanged();

        Markoff::MarkoffEdit e2; e2.oldStart = 6; e2.oldEnd = 6; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a));  // focus-change broke it
        QCOMPARE(doc.undoDepth(), 2);
    }

    void coalescer_idle_expiry_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a));

        // Manually expire the idle window.
        coalescer.notifyIdleExpired();

        Markoff::MarkoffEdit e2; e2.oldStart = 6; e2.oldEnd = 6; e2.newText = "B";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a));
        QCOMPARE(doc.undoDepth(), 2);
    }

    void coalescer_other_breaks_chain() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));

        UndoCoalescer coalescer(&doc);
        Markoff::BlockAnchor a = anchorAtFirstBlock(doc);

        Markoff::MarkoffEdit e1; e1.oldStart = 5; e1.oldEnd = 5; e1.newText = "A";
        doc.applyLocalEdit({ e1 });
        QVERIFY(!coalescer.recordPrintable(a));

        // Simulate a paste / multi-char delete.
        Markoff::MarkoffEdit ep; ep.oldStart = 6; ep.oldEnd = 6; ep.newText = "PASTED";
        doc.applyLocalEdit({ ep });
        coalescer.recordOther();

        Markoff::MarkoffEdit e2; e2.oldStart = 12; e2.oldEnd = 12; e2.newText = "Z";
        doc.applyLocalEdit({ e2 });
        QVERIFY(!coalescer.recordPrintable(a));  // recordOther broke the chain
        QCOMPARE(doc.undoDepth(), 3);
    }

    // ---------- LiveStructuralKeyHandler — paragraph Enter (all positions) ----------

    void enter_at_end_of_paragraph_creates_hole() {
        // R5.5 Task 11: EOB-Enter now creates a hole row instead of mutating source.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);

        auto *handler = binding.structuralKeyHandler();
        QVERIFY(handler);

        const bool consumed = handler->tryHandle(
            /*key=*/Qt::Key_Return,
            /*modifiers=*/Qt::NoModifier,
            /*blockIndex=*/0,
            /*qtPos=*/5,                  // end of "hello"
            /*selectionEmpty=*/true,
            /*blockText=*/QStringLiteral("hello"));
        QVERIFY(consumed);

        // F5: source is NOT mutated at hole creation time.
        QCOMPARE(doc.toMarkdown(), QString("hello"));

        // A hole was created in the layer.
        QCOMPARE(binding.holeLayer()->holeCount(), 1);

        // Proxy has 2 rows: 1 parser block + 1 hole.
        QCOMPARE(binding.proxyModel()->rowCount(), 2);
        // Hole is AFTER the block (EOB-Enter).
        QVERIFY(binding.proxyModel()->proxyRowIsHole(1));
    }
    void enter_in_middle_of_paragraph_splits_block() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello world", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("hello world"));
        QVERIFY(consumed);

        QCOMPARE(doc.toMarkdown(), QString("hello\n\n world"));

        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);
        // Parser includes the paragraph-separator \n in the first block's
        // byte range; second block starts after the blank line.
        QCOMPARE(binding.model()->recordAt(0).text, QString("hello\n"));
        QCOMPARE(binding.model()->recordAt(1).text, QString(" world"));

        const Cursor cur = binding.cursorState()->cursor();
        QVERIFY(std::holds_alternative<TextCaret>(cur));
        QCOMPARE(anchorOf(std::get<TextCaret>(cur).block),
                 binding.model()->recordAt(1).blockAnchor);
    }

    void enter_at_start_of_paragraph_creates_hole_above() {
        // R5.5 Task 11: start-of-block-Enter now creates a hole row instead of
        // mutating source.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("hello"));
        QVERIFY(consumed);

        // F5: source is NOT mutated at hole creation time.
        QCOMPARE(doc.toMarkdown(), QString("hello"));

        // A hole was created in the layer.
        QCOMPARE(binding.holeLayer()->holeCount(), 1);

        // Proxy has 2 rows: 1 hole (above) + 1 parser block.
        QCOMPARE(binding.proxyModel()->rowCount(), 2);
        QVERIFY(binding.proxyModel()->proxyRowIsHole(0));
        QVERIFY(!binding.proxyModel()->proxyRowIsHole(1));
    }

    // ---------- LiveStructuralKeyHandler — paragraph Backspace at row-start ----------

    void backspace_at_start_of_paragraph_merges_with_previous() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha\n\nbeta", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/1, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("beta"));
        QVERIFY(consumed);

        // One byte deleted from the inter-block separator: "alpha\nbeta"
        // (or possibly "alphabeta" depending on which separator byte got
        // deleted; the legacy handler deletes ONE byte, leaving "alpha\nbeta",
        // which the parser treats as a single paragraph).
        QCOMPARE(doc.toMarkdown(), QString("alpha\nbeta"));

        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);
    }

    void backspace_at_start_of_first_block_is_not_consumed() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("alpha"));
        QVERIFY(!consumed);
        QCOMPARE(doc.toMarkdown(), QString("alpha"));
    }

    // ---------- LiveStructuralKeyHandler — paragraph Shift-Enter (soft break) ----------

    void shift_enter_inserts_soft_break() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello world", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::ShiftModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("hello world"));
        QVERIFY(consumed);
        QCOMPARE(doc.toMarkdown(), QString("hello\n world"));

        QVERIFY(parseSpy.wait(2000));
        // Parser keeps it as one paragraph (CommonMark soft break).
        QCOMPARE(binding.model()->rowCount(), 1);
    }

    // ---------- LiveStructuralKeyHandler — paragraph Delete at row-end ----------

    void delete_at_end_of_paragraph_merges_with_next() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha\n\nbeta", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Delete, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("alpha"));
        QVERIFY(consumed);
        QCOMPARE(doc.toMarkdown(), QString("alpha\nbeta"));

        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 1);
    }

    void delete_at_end_of_last_block_is_not_consumed() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Delete, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/5,
            /*selectionEmpty=*/true,
            QStringLiteral("alpha"));
        QVERIFY(!consumed);
        QCOMPARE(doc.toMarkdown(), QString("alpha"));
    }

    // ---------- LiveStructuralKeyHandler — heading structural keys ----------

    void enter_at_end_of_heading_inserts_paragraph_break() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        // Use trailing \n so tree-sitter recognises the ATX heading.
        doc.resetContent("# Title\n", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);

        // blockText passed from delegate is the source-faithful heading text
        // (includes the # prefix). qtPos 7 = end of "# Title" (7 chars).
        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/7,
            /*selectionEmpty=*/true,
            QStringLiteral("# Title"));
        QVERIFY(consumed);

        // After inserting \n\n at end of heading text (before the trailing \n
        // of the source), the document becomes "# Title\n\n\n".
        QVERIFY(doc.toMarkdown().startsWith(QStringLiteral("# Title")));
    }

    void backspace_at_start_of_heading_merges_with_previous() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha\n\n# Heading", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/1, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("# Heading"));
        QVERIFY(consumed);
        QCOMPARE(doc.toMarkdown(), QString("alpha\n# Heading"));
    }

    // ---------- LiveStructuralKeyHandler — code-block structural keys ----------

    void backspace_at_start_of_code_block_merges_with_previous() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha\n\n```\ncode\n```", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.model()->recordAt(1).kind, BlockKind::CodeBlock);

        // Code-block delegate exposes the body bytes as blockText (sans fences).
        // qtPos 0 = start of body. R5 backspace-at-start of code-block treats
        // that as "merge with previous"; the byte deleted is the inter-block
        // separator before currentBlockStart, where currentBlockStart is the
        // CRDT-resolved start of the WHOLE code-block source range (including
        // the opening fence). One \n is removed.
        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Backspace, Qt::NoModifier,
            /*blockIndex=*/1, /*qtPos=*/0,
            /*selectionEmpty=*/true,
            QStringLiteral("code\n"));
        QVERIFY(consumed);
        // Exact post-state depends on which separator byte got deleted; the
        // important invariants are: it's shorter, and the structural handler
        // returned true.
        QVERIFY(doc.toMarkdown().size() < QStringLiteral("alpha\n\n```\ncode\n```").size());
    }

    void code_block_enter_is_not_consumed_by_structural_handler() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("```\ncode\n```", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::CodeBlock);

        const bool consumed = binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            /*blockIndex=*/0, /*qtPos=*/4,    // mid-body
            /*selectionEmpty=*/true,
            QStringLiteral("code\n"));
        QVERIFY(!consumed);
    }

    // ---------- R5.5 Task 11 — EOB-Enter + start-Enter create holes ----------

    void paragraph_eob_enter_creates_hole_not_source_edit() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        LiveHoleLayer       *layer   = binding.holeLayer();
        LiveProxyBlockModel *proxy   = binding.proxyModel();
        LiveStructuralKeyHandler *handler = binding.structuralKeyHandler();
        QVERIFY(layer && proxy && handler);

        // Simulate caret at qtPos 5 of row 0 ("hello"), end-of-block.
        const bool ok = handler->tryHandle(Qt::Key_Return, Qt::NoModifier,
                                           /*blockIndex=*/0,
                                           /*qtPos=*/5,
                                           /*selectionEmpty=*/true,
                                           QStringLiteral("hello"));
        QVERIFY(ok);
        QCOMPARE(layer->holeCount(), 1);
        QCOMPARE(doc.toMarkdown(), QString("hello"));  // F5 — source unchanged
        QCOMPARE(proxy->rowCount(), 2);                // 1 parser + 1 hole
        QVERIFY(proxy->proxyRowIsHole(1));
    }

    void paragraph_start_enter_creates_hole_above() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *proxy   = binding.proxyModel();
        auto *handler = binding.structuralKeyHandler();

        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier,
                                   0, /*qtPos=*/0, true,
                                   QStringLiteral("hello")));
        QCOMPARE(layer->holeCount(), 1);
        QCOMPARE(doc.toMarkdown(), QString("hello"));
        QCOMPARE(proxy->rowCount(), 2);
        QVERIFY(proxy->proxyRowIsHole(0));
        QVERIFY(!proxy->proxyRowIsHole(1));
    }

    void paragraph_eob_enter_routes_cursor_into_hole() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *handler = binding.structuralKeyHandler();
        auto *cursor  = binding.cursorState();
        auto *layer   = binding.holeLayer();

        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier,
                                   0, 5, true, QStringLiteral("hello")));

        QCOMPARE(layer->holeCount(), 1);
        const Cursor c = cursor->cursor();
        const auto *tc = std::get_if<TextCaret>(&c);
        QVERIFY(tc);
        QVERIFY(isHoleBlockId(tc->block));
        QCOMPARE(tc->cachedByteOffset, quint32(0));

        // The hole's holeId — the only one in the layer.
        QList<quint64> ids = layer->holesInOrder();
        QCOMPARE(ids.size(), 1);
        QCOMPARE(holeIdOf(tc->block), ids.first());
    }

    // ---------- R5.5 Task 12 — hole-row Enter dispatch ----------

    void hole_enter_at_end_of_buffer_commits_then_creates_new_hole() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *proxy   = binding.proxyModel();
        auto *handler = binding.structuralKeyHandler();

        // First Enter at EOB of "hello" (proxy row 0) — creates hole at proxy row 1.
        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "hello"));
        QCOMPARE(layer->holeCount(), 1);
        QCOMPARE(proxy->rowCount(), 2);
        QVERIFY(proxy->proxyRowIsHole(1));

        const quint64 firstHoleId = layer->holesInOrder().first();

        // Simulate typing "first" into the hole buffer.
        layer->setBlockHoleBuffer(firstHoleId, "first");

        // Enter at end of buffer (qtPos == 5 == "first".length()) on proxy row 1.
        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 1, 5, true, "first"));

        // First hole committed; source now has "hello\n\nfirst".
        QTRY_COMPARE(doc.toMarkdown(), QString("hello\n\nfirst"));
        // Only the new hole remains.
        QCOMPARE(layer->holeCount(), 1);
        // Parser produces 2 blocks ("hello", "first") + 1 new hole = 3 proxy rows.
        QTRY_COMPARE(proxy->rowCount(), 3);
        QVERIFY(proxy->proxyRowIsHole(2));

        // Cursor is in the new hole at qtPos 0.
        const auto cur = binding.cursorState()->cursor();
        const auto *tc = std::get_if<TextCaret>(&cur);
        QVERIFY(tc);
        QVERIFY(isHoleBlockId(tc->block));
        QCOMPARE(tc->cachedByteOffset, quint32(0));
        QCOMPARE(holeIdOf(tc->block), layer->holesInOrder().first());

        // Type "second" into the new hole and trigger idle commit.
        const quint64 newHoleId = layer->holesInOrder().first();
        layer->setBlockHoleBuffer(newHoleId, "second");
        QSignalSpy idleSpy(layer, SIGNAL(idleCommitDue(quint64)));
        QVERIFY(idleSpy.wait(400));
        layer->commitBlockHole(idleSpy.first().at(0).toULongLong());
        QTRY_COMPARE(doc.toMarkdown(), QString("hello\n\nfirst\n\nsecond"));
    }

    void hole_enter_mid_buffer_splits_into_committed_prefix_and_new_hole_suffix() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *handler = binding.structuralKeyHandler();

        // Create a hole after "hello" (proxy row 1).
        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "hello"));
        QCOMPARE(layer->holeCount(), 1);

        const quint64 holeId = layer->holesInOrder().first();
        layer->setBlockHoleBuffer(holeId, "helloworld");  // 10 chars

        // Mid-buffer Enter at qtPos 5 (between "hello" and "world") on proxy row 1.
        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 1, 5, true, "helloworld"));

        // Prefix "hello" committed; source: "hello\n\nhello".
        QTRY_COMPARE(doc.toMarkdown(), QString("hello\n\nhello"));
        // New hole has suffix "world".
        QCOMPARE(layer->holeCount(), 1);
        const quint64 newId = layer->holesInOrder().first();
        QCOMPARE(layer->bufferText(newId), QString("world"));

        // Cursor at qtPos 0 of the new hole.
        const auto cur = binding.cursorState()->cursor();
        const auto *tc = std::get_if<TextCaret>(&cur);
        QVERIFY(tc);
        QVERIFY(isHoleBlockId(tc->block));
        QCOMPARE(holeIdOf(tc->block), newId);
        QCOMPARE(tc->cachedByteOffset, quint32(0));
    }

    void hole_enter_on_empty_buffer_is_noop() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *handler = binding.structuralKeyHandler();

        // Create a hole after "hello" with empty buffer (proxy row 1).
        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "hello"));
        QCOMPARE(layer->holeCount(), 1);
        const quint64 holeId = layer->holesInOrder().first();
        // bufferText is empty by default.

        // Enter on empty hole — consumed but no source change, no new hole.
        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 1, 0, true, ""));
        QCOMPARE(layer->holeCount(), 1);                      // same hole, unchanged
        QCOMPARE(layer->holesInOrder().first(), holeId);
        QCOMPARE(doc.toMarkdown(), QString("hello"));         // source untouched
    }

    // ---------- R5.5 Task 13 — hole-row abandon paths ----------

    void hole_esc_abandons_and_focuses_previous_neighbor() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *handler = binding.structuralKeyHandler();

        // Create a hole at EOB; type into it.
        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "hello"));
        const quint64 holeId = layer->holesInOrder().first();
        layer->setBlockHoleBuffer(holeId, "x");

        // Esc — abandon, focus previous (row 0, "hello", end-of-row qtPos 5).
        QVERIFY(handler->tryHandle(Qt::Key_Escape, Qt::NoModifier, 1, 1, true, "x"));
        QCOMPARE(layer->holeCount(), 0);
        QCOMPARE(doc.toMarkdown(), QString("hello"));

        const auto cur = binding.cursorState()->cursor();
        const auto *tc = std::get_if<TextCaret>(&cur);
        QVERIFY(tc);
        QVERIFY(!isHoleBlockId(tc->block));   // anchor-side
        QCOMPARE(tc->cachedByteOffset, quint32(5));   // end of "hello"
    }

    void hole_backspace_at_qt_pos_0_with_empty_buffer_abandons() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *handler = binding.structuralKeyHandler();

        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "hello"));
        QCOMPARE(layer->holeCount(), 1);
        // bufferText is empty (default); qtPos is 0.

        QVERIFY(handler->tryHandle(Qt::Key_Backspace, Qt::NoModifier, 1, 0, true, ""));
        QCOMPARE(layer->holeCount(), 0);
        QCOMPARE(doc.toMarkdown(), QString("hello"));

        const auto cur = binding.cursorState()->cursor();
        const auto *tc = std::get_if<TextCaret>(&cur);
        QVERIFY(tc);
        QVERIFY(!isHoleBlockId(tc->block));
        QCOMPARE(tc->cachedByteOffset, quint32(5));
    }

    void hole_delete_at_end_with_empty_buffer_abandons() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("first\n\nsecond", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 2);

        auto *layer   = binding.holeLayer();
        auto *handler = binding.structuralKeyHandler();

        // Create a hole at EOB of "first" (between first and second).
        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "first"));
        QCOMPARE(layer->holeCount(), 1);

        // Delete on empty hole — focus moves to next neighbor ("second", qtPos 0).
        QVERIFY(handler->tryHandle(Qt::Key_Delete, Qt::NoModifier, 1, 0, true, ""));
        QCOMPARE(layer->holeCount(), 0);
        QCOMPARE(doc.toMarkdown(), QString("first\n\nsecond"));

        const auto cur = binding.cursorState()->cursor();
        const auto *tc = std::get_if<TextCaret>(&cur);
        QVERIFY(tc);
        QVERIFY(!isHoleBlockId(tc->block));
        QCOMPARE(tc->cachedByteOffset, quint32(0));
    }

    void hole_backspace_at_qt_pos_0_with_non_empty_buffer_is_passthrough() {
        Markoff::MarkoffDocument doc(1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("hello", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *layer   = binding.holeLayer();
        auto *handler = binding.structuralKeyHandler();

        QVERIFY(handler->tryHandle(Qt::Key_Return, Qt::NoModifier, 0, 5, true, "hello"));
        const quint64 holeId = layer->holesInOrder().first();
        layer->setBlockHoleBuffer(holeId, "abc");

        // Backspace at qtPos 0 with non-empty buffer — passthrough (NotHandled),
        // hole stays.
        QVERIFY(!handler->tryHandle(Qt::Key_Backspace, Qt::NoModifier, 1, 0, true, "abc"));
        QCOMPARE(layer->holeCount(), 1);
        QCOMPARE(layer->bufferText(holeId), QString("abc"));
    }

};

QTEST_MAIN(TstLiveRenderStructural)
#include "tst_live_render_structural.moc"
