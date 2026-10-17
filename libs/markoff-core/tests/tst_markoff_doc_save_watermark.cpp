// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

namespace {
struct CandidateDocument {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};

CandidateDocument openCandidate(const QString& key)
{
    auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) throw std::runtime_error("provider creation failed");
    CandidateDocument fixture{std::move(providerResult).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider,
        {key, 1}, Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(QByteArrayLiteral("hello")).state
        != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}
}

class TestSaveWatermark : public QObject {
    Q_OBJECT
private slots:
    void initial_export_is_revision_bound() {
        auto fixture = openCandidate(QStringLiteral("save-revision"));
        auto snapshot = fixture.document->snapshotForExport();
        QVERIFY(snapshot.ok());
        QCOMPARE(snapshot.value().markdown, QByteArrayLiteral("hello"));
        QVERIFY(fixture.document->acknowledgeExport(snapshot.value()).ok());
    }

    void stale_export_cannot_acknowledge_later_edit() {
        auto fixture = openCandidate(QStringLiteral("save-stale"));
        const auto before = fixture.document->snapshotForExport();
        QVERIFY(before.ok());
        const auto block = fixture.document->iterateBlocks().front();
        QCOMPARE(fixture.document->applyBlockEdit(
            {block, 5, 0, QByteArrayLiteral("!")}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QVERIFY(!fixture.document->acknowledgeExport(before.value()).ok());
        auto current = fixture.document->snapshotForExport();
        QVERIFY(current.ok());
        QCOMPARE(current.value().markdown, QByteArrayLiteral("hello!"));
        QVERIFY(fixture.document->acknowledgeExport(current.value()).ok());
    }
};

QTEST_MAIN(TestSaveWatermark)

#include "tst_markoff_doc_save_watermark.moc"
