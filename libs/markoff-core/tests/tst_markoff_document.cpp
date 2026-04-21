// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTextDocument>

#include <markoff/MarkoffDocument.h>

using namespace Markoff;

class TstMarkoffDocument : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void setPlainTextEmitsContentsChanged() {
        // PhaseC3: rewrite in Task 8 — setPlainText() → resetContent(text, Origin::FirstOpen); plainText() → toMarkdown()
        MarkoffDocument doc;
        QSignalSpy spy(&doc, &MarkoffDocument::contentsChanged);
        doc.setPlainText(QStringLiteral("hello"));
        QCOMPARE(doc.plainText(), QStringLiteral("hello"));
        QVERIFY(spy.count() >= 1);
    }

    void replaceMutatesTextAndEmits() {
        // PhaseC3: rewrite in Task 8 — setPlainText() → resetContent(text, Origin::FirstOpen); replace() → undoStack()->push(new MarkdownDelta(...)); plainText() → toMarkdown()
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("hello world"));
        QSignalSpy spy(&doc, &MarkoffDocument::contentsChanged);
        doc.replace(6, 5, QStringLiteral("there"));
        QCOMPARE(doc.plainText(), QStringLiteral("hello there"));
        QVERIFY(spy.count() >= 1);
    }

    void insertAndRemove() {
        // PhaseC3: rewrite in Task 8 — setPlainText() → resetContent(text, Origin::FirstOpen); insert() → undoStack()->push(new MarkdownDelta(&doc, offset, 0, str)); remove() → undoStack()->push(new MarkdownDelta(&doc, offset, len, QString())); plainText() → toMarkdown()
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("ac"));
        doc.insert(1, QStringLiteral("b"));
        QCOMPARE(doc.plainText(), QStringLiteral("abc"));
        doc.remove(0, 1);
        QCOMPARE(doc.plainText(), QStringLiteral("bc"));
    }

    void transactionBoundaryCoalescesUndo() {
        // PhaseC3: rewrite in Task 8 — setPlainText() → resetContent(text, Origin::FirstOpen); textDocument() removed; use undoStack()->beginMacro()/endMacro(); plainText() → toMarkdown()
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("hello"));
        QTextDocument *td = doc.textDocument();
        const QString beforeTxn = doc.plainText();

        doc.beginTransaction();
        doc.insert(5, QStringLiteral(" a"));
        doc.insert(doc.plainText().size(), QStringLiteral(" b"));
        doc.endTransaction();

        QCOMPARE(doc.plainText(), QStringLiteral("hello a b"));

        // One undo should revert the entire transaction.
        td->undo();
        QCOMPARE(doc.plainText(), beforeTxn);
    }

    void untransactedEditsAreIndividuallyUndoable() {
        // PhaseC3: rewrite in Task 8 — setPlainText() → resetContent(text, Origin::FirstOpen); textDocument() removed; insert() → undoStack()->push(new MarkdownDelta(...)); plainText() → toMarkdown()
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("a"));
        QTextDocument *td = doc.textDocument();

        doc.insert(1, QStringLiteral("b"));
        doc.insert(2, QStringLiteral("c"));
        QCOMPARE(doc.plainText(), QStringLiteral("abc"));

        td->undo();
        QCOMPARE(doc.plainText(), QStringLiteral("ab"));
        td->undo();
        QCOMPARE(doc.plainText(), QStringLiteral("a"));
    }

    void parseIsSyncForSmallDocs() {
        // PhaseC3: rewrite in Task 8 — setPlainText() → resetContent(text, Origin::FirstOpen); parsed() → parsedDocument()
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("# Heading\n\nbody\n"));
        QVERIFY(!doc.parseIsPending());
        QVERIFY(doc.parsed() != nullptr);
    }

    void parseIsInvalidatedOnEdit() {
        // PhaseC3: rewrite in Task 8 — setPlainText() → resetContent(text, Origin::FirstOpen); insert() → undoStack()->push(new MarkdownDelta(...)); plainText() → toMarkdown(); parsed() → parsedDocument()
        MarkoffDocument doc;
        doc.setPlainText(QStringLiteral("# h\n"));
        const auto *first = doc.parsed();
        QVERIFY(first != nullptr);
        doc.insert(doc.plainText().size(), QStringLiteral("\nmore\n"));
        if (doc.parseIsPending()) {
            QVERIFY(doc.parsed() == nullptr);
        } else {
            QVERIFY(doc.parsed() != nullptr);
        }
    }
};

QTEST_MAIN(TstMarkoffDocument)
#include "tst_markoff_document.moc"
