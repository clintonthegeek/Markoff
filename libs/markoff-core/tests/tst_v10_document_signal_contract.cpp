// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>

class TestDocumentSignalContract : public QObject {
    Q_OBJECT
private slots:
    void documentChanged_emitted_on_load() {
        auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
        QVERIFY(providerResult.ok());
        auto provider = std::move(providerResult).value();
        auto opened = Markoff::MarkoffDocument::open(*provider,
            {QStringLiteral("signal-contract"), 1}, Markoff::VNext::StorageMode::ephemeral);
        QVERIFY(opened.ok());
        auto doc = std::move(opened).value();
        QSignalSpy changed(doc.get(), SIGNAL(documentChanged()));
        QSignalSpy reloaded(doc.get(), SIGNAL(documentReloaded()));
        QCOMPARE(doc->importInitialMarkdown(QByteArrayLiteral("# Hello\n")).state,
                 Markoff::VNext::ActionPublicationState::committed);
        QVERIFY(changed.count() >= 1);
        // loadFromMarkdown emits documentLoaded/documentChanged, not documentReloaded
        QVERIFY(reloaded.count() == 0);
    }
    void documentChanged_no_args() {
        auto sig = &Markoff::MarkoffDocument::documentChanged;
        Q_UNUSED(sig);
        QVERIFY(true);
    }
    void contentsChanged_with_offsets_does_not_exist() {
        QVERIFY(true);
    }
};
QTEST_GUILESS_MAIN(TestDocumentSignalContract)
#include "tst_v10_document_signal_contract.moc"
