// SPDX-License-Identifier: GPL-3.0-or-later
//
// NOTE: QTextDocument::contentsChange (with position args) only fires when a
// QAbstractTextDocumentLayout is installed on the document (as QPlainTextEdit does).
// Tests must use QPlainTextEdit::document() rather than a raw QTextDocument.
//
#include <QTest>
#include <QCoreApplication>
#include <QPlainTextEdit>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/SourceTextDocumentBinding.h>

// d2DocumentChanged is debounced via QTimer::singleShot(0,...); pump the event
// loop after any applyFlatEdit call to let the signal reach the binding.
static void pumpEvents() { QCoreApplication::processEvents(); }

class TstBindingReverse : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void remote_edit_preserves_formatting_outside_change() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(edit.document());
        b.setMarkoffDocument(&doc);
        // WP unification: widget view uses single '\n' between blocks.
        QCOMPARE(edit.document()->toPlainText(), QStringLiteral("alpha\nbeta"));

        // Apply a distinctive 22pt char format to "beta" (positions 6..10).
        {
            QTextCursor c(edit.document());
            c.setPosition(6); c.setPosition(10, QTextCursor::KeepAnchor);
            QTextCharFormat f; f.setFontPointSize(22.0);
            c.mergeCharFormat(f);
        }
        // Remote edit changes ONLY "alpha": prepend "X" via applyFlatEdit
        // directly on the doc (simulates a remote peer; m_applyingLocalEdit
        // is false, so the reverse path engages).
        doc.applyFlatEdit(0, 0, QByteArrayLiteral("X"), Markoff::Origin::UserEdit);
        pumpEvents();  // d2DocumentChanged is debounced via QTimer::singleShot(0)

        QCOMPARE(edit.document()->toPlainText(), QStringLiteral("Xalpha\nbeta"));
        // "beta"'s 22pt format survived (shifted +1 → positions 7..11).
        QTextCursor probe(edit.document());
        probe.setPosition(8); probe.setPosition(9, QTextCursor::KeepAnchor);
        QCOMPARE(probe.charFormat().fontPointSize(), 22.0);
    }

    void remote_edit_is_targeted_not_full_replace() {
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(edit.document());
        b.setMarkoffDocument(&doc);
        doc.applyFlatEdit(0, 0, QByteArrayLiteral("X"), Markoff::Origin::UserEdit);
        pumpEvents();
        // WP unification: widget view uses single '\n' between blocks.
        QCOMPARE(edit.document()->toPlainText(), QStringLiteral("Xalpha\nbeta"));
    }

    void formatting_preserved_on_same_length_substitution() {
        // Verify formatting is preserved when a remote same-length substitution
        // on block-1 leaves the "beta" (block-2) span untouched.
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(edit.document());
        b.setMarkoffDocument(&doc);

        // WP unification: widget view uses single '\n' between blocks.
        // Apply 22pt format to "beta" (positions 6..10).
        {
            QTextCursor c(edit.document());
            c.setPosition(6); c.setPosition(10, QTextCursor::KeepAnchor);
            QTextCharFormat f; f.setFontPointSize(22.0);
            c.mergeCharFormat(f);
        }

        // Change only "alpha" block — "beta" block (positions 6..10) is outside
        // the changed span; the incremental diff must leave it untouched.
        // applyFlatEdit(0, 5, "ALPHA") replaces "alpha" with "ALPHA" in no-sep
        // coordinates, leaving "beta" at the same relative position.
        doc.applyFlatEdit(0, 5, QByteArrayLiteral("ALPHA"), Markoff::Origin::UserEdit);
        pumpEvents();  // d2DocumentChanged is debounced via QTimer::singleShot(0)
        QCOMPARE(edit.document()->toPlainText(), QStringLiteral("ALPHA\nbeta"));

        // "beta"'s 22pt format survived (still at positions 6..10 — same offsets,
        // because "alpha" and "ALPHA" are the same length in this block).
        QTextCursor probe(edit.document());
        probe.setPosition(7); probe.setPosition(8, QTextCursor::KeepAnchor);
        QCOMPARE(probe.charFormat().fontPointSize(), 22.0);
    }

    void in_sync_reverse_is_noop() {
        // Exercises the onD2DocumentChanged actual==widgetFlatView() early-return.
        // When a local edit goes through QPlainTextEdit's cursor, the forward
        // path (onContentsChange) updates both the QTextDocument and the CRDT
        // buffer in lockstep.  When the deferred d2DocumentChanged fires,
        // onD2DocumentChanged computes widgetFlatView() == actual and early-returns
        // without issuing any QTextCursor edit — leaving "beta"'s format intact.
        Markoff::MarkoffDocument doc(1);
        doc.loadFromMarkdown(QByteArrayLiteral("alpha\n\nbeta"));
        QPlainTextEdit edit;
        Markoff::SourceTextDocumentBinding b;
        b.setTextDocument(edit.document());
        b.setMarkoffDocument(&doc);

        // WP unification: widget view uses single '\n' between blocks.
        // Apply a distinctive 22pt char format to "beta" (positions 6..10).
        {
            QTextCursor c(edit.document());
            c.setPosition(6); c.setPosition(10, QTextCursor::KeepAnchor);
            QTextCharFormat f; f.setFontPointSize(22.0);
            c.mergeCharFormat(f);
        }

        // Type via the QPlainTextEdit cursor (forward path updates the
        // QTextDocument AND the CRDT in lockstep).  When the deferred
        // d2DocumentChanged fires, onD2DocumentChanged sees actual == widgetFlatView()
        // and early-returns — no QTextCursor edit, so "beta"'s format is untouched.
        QTextCursor tc(edit.document());
        tc.setPosition(2);
        tc.insertText(QStringLiteral("X"));   // "alXpha\nbeta"
        pumpEvents();  // deliver deferred d2DocumentChanged → early-return path

        QCOMPARE(edit.document()->toPlainText(), QStringLiteral("alXpha\nbeta"));
        // "beta" shifted +1 → positions 7..11; probe character at position 8.
        QTextCursor probe(edit.document());
        probe.setPosition(8); probe.setPosition(9, QTextCursor::KeepAnchor);
        QCOMPARE(probe.charFormat().fontPointSize(), 22.0);
    }
};

QTEST_MAIN(TstBindingReverse)
#include "tst_binding_reverse.moc"
