// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff/source/Editor.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>

namespace {
// Read the D2 flat view: concatenation of per-block buffers.
QByteArray fullText(Markoff::MarkoffDocument &doc)
{
    QByteArray out;
    for (Markoff::BlockId id : doc.iterateBlocks())
        out += doc.blockText(id);
    return out;
}
} // namespace

class TstSourceWidgetBindingRoundtrip : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void typing_propagates_to_markoff_document() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        // D2: document must have at least one block before edits can land.
        doc.loadFromMarkdown(QByteArray());
        e.setDocument(&doc);
        e.show();
        // Key events must target the inner QPlainTextEdit where text input lands.
        QTest::keyClicks(e.plainTextEdit(), QStringLiteral("abc"));
        QTRY_COMPARE(fullText(doc), QByteArrayLiteral("abc"));
    }

    void external_doc_edit_propagates_to_editor() {
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        // D2: document must have at least one block before edits can land.
        doc.loadFromMarkdown(QByteArray());
        e.setDocument(&doc);

        doc.applyFlatEdit(0, 0, QByteArray("hello"), Markoff::Origin::UserEdit);
        // d2DocumentChanged is deferred (QTimer::singleShot(0)); let it settle.
        QCoreApplication::processEvents();
        QTRY_COMPARE(e.toPlainText(), QStringLiteral("hello"));
    }

    void crdt_undo_via_ctrl_z() {
        // Ctrl+Z must route to MarkoffDocument::undoD2() (D2 undo), not to
        // QPlainTextEdit's QTextDocument undo stack (which is disabled by
        // SourceTextDocumentBinding::rewireQtDocument).
        //
        // Each keystroke produces one applyFlatEdit -> one undo entry, so
        // three keystrokes need three Ctrl+Z presses to fully revert.
        // Auto-coalescing of adjacent inserts is a foundation concern (not
        // yet implemented), not a widget concern.
        Markoff::Source::Editor e;
        Markoff::MarkoffDocument doc(1);
        // D2: document must have at least one block before edits can land.
        doc.loadFromMarkdown(QByteArray());
        e.setDocument(&doc);
        e.show();
        // Key events must target the inner QPlainTextEdit where text input lands.
        QTest::keyClicks(e.plainTextEdit(), QStringLiteral("abc"));
        QTRY_COMPARE(fullText(doc), QByteArrayLiteral("abc"));
        // Undo key events also go to the inner widget; the event filter on
        // Editor intercepts Ctrl+Z and routes it to undoD2().
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(e.plainTextEdit(), Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE(fullText(doc), QByteArray());
    }
};

QTEST_MAIN(TstSourceWidgetBindingRoundtrip)
#include "tst_source_widget_binding_roundtrip.moc"
