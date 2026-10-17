// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/Origin.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <memory>
#include <stdexcept>

namespace {
struct CandidateDocument {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};

CandidateDocument openCandidate(const QByteArray& source)
{
    auto providerResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok()) throw std::runtime_error("provider creation failed");
    CandidateDocument fixture{std::move(providerResult).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider,
        {QStringLiteral("targeted-signals"), 1}, Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(source).state
        != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}
}

class TestTargetedBlockSignals : public QObject {
    Q_OBJECT
private slots:
    void edit_emits_blocksChanged_with_touched_id()
    {
        auto fixture = openCandidate(QByteArrayLiteral("# Hello\n\nWorld\n"));
        auto& doc = *fixture.document;
        QCoreApplication::processEvents(); // flush deferred signals

        const auto blockIds = doc.iterateBlocks();
        QVERIFY(blockIds.size() >= 2);
        const Markoff::BlockId target = blockIds[1];

        QSignalSpy changed(&doc, &Markoff::MarkoffDocument::blocksChanged);
        Markoff::BlockEdit edit;
        edit.blockId = target;
        edit.withinBlockByteOffset = 5;
        edit.removedBytes = 0;
        edit.insertedUtf8 = QByteArrayLiteral("!");
        QCOMPARE(doc.applyBlockEdit(edit).state,
                 Markoff::VNext::ActionPublicationState::committed);

        QVERIFY(changed.count() >= 1);
        const auto ids = changed.first().at(0).value<QList<Markoff::BlockId>>();
        QVERIFY(ids.contains(target));
    }

    void structural_insert_emits_blockInserted()
    {
        auto fixture = openCandidate(QByteArrayLiteral("# A\n"));
        auto& doc = *fixture.document;
        QCoreApplication::processEvents();

        const auto blocks = doc.iterateBlocks();
        QVERIFY(blocks.size() >= 1);
        QSignalSpy inserted(&doc, &Markoff::MarkoffDocument::blockInserted);
        const auto structuralSequence = doc.structuralEditSequence();
        QCOMPARE(doc.applyStructural({Markoff::StructuralOp::InsertEntry{
            blocks[0], Markoff::BlockKind::Paragraph}}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QVERIFY(doc.structuralEditSequence() > structuralSequence);
        QVERIFY(inserted.count() >= 1);
    }

    void structural_remove_emits_blockRemoved()
    {
        auto fixture = openCandidate(QByteArrayLiteral("# A\n\nB\n"));
        auto& doc = *fixture.document;
        QCoreApplication::processEvents();

        const auto blocksBefore = doc.iterateBlocks();
        QVERIFY(blocksBefore.size() >= 2);

        QSignalSpy removed(&doc, &Markoff::MarkoffDocument::blockRemoved);
        const auto structuralSequence = doc.structuralEditSequence();
        QCOMPARE(doc.applyStructural({Markoff::StructuralOp::RemoveEntry{
            blocksBefore[1]}}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QVERIFY(doc.structuralEditSequence() > structuralSequence);
        QVERIFY(removed.count() >= 1);
    }

    void blockInserted_carries_valid_row()
    {
        auto fixture = openCandidate(QByteArrayLiteral("# A\n"));
        auto& doc = *fixture.document;
        QCoreApplication::processEvents();

        const auto blocks = doc.iterateBlocks();
        QSignalSpy inserted(&doc, &Markoff::MarkoffDocument::blockInserted);
        const auto structuralSequence = doc.structuralEditSequence();
        QCOMPARE(doc.applyStructural({Markoff::StructuralOp::InsertEntry{
            blocks[0], Markoff::BlockKind::Paragraph}}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QVERIFY(doc.structuralEditSequence() > structuralSequence);

        QVERIFY(inserted.count() >= 1);
        const int row = inserted.first().at(1).toInt();
        QVERIFY(row >= 0);
        QVERIFY(row < static_cast<int>(doc.iterateBlocks().size()));
    }

    void blockRemoved_carries_former_row()
    {
        auto fixture = openCandidate(QByteArrayLiteral("# A\n\nB\n"));
        auto& doc = *fixture.document;
        QCoreApplication::processEvents();

        const auto blocksBefore = doc.iterateBlocks();
        QVERIFY(blocksBefore.size() >= 2);
        const int expectedRow    = 1; // second block

        QSignalSpy removed(&doc, &Markoff::MarkoffDocument::blockRemoved);
        const auto structuralSequence = doc.structuralEditSequence();
        QCOMPARE(doc.applyStructural({Markoff::StructuralOp::RemoveEntry{
            blocksBefore[1]}}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QVERIFY(doc.structuralEditSequence() > structuralSequence);

        QVERIFY(removed.count() >= 1);
        const int row = removed.first().at(1).toInt();
        QCOMPARE(row, expectedRow);
    }
};

QTEST_GUILESS_MAIN(TestTargetedBlockSignals)
#include "tst_v10_targeted_block_signals.moc"
