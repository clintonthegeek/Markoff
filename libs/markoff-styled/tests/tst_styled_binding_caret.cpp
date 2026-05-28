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
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>
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
        b.setTextDocument(edit.document());  // seeds qdoc with widgetFlatView "Hello\nWorld"
        QCOMPARE(edit.toPlainText(), QStringLiteral("Hello\nWorld"));

        QSignalSpy spy(&b, &SourceTextDocumentBinding::caretResolved);

        // Caret at end of "Hello" (sep-view pos 5), press Enter.
        QTextCursor c(edit.document());
        c.setPosition(5);
        c.insertText(QStringLiteral("\n"));  // fires contentsChange

        QTRY_COMPARE(spy.count(), 1);
        // WP runtime view: blocks joined by single '\n', so the new empty
        // block's start is at sep-view position 6 ("Hello" + "\n").
        QCOMPARE(spy.at(0).at(0).toInt(), 6);
        QCOMPARE(spy.at(0).at(1).toInt(), 6);
        QCOMPARE(int(doc.iterateBlocks().size()), 3);
    }

    void session_selection_change_resolves_caret_in_sep_view() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("Hello\n\nWorld"));
        // Drive the existing QPlainTextEdit pattern used elsewhere in this file.
        QPlainTextEdit edit;
        SourceTextDocumentBinding b;
        auto *session = doc.createSession();
        b.setMarkoffDocument(&doc);
        b.setTextDocument(edit.document());
        b.setSession(session);

        QSignalSpy spy(&b, &SourceTextDocumentBinding::caretResolved);

        // Place a collapsed selection at the start of "World" (no-sep byte 5).
        // In sep-view that is position 6 ("Hello\n" = 6, WP unification: single '\n').
        // The OLD no-separator concatenation would have wrongly returned 5.
        const auto anchor = doc.textAnchorAt(5, /*rightBias*/ false);
        Markoff::Selection sel;
        sel.anchor = anchor;
        sel.active = anchor;
        sel.kind   = Markoff::Selection::Kind::Primary;
        session->setPrimarySelection(sel);

        QTRY_VERIFY(spy.count() >= 1);
        const auto last = spy.last();
        QCOMPARE(last.at(0).toInt(), 6);
        QCOMPARE(last.at(1).toInt(), 6);
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
