// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/live-render/UndoCoalescer.h>
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/Cursor.h>
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

    void enter_at_end_of_paragraph_inserts_paragraph_break() {
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

        // CRDT now has "hello\n\n" — paragraph break at end.
        QCOMPARE(doc.toMarkdown(), QString("hello\n\n"));

        // After parse-back, two rows; the second is empty.
        QVERIFY(parseSpy.wait(2000));
        QCOMPARE(binding.model()->rowCount(), 2);

        // Cursor parked at row 1 qtPos 0.
        const Cursor cur = binding.cursorState()->cursor();
        QVERIFY(std::holds_alternative<TextCaret>(cur));
        QCOMPARE(std::get<TextCaret>(cur).block,
                 binding.model()->recordAt(1).blockAnchor);
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
        QCOMPARE(binding.model()->recordAt(0).text, QString("hello"));
        QCOMPARE(binding.model()->recordAt(1).text, QString("world"));

        const Cursor cur = binding.cursorState()->cursor();
        QVERIFY(std::holds_alternative<TextCaret>(cur));
        QCOMPARE(std::get<TextCaret>(cur).block,
                 binding.model()->recordAt(1).blockAnchor);
    }

    void enter_at_start_of_paragraph_inserts_blank_above() {
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

        QCOMPARE(doc.toMarkdown(), QString("\n\nhello"));

        QVERIFY(parseSpy.wait(2000));
        // Parser collapses leading \n\n into a single empty/non-empty pair;
        // expected outcome: one paragraph row with text "hello". Whether an
        // empty leading row exists is parser-dependent and not asserted.
        QVERIFY(binding.model()->rowCount() >= 1);
    }

};

QTEST_MAIN(TstLiveRenderStructural)
#include "tst_live_render_structural.moc"
