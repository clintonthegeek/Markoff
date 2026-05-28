// SPDX-License-Identifier: GPL-3.0-or-later
//
// NOTE: QTextDocument::contentsChange (with position args) only fires when a
// QAbstractTextDocumentLayout is installed on the document (as QPlainTextEdit does).
// Tests must use QPlainTextEdit::document() rather than a raw QTextDocument.
//
#include <QTest>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextDocument>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/SourceTextDocumentBinding.h>

namespace { QByteArray flat(Markoff::MarkoffDocument &d) { return d.flatView(); } }

class TstBindingForward : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_at_block_boundary_lands_in_previous_block() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(edit.document());
        b.setMarkoffDocument(&doc);
        // WP unification: widget view uses single '\n' between blocks.
        QCOMPARE(edit.toPlainText(), QStringLiteral("alpha\nbeta"));
        // End of "alpha" is qtPos 5 (before the '\n' separator).
        QTextCursor c(edit.document());
        c.setPosition(5);
        c.insertText(QStringLiteral(" "));   // fires contentsChange
        QCOMPARE(flat(doc), QByteArrayLiteral("alpha \n\nbeta"));   // space in block 0
        // Widget view reflects the change; still single '\n' separator.
        QCOMPARE(edit.toPlainText(), QStringLiteral("alpha \nbeta")); // no drift
    }
    void typing_mid_block_unaffected() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(edit.document());
        b.setMarkoffDocument(&doc);
        QTextCursor c(edit.document());
        c.setPosition(2);
        c.insertText(QStringLiteral("X"));
        QCOMPARE(flat(doc), QByteArrayLiteral("alXpha\n\nbeta"));
    }
    void backspace_over_separator_merges_blocks() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(edit.document());
        b.setMarkoffDocument(&doc);
        // WP unification: widget view uses single '\n' between blocks.
        // Select the '\n' separator (qtPos 5..6) and delete it.
        QTextCursor c(edit.document());
        c.setPosition(5);
        c.setPosition(6, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        QCOMPARE(int(doc.iterateBlocks().size()), 1);
        QCOMPARE(flat(doc), QByteArrayLiteral("alphabeta"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("alphabeta"));
    }
    void cross_block_selection_delete_merges_content() {
        // Delete spanning content of TWO blocks + the separator must merge
        // into ONE canonical block (Tier 2 cross-block path).
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("hello\n\nworld"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(edit.document());
        b.setMarkoffDocument(&doc);
        // WP unification: widget view uses single '\n' between blocks.
        QCOMPARE(edit.toPlainText(), QStringLiteral("hello\nworld"));

        // Select qtPos 3..8 = "lo\nwo" (mid-block0 through the single-'\n'
        // separator into mid-block1) and delete. Expect one merged block "helrld".
        QTextCursor c(edit.document());
        c.setPosition(3);
        c.setPosition(8, QTextCursor::KeepAnchor);
        c.removeSelectedText();

        QCOMPARE(int(doc.iterateBlocks().size()), 1);
        QCOMPARE(flat(doc), QByteArrayLiteral("helrld"));
        QCOMPARE(edit.toPlainText(), QStringLiteral("helrld"));
    }
};

QTEST_MAIN(TstBindingForward)
#include "tst_binding_forward.moc"
