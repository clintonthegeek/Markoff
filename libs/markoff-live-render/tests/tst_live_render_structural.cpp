// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/live-render/UndoCoalescer.h>
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/Marker.h>
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
        // BlockWalker trims the trailing \n that tree-sitter includes in
        // each block's byte range — without that trim, qtPos at end-of-
        // paragraph lands inside the "\n\n" separator and end-of-block
        // typing eats the following paragraph (see BlockWalker comment).
        QCOMPARE(binding.model()->recordAt(0).text, QString("hello"));
        QCOMPARE(binding.model()->recordAt(1).text, QString(" world"));

        const Cursor cur = binding.cursorState()->cursor();
        QVERIFY(std::holds_alternative<TextCaret>(cur));
        QCOMPARE(std::get<TextCaret>(cur).block,
                 binding.model()->recordAt(1).blockAnchor);
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

    // ---------- R5.5 Task 6 — EOB-Enter + start-Enter insert marker paragraph ----------

    void paragraphEnter_atEob_insertsMarkerEdit() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *handler = binding.structuralKeyHandler();
        QVERIFY(handler);

        const bool handled = handler->tryHandle(
            Qt::Key_Return, /*mods=*/Qt::NoModifier,
            /*blockIndex=*/0,
            /*qtPos=*/5,
            /*selectionEmpty=*/true,
            /*blockText=*/QStringLiteral("alpha"));
        QVERIFY(handled);
        QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()),
                 QStringLiteral("alpha\n\n%1").arg(kMarkerChar));

        QTRY_COMPARE(binding.model()->rowCount(), 2);
        QCOMPARE(binding.cursorState()->focusedAnchorRow(), 1);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 0);
    }

    void paragraphEnter_atStartOfBlock_insertsMarkerEdit() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent("alpha", Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 1);

        auto *handler = binding.structuralKeyHandler();
        QVERIFY(handler);

        const bool handled = handler->tryHandle(
            Qt::Key_Return, /*mods=*/Qt::NoModifier,
            /*blockIndex=*/0,
            /*qtPos=*/0,
            /*selectionEmpty=*/true,
            /*blockText=*/QStringLiteral("alpha"));
        QVERIFY(handled);
        QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()),
                 QStringLiteral("%1\n\nalpha").arg(kMarkerChar));

        QTRY_COMPARE(binding.model()->rowCount(), 2);
        // Cursor follows the user's content (now at row 1), not the marker.
        // Wait for the pending cursor request to resolve.
        QTRY_COMPARE(binding.cursorState()->focusedAnchorRow(), 1);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 0);
    }

    void paragraphEnter_atStartOfBlock_cursorFollowsContent() {
        using namespace Markoff;
        using namespace Markoff::LiveRender;
        // Dogfood correction (Task 18 Bug 2): pressing Enter at qtPos 0 of a
        // paragraph must leave the cursor at qtPos 0 of the user's content
        // (which is now shifted down to row blockIndex+1), NOT in the marker
        // paragraph (at row blockIndex). The marker stays as a visual blank
        // above; the user stays focused on their content to keep typing.
        MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *model   = binding.model();
        auto *cs      = binding.cursorState();
        auto *handler = binding.structuralKeyHandler();

        doc.resetContent(QByteArrayLiteral("alpha\n"), Origin::TestFixture);
        QTRY_COMPARE(model->rowCount(), 1);

        bool handled = handler->tryHandle(Qt::Key_Return, Qt::NoModifier,
                                          /*blockIndex=*/0,
                                          /*qtPos=*/0,
                                          /*selectionEmpty=*/true,
                                          /*blockText=*/QStringLiteral("alpha"));
        QVERIFY(handled);
        QTRY_COMPARE(model->rowCount(), 2);
        // Marker paragraph is at row 0 (visual blank above); user's content is at row 1.
        // Cursor MUST land in row 1 (the user's content) so they can keep typing.
        // Wait for the pending cursor request to resolve.
        QTRY_COMPARE(cs->focusedAnchorRow(), 1);
        QCOMPARE(cs->focusedQtPos(), 0);
    }

    void paragraphEnter_atStartOfMidDocParagraph_cursorOnUserContent() {
        using namespace Markoff;
        using namespace Markoff::LiveRender;
        // Dogfood Bug 3 (Task 18 pass 2): pressing Enter at qtPos 0 of a
        // paragraph in the MIDDLE of a multi-paragraph document must leave
        // the cursor on the user's content row, not on the row AFTER it.
        // The simple single-paragraph atStart test (above) passed by
        // coincidence; multi-paragraph context exposes an off-by-one.
        MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *model   = binding.model();
        auto *cs      = binding.cursorState();
        auto *handler = binding.structuralKeyHandler();

        doc.resetContent(QByteArrayLiteral("prev\n\nP\n\nQ\n"), Origin::TestFixture);
        QTRY_COMPARE(model->rowCount(), 3);
        QCOMPARE(model->recordAt(0).text, QStringLiteral("prev"));
        QCOMPARE(model->recordAt(1).text, QStringLiteral("P"));
        QCOMPARE(model->recordAt(2).text, QStringLiteral("Q"));

        bool handled = handler->tryHandle(Qt::Key_Return, Qt::NoModifier,
                                          /*blockIndex=*/1,
                                          /*qtPos=*/0,
                                          /*selectionEmpty=*/true,
                                          /*blockText=*/QStringLiteral("P"));
        QVERIFY(handled);
        QTRY_COMPARE(model->rowCount(), 4);

        // Post-edit layout:
        QCOMPARE(model->recordAt(0).text, QStringLiteral("prev"));
        // row 1 is the marker paragraph (text = QString(kMarkerChar))
        QCOMPARE(model->recordAt(1).text, QString(kMarkerChar));
        // row 2 is P (the user's content)
        QCOMPARE(model->recordAt(2).text, QStringLiteral("P"));
        // row 3 is Q
        QCOMPARE(model->recordAt(3).text, QStringLiteral("Q"));

        // Cursor must land in P (row 2), not in Q (row 3).
        QTRY_COMPARE(cs->focusedAnchorRow(), 2);
        QCOMPARE(cs->focusedQtPos(), 0);
    }

    void paragraphEnter_atStartOfMidDocParagraph_largerDoc_cursorOnUserContent() {
        using namespace Markoff;
        using namespace Markoff::LiveRender;
        // Larger-doc variant: more paragraphs, the target is "in the last
        // third" per dogfood report wording.
        MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *model   = binding.model();
        auto *cs      = binding.cursorState();
        auto *handler = binding.structuralKeyHandler();

        QByteArray src;
        for (int i = 0; i < 9; ++i) {
            src += QByteArrayLiteral("para") + QByteArray::number(i) + QByteArrayLiteral("\n\n");
        }
        // 9 paragraphs separated by blank lines.
        doc.resetContent(src, Origin::TestFixture);
        QTRY_COMPARE(model->rowCount(), 9);
        QCOMPARE(model->recordAt(6).text, QStringLiteral("para6"));

        bool handled = handler->tryHandle(Qt::Key_Return, Qt::NoModifier,
                                          /*blockIndex=*/6,
                                          /*qtPos=*/0,
                                          /*selectionEmpty=*/true,
                                          /*blockText=*/QStringLiteral("para6"));
        QVERIFY(handled);
        QTRY_COMPARE(model->rowCount(), 10);

        QCOMPARE(model->recordAt(6).text, QString(kMarkerChar));
        QCOMPARE(model->recordAt(7).text, QStringLiteral("para6"));
        QCOMPARE(model->recordAt(8).text, QStringLiteral("para7"));

        // Cursor must land on para6 at row 7.
        QTRY_COMPARE(cs->focusedAnchorRow(), 7);
        QCOMPARE(cs->focusedQtPos(), 0);
    }

    void paragraphEnter_atStartOfMidDocParagraph_textVerifies() {
        using namespace Markoff;
        using namespace Markoff::LiveRender;
        // Dogfood Bug 3 v2 (Task 18 pass 3): the previous regression tests
        // asserted by row index, which the broken anchor-resolution path
        // happened to satisfy on tiny single-character paragraphs. With
        // longer multi-line paragraphs (BlockWalker glues consecutive lines
        // into one row), the dogfood scenario lands the cursor on the
        // ORIGINALLY-FOLLOWING paragraph instead of the user's shifted
        // content. Verify by TEXT CONTENT, not just by row index.
        MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *model   = binding.model();
        auto *cs      = binding.cursorState();
        auto *handler = binding.structuralKeyHandler();

        // Three paragraphs of distinct lengths and content. The middle one
        // ("beta") spans multiple source lines (BlockWalker glues them);
        // the trailing one ("gamma") is intentionally longer so a wrong-
        // row cursor lands on a row whose text is recognisably gamma.
        const QByteArray src =
            QByteArrayLiteral("alpha alpha alpha alpha\n\n")
          + QByteArrayLiteral("beta line one and two and three\n")
          + QByteArrayLiteral("beta line four continued\n\n")
          + QByteArrayLiteral("gamma gamma gamma gamma gamma gamma gamma gamma\n");
        doc.resetContent(src, Origin::TestFixture);
        QSignalSpy parseSpy(&doc, &MarkoffDocument::parseUpdated);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(model->rowCount(), 3);
        QCOMPARE(model->recordAt(0).text, QStringLiteral("alpha alpha alpha alpha"));
        // beta is two source lines glued by BlockWalker.
        QVERIFY(model->recordAt(1).text.startsWith(QStringLiteral("beta line one")));
        QVERIFY(model->recordAt(2).text.startsWith(QStringLiteral("gamma")));

        const QString preEditBetaText = model->recordAt(1).text;

        bool handled = handler->tryHandle(Qt::Key_Return, Qt::NoModifier,
                                          /*blockIndex=*/1,
                                          /*qtPos=*/0,
                                          /*selectionEmpty=*/true,
                                          /*blockText=*/preEditBetaText);
        QVERIFY(handled);
        QTRY_COMPARE(model->rowCount(), 4);

        // Post-edit layout:
        //   row 0: alpha
        //   row 1: <marker>
        //   row 2: beta (the user's content — what the cursor MUST follow)
        //   row 3: gamma
        QCOMPARE(model->recordAt(0).text, QStringLiteral("alpha alpha alpha alpha"));
        QCOMPARE(model->recordAt(1).text, QString(kMarkerChar));
        QCOMPARE(model->recordAt(2).text, preEditBetaText);
        QVERIFY(model->recordAt(3).text.startsWith(QStringLiteral("gamma")));

        // The crucial assertion: the cursor's resolved row's TEXT must equal
        // the user's pre-edit content (beta), NOT the originally-following
        // gamma paragraph. Row index is a secondary check; if it disagrees
        // with the text check, the text check is authoritative.
        QTRY_VERIFY(cs->focusedAnchorRow() >= 0);
        const int resolvedRow = cs->focusedAnchorRow();
        const QString resolvedText = model->recordAt(resolvedRow).text;
        QCOMPARE(resolvedText, preEditBetaText);
        QVERIFY2(!resolvedText.startsWith(QStringLiteral("gamma")),
                 "cursor landed on the originally-following paragraph");
        QCOMPARE(cs->focusedQtPos(), 0);
    }

    void paragraphEnter_onMarkerOnlyBlock_isNoOp() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent(QStringLiteral("alpha\n\n%1\n").arg(kMarkerChar).toUtf8(),
                         Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 2);

        auto *handler = binding.structuralKeyHandler();
        QVERIFY(handler);

        const QString preEditSrc = QString::fromUtf8(doc.toMarkdownUtf8());
        const quint64 preEditSeq = doc.editSequence();

        const bool handled = handler->tryHandle(
            Qt::Key_Return, /*mods=*/Qt::NoModifier,
            /*blockIndex=*/1,
            /*qtPos=*/0,
            /*selectionEmpty=*/true,
            /*blockText=*/QString(kMarkerChar));
        QVERIFY(handled);                          // keystroke consumed
        QCOMPARE(doc.editSequence(), preEditSeq);  // no source edit
        QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()), preEditSrc);
    }

    // ---------- R5.5 Task 9 — Backspace at qtPos 0 after marker block ----------

    void backspace_atQt0_afterMarkerBlock_scrubsMarker() {
        using namespace Markoff;
        using namespace Markoff::LiveRender;
        MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        auto *model   = binding.model();
        auto *handler = binding.structuralKeyHandler();

        doc.resetContent(QStringLiteral("alpha\n\n%1\n\nbeta\n").arg(kMarkerChar).toUtf8(),
                         Origin::TestFixture);
        QTRY_COMPARE(model->rowCount(), 3);

        bool handled = handler->tryHandle(Qt::Key_Backspace, Qt::NoModifier,
                                          /*blockIndex=*/2,
                                          /*qtPos=*/0,
                                          /*selectionEmpty=*/true,
                                          /*blockText=*/QStringLiteral("beta"));
        QVERIFY(handled);
        QTRY_COMPARE(model->rowCount(), 2);
        QCOMPARE(QString::fromUtf8(doc.toMarkdownUtf8()),
                 QStringLiteral("alpha\n\nbeta\n"));
    }

