// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>
#include <markoff/core/StructuralOp.h>
#include <markoff/vnext/DocumentStorageProvider.h>

#include "CollabConsumer.h"
#include "InMemoryTransport.h"

#include <memory>
#include <stdexcept>

namespace {
struct Peer {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};

Peer openPeer(const QString& key)
{
    auto provider = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("ephemeral provider creation failed");
    Peer peer{std::move(provider).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*peer.provider,
        {key, 1}, Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("provider-backed document open failed");
    peer.document = std::move(opened).value();
    return peer;
}

QByteArray onlyBlockText(const Markoff::MarkoffDocument& document)
{
    const auto blocks = document.iterateBlocks();
    if (blocks.size() != 1) return {};
    return document.blockText(blocks.front());
}
}

class TstVNextActionConsumer final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void consumerRequeuesPersistedPendingActionsOnStartup()
    {
        auto a = openPeer(QStringLiteral("action-outbox-replay"));
        auto b = openPeer(QStringLiteral("action-outbox-replay"));
        QCOMPARE(a.document->importInitialMarkdown(QByteArrayLiteral("replayed")).state,
            Markoff::VNext::ActionPublicationState::committed);
        const auto beforeSend = a.document->pendingOutboundActions();
        QVERIFY(beforeSend.ok());
        QCOMPARE(beforeSend.value().size(), size_t(1));

        InMemoryTransport transportA(QStringLiteral("A"));
        InMemoryTransport transportB(QStringLiteral("B"));
        transportA.connectPeer(&transportB);
        transportB.connectPeer(&transportA);
        CollabConsumer consumerB(b.document.get(), &transportB, nullptr,
            CollabConsumer::DeliveryMode::actionBundles);
        CollabConsumer consumerA(a.document.get(), &transportA, nullptr,
            CollabConsumer::DeliveryMode::actionBundles);

        QCOMPARE(onlyBlockText(*b.document), QByteArrayLiteral("replayed"));
        const auto afterReceipt = a.document->pendingOutboundActions();
        QVERIFY(afterReceipt.ok());
        QCOMPARE(afterReceipt.value().size(), size_t(0));
    }

