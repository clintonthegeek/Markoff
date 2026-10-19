// SPDX-License-Identifier: GPL-3.0-or-later
#include <QPalette>
#include <QTest>

#include <markoff/source/Editor.h>
#include <markoff/core/MarkoffDocument.h>
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
        {QStringLiteral("source-widget-%1").arg(++serial), 1},
        Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    const auto imported = fixture.document->importInitialMarkdown(markdown);
    if (imported.state != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}
}
#include <markoff/core/Theme.h>

class TstSourceWidgetEditor : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ASK-M8: deleting the document under a live editor must leave the leaf
    // safe — the base retires the pointer, the binding holds a QPointer.
    // ASK-M21: the leaf's owned session dies with the leaf instead of
    // accumulating in the document across teardown.
    void owned_session_is_destroyed_with_the_editor() {
        auto fixture = openDocument(QByteArrayLiteral("hello"));
        const int before = fixture.document->sessions().size();
        {
            Markoff::Source::Editor e;
            e.setDocument(fixture.document.get());
            QCOMPARE(fixture.document->sessions().size(), before + 1);
        }
        QCOMPARE(fixture.document->sessions().size(), before);
    }

    // ASK-M20: a consumer holding a MarkdownView* can reach the session — the
    // accessor is on the base contract now, not only on the styled leaf.
    void session_is_reachable_through_the_base_contract() {
        Markoff::Source::Editor e;
        auto fixture = openDocument(QByteArrayLiteral("hello"));
        e.setDocument(fixture.document.get());
        Markoff::Session *s = fixture.document->createSession();

        Markoff::MarkdownView *base = &e;
        QCOMPARE(base->session(), e.session());   // the leaf's own session
        base->setSession(s);
        QCOMPARE(base->session(), s);
        QCOMPARE(e.session(), s);

        fixture.document->destroySession(s);
    }

    void document_deleted_under_editor_is_safe() {
        Markoff::Source::Editor e;
        auto fixture = openDocument(QByteArrayLiteral("hello world"));
        e.setDocument(fixture.document.get());
        QVERIFY(e.document() != nullptr);

        fixture.document.reset();

        QCOMPARE(e.document(), nullptr);
        e.undo();
        e.update();
        QCOMPARE(e.cursorPosition().line, 1);
    }

    void editor_constructs() {
        Markoff::Source::Editor e;
        QVERIFY(e.document() == nullptr);
    }

    void setDocument_attaches_and_seed_text_appears() {
        Markoff::Source::Editor e;
        auto fixture = openDocument(QByteArrayLiteral("hello world"));
        auto& doc = *fixture.document;
        e.setDocument(&doc);
        // setDocument triggers syncQtDocumentFromMarkoff synchronously;
        // d2DocumentChanged is deferred — let it settle.
        QTest::qWait(50);
        QCOMPARE(e.toPlainText(), QStringLiteral("hello world"));
        QCOMPARE(e.document(), &doc);
    }

    void setTheme_updates_palette_base_color() {
        Markoff::Source::Editor e;
        Markoff::Theme t = Markoff::Theme::defaultLight();
        const QColor sentinel("#abcdef");
        t.setColor(Markoff::Theme::Slot::EditorBackground, sentinel);
        e.setTheme(t);
        // Theme applies to the inner QPlainTextEdit, not the outer QWidget.
        QCOMPARE(e.plainTextEdit()->palette().color(QPalette::Base), sentinel);
    }
};

QTEST_MAIN(TstSourceWidgetEditor)
#include "tst_source_widget_editor.moc"
