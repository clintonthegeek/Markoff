// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/StructuralOp.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

namespace {
struct CandidateFixture {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};

CandidateFixture openCandidate(const QString& key, const QByteArray& source)
{
    auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) throw std::runtime_error("provider creation failed");
    CandidateFixture fixture{std::move(providerResult).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider, {key, 1},
        Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(source).state
        != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}
}

using namespace Markoff;

class TstD2SaveHistory : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void save_keeps_text_undoable()
    {
        auto fixture = openCandidate(QStringLiteral("save-history-text"),
                                     QByteArrayLiteral("hello\n"));
        auto& doc = *fixture.document;
        const auto blocks = doc.iterateBlocks();
        QCOMPARE(blocks.size(), size_t(1));
        const QByteArray original = doc.blockText(blocks.front());
        QCOMPARE(doc.applyBlockEdit(BlockEdit{blocks.front(), 5, 0, " world"}).state,
                 VNext::ActionPublicationState::committed);
        const auto snapshot = doc.snapshotForExport();
        QVERIFY(snapshot.ok());
        QCOMPARE(snapshot.value().markdown, QByteArrayLiteral("hello world\n"));
        QVERIFY(doc.acknowledgeExport(snapshot.value()).ok());

        doc.undoD2();
        QCOMPARE(doc.blockText(blocks.front()), original);
    }

    void save_keeps_removed_block_recoverable()
    {
        auto fixture = openCandidate(QStringLiteral("save-history-removed"),
                                     QByteArrayLiteral("first\n\nsecond\n"));
        auto& doc = *fixture.document;
        const auto blocks = doc.iterateBlocks();
        QVERIFY(blocks.size() >= 2);
        const BlockId removed = blocks[1];
        const QByteArray removedText = doc.blockText(removed);
        QVERIFY(!removedText.isEmpty());

        QCOMPARE(doc.applyStructural(StructuralOp{StructuralOp::RemoveEntry{removed}}).state,
                 VNext::ActionPublicationState::committed);
        auto snapshot = doc.snapshotForExport();
        QVERIFY(snapshot.ok());
        QVERIFY(doc.acknowledgeExport(snapshot.value()).ok());

        doc.undoD2();
        const auto restoredBlocks = doc.iterateBlocks();
        QVERIFY(std::find(restoredBlocks.begin(), restoredBlocks.end(), removed)
                != restoredBlocks.end());
        QCOMPARE(doc.blockText(removed), removedText);
    }
};

QTEST_GUILESS_MAIN(TstD2SaveHistory)
#include "tst_d2_save_history.moc"
