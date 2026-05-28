// SPDX-License-Identifier: GPL-3.0-or-later
//
// NOTE: QTextDocument::contentsChange (with position args) only fires when a
// QAbstractTextDocumentLayout is installed on the document (as QPlainTextEdit
// does). Tests must use QPlainTextEdit::document() rather than a raw
// QTextDocument.
//
#include <QSignalSpy>
#include <QTest>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>

using Markoff::SourceTextDocumentBinding;

class TstStyledBindingCaret : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void enter_at_paragraph_end_resolves_caret_to_new_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        QPlainTextEdit edit;
        SourceTextDocumentBinding b;
        b.setMarkoffDocument(&doc);
        b.setTextDocument(edit.document());  // seeds qdoc with flatView "Hello\n\nWorld"
        QCOMPARE(edit.toPlainText(), QStringLiteral("Hello\n\nWorld"));

        QSignalSpy spy(&b, &SourceTextDocumentBinding::caretResolved);

        // Caret at end of "Hello" (sep-view pos 5), press Enter.
        QTextCursor c(edit.document());
        c.setPosition(5);
        c.insertText(QStringLiteral("\n"));  // fires contentsChange

        QTRY_COMPARE(spy.count(), 1);
        // New empty block sits between Hello and World; its start in sep-view
        // is after "Hello\n\n" == position 7.
        QCOMPARE(spy.at(0).at(0).toInt(), 7);
        QCOMPARE(spy.at(0).at(1).toInt(), 7);
        // Model gained a block.
        QCOMPARE(int(doc.iterateBlocks().size()), 3);
    }

    void ordinary_typing_does_not_resolve_caret() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        QPlainTextEdit edit;
        SourceTextDocumentBinding b;
        b.setMarkoffDocument(&doc);
        b.setTextDocument(edit.document());

        QSignalSpy spy(&b, &SourceTextDocumentBinding::caretResolved);

        // Type "X" inside "Hello" (pos 2).
        QTextCursor c(edit.document());
        c.setPosition(2);
        c.insertText(QStringLiteral("X"));
        QTest::qWait(50);  // let any debounced d2 signal fire

        QCOMPARE(spy.count(), 0);  // chokepoint only fires for structural ops
    }
};

QTEST_MAIN(TstStyledBindingCaret)
#include "tst_styled_binding_caret.moc"
