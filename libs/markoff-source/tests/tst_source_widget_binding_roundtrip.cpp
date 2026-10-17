// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include <markoff/source/Editor.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/Session.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>
namespace {
struct CandidateDocument {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};
CandidateDocument openDocument(const QByteArray& markdown)
{
    static int serial = 0;
    auto provider = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("provider creation failed");
    CandidateDocument fixture{std::move(provider).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider,
        {QStringLiteral("source-binding-%1").arg(++serial), 1},
        Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    const auto imported = fixture.document->importInitialMarkdown(markdown);
    if (imported.state != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    if (!fixture.document->canStartOrdinaryAction())
        throw std::runtime_error("initial import did not open ordinary edits; provider state "
            + std::to_string(static_cast<int>(fixture.document->providerState()))
            + (imported.diagnostic ? "; " + std::string(imported.diagnostic->message())
                                   : std::string{}));
    return fixture;
}
}

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
        auto fixture = openDocument(QByteArrayLiteral(""));
        auto& doc = *fixture.document;
        e.setDocument(&doc);
        e.show();
        // Key events must target the inner QPlainTextEdit where text input lands.
        QTest::keyClicks(e.plainTextEdit(), QStringLiteral("abc"));
        QTRY_COMPARE(fullText(doc), QByteArrayLiteral("abc"));
    }

    void external_doc_edit_propagates_to_editor() {
        Markoff::Source::Editor e;
        auto fixture = openDocument(QByteArrayLiteral(""));
        auto& doc = *fixture.document;
        e.setDocument(&doc);

        const auto result = doc.applyBlockEdit({doc.iterateBlocks().front(), 0, 0,
                                                QByteArrayLiteral("hello")});
        QVERIFY2(result.state == Markoff::VNext::ActionPublicationState::committed,
                 result.diagnostic ? result.diagnostic->message().data()
                                   : "candidate edit was not committed");
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
        auto fixture = openDocument(QByteArrayLiteral(""));
        auto& doc = *fixture.document;
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

    void typing_at_boundary_lands_in_correct_block() {
        // End-to-end regression guard: typing at a block boundary must land
        // the character in the correct (preceding) block, not the following
        // one, and must not create a spurious third block.
        Markoff::Source::Editor e;
        auto fixture = openDocument(QByteArrayLiteral("alpha\n\nbeta"));
        auto& doc = *fixture.document;
        e.setDocument(&doc);
        e.show();
        // End of "alpha" = qtPos 5 (before the "\n\n").
        QTextCursor c = e.plainTextEdit()->textCursor();
        c.setPosition(5);
        e.plainTextEdit()->setTextCursor(c);
        QTest::keyClicks(e.plainTextEdit(), QStringLiteral(" "));
        QTRY_COMPARE(doc.blockText(doc.iterateBlocks()[0]), QByteArrayLiteral("alpha "));
        QCOMPARE(int(doc.iterateBlocks().size()), 2);   // still 2 blocks, no drift
    }
};

QTEST_MAIN(TstSourceWidgetBindingRoundtrip)
#include "tst_source_widget_binding_roundtrip.moc"