    void queuedLocalActionIsDeliveredAndReceiptedAfterReconnect()
    {
        auto a = openPeer(QStringLiteral("action-reconnect"));
        auto b = openPeer(QStringLiteral("action-reconnect"));
        InMemoryTransport transportA(QStringLiteral("A"));
        InMemoryTransport transportB(QStringLiteral("B"));
        CollabConsumer consumerA(a.document.get(), &transportA, nullptr,
            CollabConsumer::DeliveryMode::actionBundles);
        CollabConsumer consumerB(b.document.get(), &transportB, nullptr,
            CollabConsumer::DeliveryMode::actionBundles);

        QCOMPARE(a.document->importInitialMarkdown(QByteArrayLiteral("offline")).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(transportA.pendingActionCount(), qsizetype(1));
        const auto beforeReconnect = a.document->pendingOutboundActions();
        QVERIFY(beforeReconnect.ok());
        QCOMPARE(beforeReconnect.value().size(), size_t(1));
        QCOMPARE(onlyBlockText(*b.document), QByteArray());

        transportA.connectPeer(&transportB);
        transportB.connectPeer(&transportA);

        QCOMPARE(onlyBlockText(*b.document), QByteArrayLiteral("offline"));
        QCOMPARE(transportA.pendingActionCount(), qsizetype(0));
        const auto afterReceipt = a.document->pendingOutboundActions();
        QVERIFY(afterReceipt.ok());
        QCOMPARE(afterReceipt.value().size(), size_t(0));
    }

    void localActionsReachPeerAndReturnReceipts()
    {
        auto a = openPeer(QStringLiteral("action-peer"));
        auto b = openPeer(QStringLiteral("action-peer"));
        InMemoryTransport transportA(QStringLiteral("A"));
        InMemoryTransport transportB(QStringLiteral("B"));
        transportA.connectPeer(&transportB);
        transportB.connectPeer(&transportA);
        CollabConsumer consumerA(a.document.get(), &transportA, nullptr,
            CollabConsumer::DeliveryMode::actionBundles);
        CollabConsumer consumerB(b.document.get(), &transportB, nullptr,
            CollabConsumer::DeliveryMode::actionBundles);
        QSignalSpy receiptsA(&consumerA,
            &CollabConsumer::remoteActionReceiptReceived);
        QSignalSpy receiptsB(&consumerB,
            &CollabConsumer::remoteActionReceiptReceived);
        QVERIFY(receiptsA.isValid());
        QVERIFY(receiptsB.isValid());

        QCOMPARE(a.document->importInitialMarkdown(QByteArrayLiteral("first")).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(onlyBlockText(*b.document), QByteArrayLiteral("first"));
        QCOMPARE(receiptsA.size(), 1);
        QVERIFY(!receiptsA.at(0).at(0).toByteArray().isEmpty());
        QVERIFY(!receiptsA.at(0).at(1).toByteArray().isEmpty());
        QCOMPARE(transportA.pendingActionCount(), qsizetype(0));

        const auto blocksA = a.document->iterateBlocks();
        const auto blocksB = b.document->iterateBlocks();
        QCOMPARE(blocksA.size(), size_t(1));
        QCOMPARE(blocksB.size(), size_t(1));
        QVERIFY(blocksA.front() == blocksB.front());
        QCOMPARE(a.document->applyBlockEdit({blocksA.front(), 5, 0,
            QByteArrayLiteral("!")}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(onlyBlockText(*b.document), QByteArrayLiteral("first!"));
        QCOMPARE(receiptsA.size(), 2);
        QCOMPARE(b.document->applyBlockEdit({blocksB.front(), 0, 0,
            QByteArrayLiteral("B")}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(onlyBlockText(*a.document), QByteArrayLiteral("Bfirst!"));
        QCOMPARE(onlyBlockText(*b.document), QByteArrayLiteral("Bfirst!"));
        QCOMPARE(receiptsB.size(), 1);

        const auto inserted = a.document->applyStructural({
            Markoff::StructuralOp::InsertEntry{blocksA.front(),
                Markoff::BlockKind::Paragraph}});
        QCOMPARE(inserted.state, Markoff::VNext::ActionPublicationState::committed);
        const auto structurallyConvergedA = a.document->iterateBlocks();
        const auto structurallyConvergedB = b.document->iterateBlocks();
        QCOMPARE(structurallyConvergedA.size(), size_t(2));
        QVERIFY(structurallyConvergedA == structurallyConvergedB);
        QCOMPARE(receiptsA.size(), 3);

        QCOMPARE(a.document->replaceMarkdown(QByteArrayLiteral("second")).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(onlyBlockText(*b.document), QByteArrayLiteral("second"));
        QCOMPARE(receiptsA.size(), 4);
        QVERIFY(!receiptsA.at(3).at(0).toByteArray().isEmpty());
        QVERIFY(!receiptsA.at(3).at(1).toByteArray().isEmpty());
        QCOMPARE(transportA.pendingActionCount(), qsizetype(0));

        QCOMPARE(b.document->replaceMarkdown(QByteArrayLiteral("third")).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(onlyBlockText(*a.document), QByteArrayLiteral("third"));
        QCOMPARE(receiptsB.size(), 2);
        QVERIFY(!receiptsB.at(1).at(0).toByteArray().isEmpty());
        QVERIFY(!receiptsB.at(1).at(1).toByteArray().isEmpty());
        QCOMPARE(transportB.pendingActionCount(), qsizetype(0));
    }

    void presenceIsNotADocumentActionAndIsNotReplayed()
    {
        auto a = openPeer(QStringLiteral("presence-sideband"));
        auto b = openPeer(QStringLiteral("presence-sideband"));
        InMemoryTransport transportA(QStringLiteral("A"));
        InMemoryTransport transportB(QStringLiteral("B"));
        transportA.connectPeer(&transportB);
        transportB.connectPeer(&transportA);
        CollabConsumer consumerA(a.document.get(), &transportA);
        CollabConsumer consumerB(b.document.get(), &transportB);
        QSignalSpy actions(a.document.get(),
            &Markoff::MarkoffDocument::localActionEnvelopeCommitted);
        QSignalSpy receipts(&consumerA, &CollabConsumer::remoteActionReceiptReceived);
        QSignalSpy presence(&consumerB, &CollabConsumer::remotePresenceReceived);

        QCOMPARE(a.document->importInitialMarkdown(QByteArrayLiteral("Hello")).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(onlyBlockText(*b.document), QByteArrayLiteral("Hello"));
        QCOMPARE(actions.size(), 1);
        QCOMPARE(receipts.size(), 1);
        const auto block = a.document->iterateBlocks().front();
        const auto anchor = a.document->boundaryTokenAt(block, 1, false);
        const auto active = a.document->boundaryTokenAt(block, 1, true);
        QVERIFY(anchor.ok());
        QVERIFY(active.ok());
        Markoff::Selection caret;
        caret.kind = Markoff::Selection::Kind::Presence;
        caret.participantId = QStringLiteral("replica-a");
        caret.participantLabel = QStringLiteral("Replica A");
        caret.presenceColor = QColor(0x22, 0x88, 0xCC);
        caret.anchorPosition = Markoff::ScopedTextPosition{block, anchor.value()};
        caret.activePosition = Markoff::ScopedTextPosition{block, active.value()};

        QVERIFY(consumerA.publishPresence(caret));
        QCOMPARE(presence.size(), 1);
        const auto delivered = presence.at(0).at(0).value<Markoff::Selection>();
        QCOMPARE(delivered.participantId, QStringLiteral("replica-a"));
        QCOMPARE(delivered.anchorPosition->block, block);
        QCOMPARE(delivered.anchorPosition->boundaryToken, anchor.value());
        QCOMPARE(actions.size(), 1);
        QCOMPARE(receipts.size(), 1);
        QCOMPARE(transportA.pendingActionCount(), qsizetype(0));
        QCOMPARE(onlyBlockText(*a.document), QByteArrayLiteral("Hello"));
        QCOMPARE(onlyBlockText(*b.document), QByteArrayLiteral("Hello"));
        QVERIFY(!a.document->canUndoForBlock(block));

        transportA.disconnectPeer(&transportB);
        QVERIFY(consumerA.publishPresence(caret));
        QCOMPARE(presence.size(), 1);
        transportA.connectPeer(&transportB);
        QCOMPARE(presence.size(), 1);

        Markoff::Selection edit;
        edit.kind = Markoff::Selection::Kind::Primary;
        edit.participantId = caret.participantId;
        edit.anchorPosition = caret.anchorPosition;
        edit.activePosition = caret.activePosition;
        QVERIFY(!consumerA.publishPresence(edit));
        QCOMPARE(presence.size(), 1);
        QCOMPARE(actions.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TstVNextActionConsumer)
#include "tst_vnext_action_consumer.moc"
