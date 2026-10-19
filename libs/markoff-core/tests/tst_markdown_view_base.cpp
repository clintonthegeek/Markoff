// SPDX-License-Identifier: GPL-3.0-or-later
// Contract tests for the MarkdownView base: default behaviors every
// leaf inherits. Spec: docs/specs/2026-06-09-markdownview-contract-v2-design.md §3.
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/EditorContext.h>
#include <markoff/core/FindController.h>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

namespace {
struct CandidateFixture {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};
CandidateFixture openCandidate(const QString& key)
{
    auto provider = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("provider creation failed");
    CandidateFixture fixture{std::move(provider).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider, {key, 1},
        Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(QByteArrayLiteral("hello")).state
        != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}
}

using namespace Markoff;

class TstMarkdownViewBase : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void undo_routes_to_undoD2_and_respects_readOnly() {
        MarkdownView v;
        auto fixture = openCandidate(QStringLiteral("markdown-view-undo"));
        MarkoffDocument& doc = *fixture.document;
        v.setDocument(&doc);

        const auto block = doc.iterateBlocks().front();
        QCOMPARE(doc.applyBlockEdit({block, 5, 0, QByteArrayLiteral(" world")}).state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(doc.serializeForSave(), QByteArray("hello world"));

        v.undo();
        QCOMPARE(doc.serializeForSave(), QByteArray("hello"));
        v.redo();
        QCOMPARE(doc.serializeForSave(), QByteArray("hello world"));

        v.setReadOnly(true);
        v.undo();   // must NOT mutate while read-only
        QCOMPARE(doc.serializeForSave(), QByteArray("hello world"));
        v.setReadOnly(false);
        v.undo();
        QCOMPARE(doc.serializeForSave(), QByteArray("hello"));
    }

    void theme_and_fontScale_store_and_signal() {
        MarkdownView v;
        QSignalSpy themeSpy(&v, &MarkdownView::themeChanged);
        QSignalSpy scaleSpy(&v, &MarkdownView::fontScaleChanged);

        Theme t = Theme::defaultDark();
        v.setTheme(t);
        QCOMPARE(themeSpy.count(), 1);

        QCOMPARE(v.fontScale(), 1.0);
        v.setFontScale(1.5);
        QCOMPARE(v.fontScale(), 1.5);
        QCOMPARE(scaleSpy.count(), 1);
        v.setFontScale(1.5);             // no-op → no second signal
        QCOMPARE(scaleSpy.count(), 1);
        v.setFontScale(99.0);            // clamps to 4.0
        QCOMPARE(v.fontScale(), 4.0);
        v.setFontScale(0.0);             // clamps to 0.25
        QCOMPARE(v.fontScale(), 0.25);
    }

    void find_and_format_defaults_are_safe_noops() {
        MarkdownView v;
        auto fixture = openCandidate(QStringLiteral("markdown-view-defaults"));
        MarkoffDocument& doc = *fixture.document;
        v.setDocument(&doc);
        FindController fc(&doc);
        v.attachFindController(&fc);   // qWarning + no-op; must not crash
        v.detachFindController();
        v.toggleBold();
        v.toggleItalic();
        v.toggleStrikethrough();
        v.toggleInlineCode();
        v.insertLink();
        v.setHeadingLevel(2);
        QCOMPARE(doc.serializeForSave(), QByteArray("hello"));
    }

    // Contract-v2 extension (caret-rect, 2026-06-11): the base default is
    // an INVALID rect — "no caret established". Leaves override.
    void caretRect_baseDefault_isInvalid()
    {
        Markoff::MarkdownView v;
        QVERIFY(!v.caretRect().isValid());
    }

    // ASK-M8: a host may delete the document while a view still holds it
    // (ordinary Qt). The base must retire the pointer, tell listeners, and
    // leave every base accessor safe to call.
    void document_destroyed_while_attached_is_retired()
    {
        MarkdownView v;
        auto fixture = openCandidate(QStringLiteral("markdown-view-destroyed"));
        v.setDocument(fixture.document.get());
        QVERIFY(v.document() != nullptr);

        MarkoffDocument *reported = reinterpret_cast<MarkoffDocument *>(1);
        int changed = 0;
        QObject::connect(&v, &MarkdownView::documentChanged, &v,
                         [&](MarkoffDocument *d) { ++changed; reported = d; });

        fixture.document.reset();   // delete the document under the view

        QCOMPARE(v.document(), nullptr);
        QCOMPARE(changed, 1);
        QCOMPARE(reported, nullptr);

        // Every base accessor is safe on the retired view.
        v.undo();
        v.redo();
        v.setReadOnly(true);
        QCOMPARE(v.cursorPosition().line, 1);
        QCOMPARE(v.cursorPosition().column, 1);
        QVERIFY(!v.caretRect().isValid());
        v.setDocument(nullptr);     // detaching again is a no-op, not a crash
        QCOMPARE(v.document(), nullptr);
    }
};

QTEST_MAIN(TstMarkdownViewBase)
#include "tst_markdown_view_base.moc"
