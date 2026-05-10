// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveStructuralKeyHandler.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/UndoLog.h>

using namespace Markoff::Live;

class TestSetextE2E : public QObject {
    Q_OBJECT

private:
    static bool waitForModelRows(LiveListModelBinding &binding,
                                  Markoff::MarkoffDocument &doc,
                                  const QByteArray &markdown, int expectedRows)
    {
        doc.loadFromMarkdown(markdown);
        for (int i = 0; i < 50; ++i) {
            QTest::qWait(10);
            if (binding.model()->rowCount() == expectedRows)
                return true;
        }
        return binding.model()->rowCount() == expectedRows;
    }

private Q_SLOTS:

    void typeShiftEnterDashes_producesSetextH2()
    {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "Heading", 1));

        // Shift+Enter at end of "Heading".
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::ShiftModifier, 0, 7, true,
            QStringLiteral("Heading"));
        QTest::qWait(50);

        // Type "---" (simulate via direct buffer edit at byte offset 8).
        auto id = binding.model()->recordAt(0).blockAnchor;
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 8, 0, QByteArray("---"), t);
        }
        QTest::qWait(100);

        const auto &rec = binding.model()->recordAt(0);
        QCOMPARE(binding.model()->rowCount(), 1);
        QCOMPARE(rec.kind, BlockKind::Heading);
        QCOMPARE(rec.headingLevel, 2);
        QCOMPARE(rec.headingForm, QString("setext"));
        QCOMPARE(rec.text, QStringLiteral("Heading\n---"));
    }

    void typeShiftEnterEquals_producesSetextH1()
    {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        QVERIFY(waitForModelRows(binding, doc, "Title", 1));

        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::ShiftModifier, 0, 5, true,
            QStringLiteral("Title"));
        QTest::qWait(50);

        auto id = binding.model()->recordAt(0).blockAnchor;
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 6, 0, QByteArray("==="), t);
        }
        QTest::qWait(100);

        const auto &rec = binding.model()->recordAt(0);
        QCOMPARE(rec.kind, BlockKind::Heading);
        QCOMPARE(rec.headingLevel, 1);
        QCOMPARE(rec.headingForm, QString("setext"));
    }

    void loadSetext_editContent_save_preservesForm()
    {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        doc.loadFromMarkdown("Heading\n---\n");
        auto id = doc.iterateBlocks().front();
        QCOMPARE(doc.blockKind(id), Markoff::BlockKind::Heading);

        // Edit the heading content (insert " Edited" at byte 7, before \n---).
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 7, 0, QByteArray(" Edited"), t);
            // buffer is now "Heading Edited\n---"
        }

        const QByteArray saved = doc.serializeForSave();
        QVERIFY2(saved.contains("Heading Edited\n---"), saved.constData());
        QVERIFY2(!saved.contains("## "), saved.constData());  // not converted to ATX

        // Round-trip: load the saved bytes into a fresh doc.
        Markoff::MarkoffDocument doc2(/*replicaId=*/2);
        doc2.loadFromMarkdown(saved);
        auto id2 = doc2.iterateBlocks().front();
        QCOMPARE(doc2.blockKind(id2), Markoff::BlockKind::Heading);
        auto attrs2 = doc2.blockAttrs(id2);
        QCOMPARE(std::get<QString>(attrs2.value(Markoff::AttrNames::HeadingForm)),
                 QString("setext"));
        QCOMPARE(std::get<int>(attrs2.value(Markoff::AttrNames::Level)), 2);
    }

    // ------------------------------------------------------------------
    // Regression tests for setext-dogfood findings S1, S2, S3
    // (docs/handoff/2026-05-09-setext-dogfood-findings.md). Each
    // verifies that a kind transition driven by a buffer edit
    // re-anchors the caret on the new delegate via
    // requestTextCaretAtAnchor — without the fix the cursor either
    // disappears (S1/S2) or jumps to position 0 (S3).
    // ------------------------------------------------------------------

    void S1_setextDemote_lastUnderlineCharDeleted_keepsCursor()
    {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // Build the setext H1 the way a typing user does so the buffer
        // state is unambiguous (no trailing-newline question from
        // loadFromMarkdown).
        QVERIFY(waitForModelRows(binding, doc, "Heading", 1));
        binding.structuralKeyHandler()->tryHandle(
            Qt::Key_Return, Qt::ShiftModifier, 0, 7, true,
            QStringLiteral("Heading"));
        QTest::qWait(50);

        const auto id = binding.model()->recordAt(0).blockAnchor;
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 8, 0, QByteArray("="), t);  // buffer: "Heading\n="
        }
        QTest::qWait(100);
        QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);
        QCOMPARE(binding.model()->recordAt(0).headingForm, QString("setext"));

        // User caret sat at qtPos=9 (after "="). Delete the "="
        // → buffer "Heading\n" (8 bytes), post-edit cursor at qtPos=8.
        binding.cursorState()->syncFromTextEdit(id,8);
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 8, 1, QByteArray(), t);  // delete "="
        }
        QTest::qWait(100);

        const auto &rec = binding.model()->recordAt(0);
        QCOMPARE(rec.kind, BlockKind::Paragraph);
        // Buffer "Heading\n" → chop trailing \n → rec.text "Heading" (7 chars).
        QCOMPARE(rec.text, QStringLiteral("Heading"));

        // qtPos clamped to text length: min(8, 7) = 7.
        QCOMPARE(binding.cursorState()->focusedAnchorRow(), 0);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 7);
    }

    void S2_setextPromote_firstUnderlineCharTyped_keepsFocus()
    {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // Plain paragraph with a soft-break already in place: "abc\n".
        // The user is about to type "-" at byte 4, triggering setext H2 promote.
        QVERIFY(waitForModelRows(binding, doc, "abc\n", 1));
        QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Paragraph);

        const auto id = binding.model()->recordAt(0).blockAnchor;

        // Type "-" at qtPos=4 → cursor lands at qtPos=5.
        binding.cursorState()->syncFromTextEdit(id,5);
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 4, 0, QByteArray("-"), t);
        }
        QTest::qWait(100);

        const auto &rec = binding.model()->recordAt(0);
        QCOMPARE(rec.kind, BlockKind::Heading);
        QCOMPARE(rec.headingForm, QString("setext"));
        QCOMPARE(rec.headingLevel, 2);

        // Cursor lands at qtPos=5 (end of "abc\n-").
        QCOMPARE(binding.cursorState()->focusedAnchorRow(), 0);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 5);
    }

    void S3_atxDemote_spaceAfterHashesDeleted_keepsCursorAtDeletionPoint()
    {
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // ATX H2.
        QVERIFY(waitForModelRows(binding, doc, "## My Heading\n", 1));
        QCOMPARE(binding.model()->recordAt(0).kind, BlockKind::Heading);
        QCOMPARE(binding.model()->recordAt(0).headingForm, QString("atx"));

        const auto id = binding.model()->recordAt(0).blockAnchor;

        // Caret was at qtPos=3 (after the space between "##" and "My").
        // Delete the space at byte 2 → buffer "##My Heading", caret at qtPos=2.
        binding.cursorState()->syncFromTextEdit(id,2);
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 2, 1, QByteArray(), t);
        }
        QTest::qWait(100);

        const auto &rec = binding.model()->recordAt(0);
        QCOMPARE(rec.kind, BlockKind::Paragraph);
        QCOMPARE(rec.text, QStringLiteral("##My Heading"));

        // Cursor stays at qtPos=2, NOT 0.
        QCOMPARE(binding.cursorState()->focusedAnchorRow(), 0);
        QCOMPARE(binding.cursorState()->focusedQtPos(), 2);
    }

    void typeDashes_inPlainParagraph_producesHorizontalRule()
    {
        // Regression: bare `---` in a paragraph (not preceded by a heading
        // line) still becomes HR, not setext-H2.
        Markoff::MarkoffDocument doc(/*replicaId=*/1);
        LiveListModelBinding binding;
        binding.setDocument(&doc);
        // Load a single-character paragraph so we get one row to work with.
        QVERIFY(waitForModelRows(binding, doc, "x", 1));

        // Replace "x" with "---" (remove 1 byte, insert 3) — simulates the
        // user selecting-all and typing "---".
        auto id = binding.model()->recordAt(0).blockAnchor;
        {
            Markoff::UndoLog::Transaction t(doc.d2UndoLog());
            doc.d2ApplyBufferEdit(id, 0, 1, QByteArray("---"), t);
        }
        QTest::qWait(100);

        QCOMPARE(binding.model()->recordAt(0).kind,
                 BlockKind::HorizontalRule);
    }
};

QTEST_MAIN(TestSetextE2E)
#include "tst_live_render_setext_e2e.moc"
