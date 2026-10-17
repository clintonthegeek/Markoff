// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkdownView.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

namespace {
class StubView : public Markoff::MarkdownView {
public:
    using Markoff::MarkdownView::MarkdownView;
};
}

class TestMarkdownViewBase : public QObject {
    Q_OBJECT
private slots:
    void defaults_are_safe() {
        StubView v;
        QCOMPARE(v.document(), nullptr);
        QCOMPARE(v.cursorPosition().line, 1);
        QCOMPARE(v.cursorPosition().column, 1);
        QCOMPARE(v.scrollPositionVisualLine(), 0.0f);
        QCOMPARE(v.isReadOnly(), false);
        QCOMPARE(v.hasCursor(), false);
        QCOMPARE(v.hasEditing(), false);
    }
    void setReadOnly_round_trips() {
        StubView v;
        v.setReadOnly(true);
        QCOMPARE(v.isReadOnly(), true);
        v.setReadOnly(false);
        QCOMPARE(v.isReadOnly(), false);
    }
    void setDocument_emits_documentChanged() {
        StubView v;
        auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
        QVERIFY(providerResult.ok());
        auto provider = std::move(providerResult).value();
        auto opened = Markoff::MarkoffDocument::open(*provider,
            {QStringLiteral("markdown-view-base"), 1},
            Markoff::VNext::StorageMode::ephemeral);
        QVERIFY(opened.ok());
        auto doc = std::move(opened).value();
        QSignalSpy spy(&v, &Markoff::MarkdownView::documentChanged);
        v.setDocument(doc.get());
        QCOMPARE(v.document(), doc.get());
        QCOMPARE(spy.count(), 1);
    }
};
QTEST_MAIN(TestMarkdownViewBase)
#include "tst_v10_markdown_view_base.moc"
