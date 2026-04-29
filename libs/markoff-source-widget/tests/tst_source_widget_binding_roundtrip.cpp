// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff/source/widget/Editor.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/Origin.h>
#include <markoff-foundation/Session.h>

class TstSourceWidgetBindingRoundtrip : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_propagates_to_markoff_document() {
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        e.setDocument(&doc);
        e.show();
        QTest::keyClicks(&e, QStringLiteral("abc"));
        QTRY_COMPARE(doc.toMarkdownUtf8(), QByteArrayLiteral("abc"));
    }

    void external_doc_edit_propagates_to_editor() {
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        e.setDocument(&doc);

        Markoff::MarkoffEdit ed;
        ed.oldStart = 0; ed.oldEnd = 0; ed.newText = QByteArray("hello");
        doc.applyLocalEdit({ ed });
        QTRY_COMPARE(e.toPlainText(), QStringLiteral("hello"));
    }

    void crdt_undo_via_ctrl_z() {
        // Ctrl+Z must route to MarkoffDocument::undo() (CRDT undo), not to
        // QPlainTextEdit's QTextDocument undo stack (which is disabled by
        // SourceTextDocumentBinding::rewireQtDocument).
        //
        // Each keystroke produces one applyLocalEdit -> one undo entry; the
        // foundation's coalescingIdleMs is presently unconsumed (see
        // MarkoffDocumentPrivate::coalescingIdleMs) so three keystrokes need
        // three Ctrl+Z presses to fully revert. Auto-coalescing is a
        // foundation feature, not a widget concern.
        Markoff::Source::Widget::Editor e;
        Markoff::MarkoffDocument doc(1);
        doc.setCoalescingIdleMs(0);
        e.setDocument(&doc);
        e.show();
        QTest::keyClicks(&e, QStringLiteral("abc"));
        QTRY_COMPARE(doc.toMarkdownUtf8(), QByteArrayLiteral("abc"));
        QTest::keyClick(&e, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(&e, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(&e, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(doc.toMarkdownUtf8(), QByteArray());
    }
};

QTEST_MAIN(TstSourceWidgetBindingRoundtrip)
#include "tst_source_widget_binding_roundtrip.moc"
