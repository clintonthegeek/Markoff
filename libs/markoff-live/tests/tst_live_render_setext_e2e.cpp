// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/LiveStructuralKeyHandler.h>
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