// ---------- R5.5 Bug 1 (Task 18 dogfood) — list-item Enter ----------

    void paragraphEnter_atStartOfListItem_doesNotMangleList() {
        using namespace Markoff;
        using namespace Markoff::LiveRender;
        MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        auto *model   = binding.model();
        auto *handler = binding.structuralKeyHandler();

        doc.resetContent(QByteArrayLiteral("- item1\n- item2\n- item3\n"),
                         Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_VERIFY(model->rowCount() >= 1);

        // Locate the row that contains "item2". The whole list is one row in
        // R2's BlockWalker mapping (lists collapse to BlockKind::Paragraph),
        // so this is row 0; the loop is defensive in case classification
        // changes later.
        int item2Row = -1;
        for (int i = 0; i < model->rowCount(); ++i) {
            if (model->recordAt(i).text.contains(QStringLiteral("item2"))) {
                item2Row = i;
                break;
            }
        }
        QVERIFY(item2Row >= 0);

        // Find the qtPos of "- item2" within that row's text. The dogfood
        // scenario is "caret BEFORE the dash of item2"; with the list
        // collapsed into one row, that's mid-block, not row-start.
        const QString rowText = model->recordAt(item2Row).text;
        const int qtPosBeforeItem2 = rowText.indexOf(QStringLiteral("- item2"));
        QVERIFY(qtPosBeforeItem2 >= 0);

        const QString preEditSrc = QString::fromUtf8(doc.toMarkdownUtf8());

        const bool handled = handler->tryHandle(
            Qt::Key_Return, Qt::NoModifier,
            item2Row, qtPosBeforeItem2,
            /*selectionEmpty=*/true,
            rowText);

        // Per spec §0 the marker design is paragraph-only. If the handler
        // claims it (mid-block split), it must NOT inject a ZWSP marker;
        // even better, the gate returns NotHandled so QTextEdit's default
        // Enter handling delivers a soft-break — non-destructive.
        if (handled) {
            QVERIFY(!QString::fromUtf8(doc.toMarkdownUtf8())
                         .contains(QChar(0x200B)));
        }
        // Source must not have been mangled — all three items still present.
        const QString postEditSrc = QString::fromUtf8(doc.toMarkdownUtf8());
        QVERIFY(postEditSrc.contains(QStringLiteral("item1")));
        QVERIFY(postEditSrc.contains(QStringLiteral("item2")));
        QVERIFY(postEditSrc.contains(QStringLiteral("item3")));

        // Stronger invariant: source must remain a single contiguous list.
        // The destructive failure mode inserts "\n\n" between list items,
        // which CommonMark interprets as TWO lists with an empty paragraph
        // between. The post-edit source must NOT contain a "\n\n" between
        // two list-marker lines.
        QVERIFY2(!postEditSrc.contains(QStringLiteral("\n\n-")),
                 qPrintable(QStringLiteral("source split list with \\n\\n: ") + postEditSrc));
    }

    void paragraphEnter_shiftEnterOnMarkerBlock_insertsSoftBreak() {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QSignalSpy parseSpy(&doc, &Markoff::MarkoffDocument::parseUpdated);
        doc.resetContent(QStringLiteral("alpha\n\n%1\n").arg(kMarkerChar).toUtf8(),
                         Markoff::Origin::FirstOpen);
        QVERIFY(parseSpy.wait(2000));
        QTRY_COMPARE(binding.model()->rowCount(), 2);

        auto *handler = binding.structuralKeyHandler();
        QVERIFY(handler);

        const quint64 preEditSeq = doc.editSequence();

        const bool handled = handler->tryHandle(
            Qt::Key_Return, /*mods=*/Qt::ShiftModifier,
            /*blockIndex=*/1,
            /*qtPos=*/0,
            /*selectionEmpty=*/true,
            /*blockText=*/QString(kMarkerChar));
        QVERIFY(handled);
        // Source MUST have changed (a soft-break was inserted into the marker
        // block) — i.e., editSequence advanced.
        QVERIFY(doc.editSequence() > preEditSeq);
        // The marker block now contains a soft-break newline. The marker
        // character is still present and the source is no longer the pre-edit shape.
        QVERIFY(QString::fromUtf8(doc.toMarkdownUtf8()).contains(kMarkerChar));
    }

};

QTEST_MAIN(TstLiveRenderStructural)
#include "tst_live_render_structural.moc"
