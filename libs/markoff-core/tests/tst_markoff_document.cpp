// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>

#include <markoff/MarkoffDocument.h>
#include <markoff/MarkdownDelta.h>

using namespace Markoff;

class TstMarkoffDocument : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void resetContentFirstOpenEmitsContentsChanged() {
        MarkoffDocument doc;
        QSignalSpy spy(&doc, &MarkoffDocument::contentsChanged);
        doc.resetContent(QStringLiteral("hello"), Origin::FirstOpen);
        QCOMPARE(doc.toMarkdown(), QStringLiteral("hello"));
        QVERIFY(spy.count() >= 1);
    }

    void replaceMutatesTextAndEmits() {
        MarkoffDocument doc;
        doc.resetContent(QStringLiteral("hello world"), Origin::FirstOpen);
        QSignalSpy spy(&doc, &MarkoffDocument::contentsChanged);
        doc.undoStack()->push(new MarkdownDelta(&doc, 6, 5, QStringLiteral("there")));
        QCOMPARE(doc.toMarkdown(), QStringLiteral("hello there"));
        QVERIFY(spy.count() >= 1);
    }

    void insertAndRemove() {
        MarkoffDocument doc;
        doc.resetContent(QStringLiteral("ac"), Origin::FirstOpen);
        doc.undoStack()->push(new MarkdownDelta(&doc, 1, 0, QStringLiteral("b")));
        QCOMPARE(doc.toMarkdown(), QStringLiteral("abc"));
        doc.undoStack()->push(new MarkdownDelta(&doc, 0, 1, QString()));
        QCOMPARE(doc.toMarkdown(), QStringLiteral("bc"));
    }

    void transactionBoundaryCoalescesUndo() {
        MarkoffDocument doc;
        doc.resetContent(QStringLiteral("hello"), Origin::FirstOpen);
        const QString beforeTxn = doc.toMarkdown();

        doc.undoStack()->beginMacro(QStringLiteral("two-inserts"));
        doc.undoStack()->push(new MarkdownDelta(&doc, 5, 0, QStringLiteral(" a")));
        doc.undoStack()->push(
            new MarkdownDelta(&doc, doc.toMarkdown().size(), 0, QStringLiteral(" b")));
        doc.undoStack()->endMacro();

        QCOMPARE(doc.toMarkdown(), QStringLiteral("hello a b"));

        // One undo should revert the entire macro.
        doc.undoStack()->undo();
        QCOMPARE(doc.toMarkdown(), beforeTxn);
    }

    void untransactedEditsAreIndividuallyUndoable() {
        // Use replace-style deltas (non-pure-insert) so mergeWith never coalesces them.
        MarkoffDocument doc;
        doc.resetContent(QStringLiteral("_b_"), Origin::FirstOpen);

        // Replace '_' at offset 0 with 'a'.
        doc.undoStack()->push(new MarkdownDelta(&doc, 0, 1, QStringLiteral("a")));
        QCOMPARE(doc.toMarkdown(), QStringLiteral("ab_"));
        // Replace '_' at offset 2 with 'c'.
        doc.undoStack()->push(new MarkdownDelta(&doc, 2, 1, QStringLiteral("c")));
        QCOMPARE(doc.toMarkdown(), QStringLiteral("abc"));

        doc.undoStack()->undo();
        QCOMPARE(doc.toMarkdown(), QStringLiteral("ab_"));
        doc.undoStack()->undo();
        QCOMPARE(doc.toMarkdown(), QStringLiteral("_b_"));
    }

    void parseCompletesAsyncForSmallDocs() {
        // Parsing is async via ParsePool debounce. After resetContent the parse
        // is scheduled; spy.wait() pumps the event loop until parseUpdated fires.
        MarkoffDocument doc;
        doc.setCoalescingIdleMs(0);  // minimize debounce for tests
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent(QStringLiteral("# Heading\n\nbody\n"), Origin::FirstOpen);
        QVERIFY(spy.wait(2000));
        QVERIFY(!doc.parseIsPending());
        QVERIFY(doc.parsedDocument() != nullptr);
    }

    void parseIsRescheduledOnEdit() {
        // After an edit the parse is invalidated (pending) and a new parseUpdated
        // fires once the pool completes the new job.
        MarkoffDocument doc;
        doc.setCoalescingIdleMs(0);
        QSignalSpy spy(&doc, &MarkoffDocument::parseUpdated);
        doc.resetContent(QStringLiteral("# h\n"), Origin::FirstOpen);
        QVERIFY(spy.wait(2000));  // first parse settles

        spy.clear();
        doc.undoStack()->push(
            new MarkdownDelta(&doc, doc.toMarkdown().size(), 0, QStringLiteral("\nmore\n")));
        // After the delta, a new parse should be scheduled and eventually complete.
        QVERIFY(spy.wait(2000));
        QVERIFY(!doc.parseIsPending());
        QVERIFY(doc.parsedDocument() != nullptr);
    }
};

QTEST_MAIN(TstMarkoffDocument)
#include "tst_markoff_document.moc"
