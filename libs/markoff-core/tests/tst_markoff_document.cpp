// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QApplication>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/SessionParams.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/AttrNames.h>

using namespace Markoff;


#include <markoff/vnext/DocumentStorageProvider.h>
#include <markoff/vnext/ActionBundleCodec.h>

#include <memory>
#include <stdexcept>

namespace {
struct CandidateDocument {
    std::unique_ptr<VNext::DocumentStorageProvider> provider;
    std::unique_ptr<MarkoffDocument> document;
};

CandidateDocument openCandidate(const QString &key)
{
    auto providerResult = VNext::makeEphemeralDocumentStorageProvider();
    if (!providerResult.ok())
        throw std::runtime_error("could not create ephemeral document provider");
    CandidateDocument result{std::move(providerResult).value(), {}};
    auto documentResult = MarkoffDocument::open(*result.provider,
        {key, 1}, VNext::StorageMode::ephemeral);
    if (!documentResult.ok())
        throw std::runtime_error("could not open provider-backed document");
    result.document = std::move(documentResult).value();
    return result;
}
}

class TstMarkoffDocument : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void opens_empty_provider_backed_document()
    {
        auto fixture = openCandidate(QStringLiteral("empty"));
        QCOMPARE(fixture.document->importInitialMarkdown({}).state,
            VNext::ActionPublicationState::committed);
        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), 1);
        QVERIFY(fixture.document->blockText(blocks.front()).isEmpty());
    }

    void frontmatter_edit_is_undoable_byte_for_byte()
    {
        auto fixture = openCandidate(QStringLiteral("frontmatter-undo"));
        QCOMPARE(fixture.document->importInitialMarkdown(
            QByteArrayLiteral("---\ntitle: Old\ncount: 1\n---\nBody\n")).state,
            VNext::ActionPublicationState::committed);

        const QByteArray before = fixture.document->serializeForSave();
        QVERIFY(before.contains("title: Old"));

        QCOMPARE(fixture.document->setFrontmatterValue(
            QByteArrayLiteral("owner"), QByteArrayLiteral("clinton")).state,
            VNext::ActionPublicationState::committed);
        QCOMPARE(fixture.document->frontmatterValue(QByteArrayLiteral("owner")).value(),
            QByteArrayLiteral("clinton"));
        const QByteArray afterEdit = fixture.document->serializeForSave();
        QVERIFY(afterEdit != before);
        QVERIFY(afterEdit.contains("owner: clinton"));

        // One undo reverses the whole edit, frontmatter byte-for-byte.
        fixture.document->undoD2();
        QVERIFY(!fixture.document->frontmatterValue(QByteArrayLiteral("owner")).has_value());
        QCOMPARE(fixture.document->serializeForSave(), before);

        // And it is redoable.
        fixture.document->redoD2();
        QCOMPARE(fixture.document->serializeForSave(), afterEdit);
    }

    void imports_edits_and_undoes_accepted_actions()
    {
        auto fixture = openCandidate(QStringLiteral("edit-undo"));
        QCOMPARE(fixture.document->importInitialMarkdown(
            QByteArrayLiteral("hello\n")).state,
            VNext::ActionPublicationState::committed);
        auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(fixture.document->blockText(blocks.front()), QByteArrayLiteral("hello"));

        QCOMPARE(fixture.document->applyBlockEdit(
            {blocks.front(), 5, 0, QByteArrayLiteral("!")}).state,
            VNext::ActionPublicationState::committed);
        QCOMPARE(fixture.document->blockText(blocks.front()), QByteArrayLiteral("hello!"));
        fixture.document->undoD2();
        QCOMPARE(fixture.document->blockText(blocks.front()), QByteArrayLiteral("hello"));
        fixture.document->redoD2();
        QCOMPARE(fixture.document->blockText(blocks.front()), QByteArrayLiteral("hello!"));
    }

    void creates_and_destroys_view_sessions()
    {
        auto fixture = openCandidate(QStringLiteral("sessions"));
        SessionParams params;
        params.participantId = QStringLiteral("alice");
        Session *session = fixture.document->createSession(params);
        QVERIFY(session);
        QCOMPARE(fixture.document->sessionForParticipant(QStringLiteral("alice")), session);
        fixture.document->destroySession(session);
        QVERIFY(fixture.document->sessions().isEmpty());
    }

    void reconstructs_list_and_quote_display_markers()
    {
        auto fixture = openCandidate(QStringLiteral("display-markers"));
        QCOMPARE(fixture.document->importInitialMarkdown(
            QByteArrayLiteral("- item\n\n> quoted\n")).state,
            VNext::ActionPublicationState::committed);
        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{2});
        QCOMPARE(fixture.document->blockKind(blocks[0]), BlockKind::ListItem);
        QCOMPARE(fixture.document->listItemDisplayMarker(blocks[0]),
                 QByteArrayLiteral("- "));
        QCOMPARE(fixture.document->blockKind(blocks[1]), BlockKind::BlockQuote);
        QCOMPARE(fixture.document->blockQuoteDisplayMarker(blocks[1]),
                 QByteArrayLiteral("> "));
    }

    void command_kind_change_uses_durable_structural_action()
    {
        auto fixture = openCandidate(QStringLiteral("command-kind-change"));
        QCOMPARE(fixture.document->importInitialMarkdown(
            QByteArrayLiteral("plain text\n")).state,
            VNext::ActionPublicationState::committed);
        const auto block = fixture.document->iterateBlocks().front();
        QCOMPARE(Cmd::changeKind(*fixture.document, block, BlockKind::Heading).state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(fixture.document->blockKind(block), BlockKind::Heading);
        fixture.document->undoD2();
        QCOMPARE(fixture.document->blockKind(block), BlockKind::Paragraph);
        fixture.document->redoD2();
        QCOMPARE(fixture.document->blockKind(block), BlockKind::Heading);
    }

    void command_kind_and_attributes_undo_as_one_action()
    {
        const QString key = QStringLiteral("command-kind-attributes");
        auto fixture = openCandidate(key);
        QCOMPARE(fixture.document->importInitialMarkdown(
            QByteArrayLiteral("plain text\n")).state,
            VNext::ActionPublicationState::committed);
        const auto block = fixture.document->iterateBlocks().front();
        const QList<QByteArray> names{AttrNames::Level, AttrNames::HeadingForm};
        const QList<AttrValue> values{AttrValue{2}, AttrValue{QStringLiteral("atx")}};
        QCOMPARE(Cmd::changeKind(*fixture.document, block, BlockKind::Heading,
                                 names, values).state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(fixture.document->blockKind(block), BlockKind::Heading);
        QCOMPARE(std::get<int>(fixture.document->blockAttrs(block).value(AttrNames::Level)), 2);
        QCOMPARE(std::get<QString>(fixture.document->blockAttrs(block).value(
            AttrNames::HeadingForm)), QStringLiteral("atx"));

        fixture.document.reset();
        auto reopened = MarkoffDocument::open(*fixture.provider,
            {key, 1}, VNext::StorageMode::ephemeral);
        QVERIFY(reopened.ok());
        fixture.document = std::move(reopened).value();
        QCOMPARE(fixture.document->blockKind(block), BlockKind::Heading);
        QCOMPARE(std::get<int>(fixture.document->blockAttrs(block).value(AttrNames::Level)), 2);
        QCOMPARE(std::get<QString>(fixture.document->blockAttrs(block).value(
            AttrNames::HeadingForm)), QStringLiteral("atx"));
        QVERIFY(fixture.document->canUndoForBlock(block));
        fixture.document->undoD2();
        QCOMPARE(fixture.document->blockKind(block), BlockKind::Paragraph);
        QVERIFY(!fixture.document->blockAttrs(block).contains(AttrNames::Level));
        QVERIFY(!fixture.document->blockAttrs(block).contains(AttrNames::HeadingForm));
        fixture.document->redoD2();
        QCOMPARE(fixture.document->blockKind(block), BlockKind::Heading);
        QCOMPARE(std::get<int>(fixture.document->blockAttrs(block).value(AttrNames::Level)), 2);
        QCOMPARE(std::get<QString>(fixture.document->blockAttrs(block).value(
            AttrNames::HeadingForm)), QStringLiteral("atx"));
    }

    void swaps_block_payload_as_one_undoable_action()
    {
        const QString key = QStringLiteral("swap-block-payload");
        auto fixture = openCandidate(key);
        QCOMPARE(fixture.document->importInitialMarkdown(
            QByteArrayLiteral("## heading\n\nplain\n")).state,
            VNext::ActionPublicationState::committed);
        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{2});
        QHash<AttrName, AttrValue> defaults{
            {AttrNames::Level, 1}, {AttrNames::HeadingForm, QStringLiteral("atx")},
            {AttrNames::InfoString, QString()}, {AttrNames::MarkerStyle, QStringLiteral("-")},
            {AttrNames::MarkerNumber, 1}, {AttrNames::IndentLevel, 0},
            {AttrNames::Checked, false}, {AttrNames::LooseRun, false},
            {AttrNames::Src, QString()}, {AttrNames::Alt, QString()},
            {AttrNames::Title, QString()}, {AttrNames::DisplayMode, false},
            {AttrNames::BlockQuoteDepth, 1}, {AttrNames::BlockQuoteRunId, 1},
        };
        QCOMPARE(fixture.document->swapBlockPayload(blocks[0], blocks[1], defaults).state,
                 VNext::ActionPublicationState::committed);
        QCOMPARE(fixture.document->blockText(blocks[0]), QByteArrayLiteral("plain"));
        QCOMPARE(fixture.document->blockText(blocks[1]), QByteArrayLiteral("## heading"));
        QCOMPARE(fixture.document->blockKind(blocks[0]), BlockKind::Paragraph);
        QCOMPARE(fixture.document->blockKind(blocks[1]), BlockKind::Heading);
        QCOMPARE(std::get<int>(fixture.document->blockAttrs(blocks[1]).value(AttrNames::Level)), 2);
        QCOMPARE(std::get<int>(fixture.document->blockAttrs(blocks[0]).value(AttrNames::Level)), 1);

        fixture.document.reset();
        auto reopened = MarkoffDocument::open(*fixture.provider,
            {key, 1}, VNext::StorageMode::ephemeral);
        QVERIFY(reopened.ok());
        fixture.document = std::move(reopened).value();
        fixture.document->undoD2();
        QCOMPARE(fixture.document->blockText(blocks[0]), QByteArrayLiteral("## heading"));
        QCOMPARE(fixture.document->blockText(blocks[1]), QByteArrayLiteral("plain"));
        QCOMPARE(fixture.document->blockKind(blocks[0]), BlockKind::Heading);
        QVERIFY(fixture.document->blockAttrs(blocks[1]).isEmpty());
        fixture.document->redoD2();
        QCOMPARE(fixture.document->blockKind(blocks[1]), BlockKind::Heading);
    }

    void smart_paste_inserts_typed_blocks_as_one_action()
    {
        const QString key = QStringLiteral("smart-paste-action");
        auto fixture = openCandidate(key);
        QCOMPARE(fixture.document->importInitialMarkdown(
            QByteArrayLiteral("leftRIGHT\n")).state,
            VNext::ActionPublicationState::committed);
        const auto target = fixture.document->iterateBlocks().front();
        const auto pasted = Cmd::pasteMarkdown(*fixture.document, target, 4,
            QByteArrayLiteral("one\n\n- two"));
        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{3});
        QCOMPARE(fixture.document->blockText(blocks[0]), QByteArrayLiteral("left"));
        QCOMPARE(fixture.document->blockText(blocks[1]), QByteArrayLiteral("one"));
        QCOMPARE(fixture.document->blockKind(blocks[2]), BlockKind::ListItem);
        QCOMPARE(fixture.document->listItemDisplayMarker(blocks[2]), QByteArrayLiteral("- "));
        QCOMPARE(fixture.document->blockText(blocks[2]), QByteArrayLiteral("twoRIGHT"));
        QCOMPARE(pasted.caretBlock, blocks[2]);
        QCOMPARE(pasted.caretByteOffset, uint32_t{3});

        fixture.document.reset();
        auto reopened = MarkoffDocument::open(*fixture.provider,
            {key, 1}, VNext::StorageMode::ephemeral);
        QVERIFY(reopened.ok());
        fixture.document = std::move(reopened).value();
        fixture.document->undoD2();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t{1});
        QCOMPARE(fixture.document->blockText(target), QByteArrayLiteral("leftRIGHT"));
        fixture.document->redoD2();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t{3});
        QCOMPARE(fixture.document->blockText(blocks[2]), QByteArrayLiteral("twoRIGHT"));
    }

    void referenceProviderReopensDocumentIdentityAndHistory()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto root = std::filesystem::path(directory.path().toStdString());
        const QString key = QStringLiteral("durable-document-reopen");
        std::vector<BlockId> originalBlocks;

        {
            auto provider = VNext::makeReferenceDocumentStorageProvider(root);
            QVERIFY(provider.ok());
            auto opened = MarkoffDocument::open(*provider.value(), {key, 1},
                VNext::StorageMode::durable);
            QVERIFY(opened.ok());
            auto document = std::move(opened).value();
            QCOMPARE(document->importInitialMarkdown(
                QByteArrayLiteral("alpha\n\nbeta")).state,
                VNext::ActionPublicationState::committed);
            originalBlocks = document->iterateBlocks();
            QCOMPARE(originalBlocks.size(), size_t{2});
            QCOMPARE(document->applyBlockEdit(
                {originalBlocks[0], 0, 0, QByteArrayLiteral("A")}).state,
                VNext::ActionPublicationState::committed);
            QCOMPARE(document->applyBlockEdit(
                {originalBlocks[1], 4, 0, QByteArrayLiteral("B")}).state,
                VNext::ActionPublicationState::committed);
            QCOMPARE(document->blockText(originalBlocks[0]), QByteArrayLiteral("Aalpha"));
            QCOMPARE(document->blockText(originalBlocks[1]), QByteArrayLiteral("betaB"));
        }

        {
            auto provider = VNext::makeReferenceDocumentStorageProvider(root);
            QVERIFY(provider.ok());
            auto opened = MarkoffDocument::open(*provider.value(), {key, 1},
                VNext::StorageMode::durable);
            QVERIFY(opened.ok());
            auto document = std::move(opened).value();
            const auto reopenedBlocks = document->iterateBlocks();
            QCOMPARE(reopenedBlocks, originalBlocks);
            QCOMPARE(document->blockText(reopenedBlocks[0]), QByteArrayLiteral("Aalpha"));
            QCOMPARE(document->blockText(reopenedBlocks[1]), QByteArrayLiteral("betaB"));
            document->undoD2();
            QCOMPARE(document->blockText(reopenedBlocks[0]), QByteArrayLiteral("Aalpha"));
            QCOMPARE(document->blockText(reopenedBlocks[1]), QByteArrayLiteral("beta"));
            document->redoD2();
            QCOMPARE(document->blockText(reopenedBlocks[1]), QByteArrayLiteral("betaB"));
        }
    }

    void durableReplicasConvergeAcrossTextStreamsAndReopen()
    {
        QTemporaryDir directoryA;
        QTemporaryDir directoryB;
        QVERIFY(directoryA.isValid());
        QVERIFY(directoryB.isValid());
        const auto rootA = std::filesystem::path(directoryA.path().toStdString());
        const auto rootB = std::filesystem::path(directoryB.path().toStdString());
        const VNext::DocumentStorageScope scope{
            QStringLiteral("durable-cross-stream-convergence"), 1};
        std::vector<BlockId> originalBlocks;
        std::unique_ptr<VNext::DocumentStorageProvider> providerA;
        std::unique_ptr<VNext::DocumentStorageProvider> providerB;
        std::unique_ptr<MarkoffDocument> documentA;
        std::unique_ptr<MarkoffDocument> documentB;

        auto firstProvider = VNext::makeReferenceDocumentStorageProvider(rootA);
        auto secondProvider = VNext::makeReferenceDocumentStorageProvider(rootB);
        QVERIFY(firstProvider.ok());
        QVERIFY(secondProvider.ok());
        providerA = std::move(firstProvider).value();
        providerB = std::move(secondProvider).value();
        auto firstOpen = MarkoffDocument::open(*providerA, scope,
            VNext::StorageMode::durable);
        auto secondOpen = MarkoffDocument::open(*providerB, scope,
            VNext::StorageMode::durable);
        QVERIFY(firstOpen.ok());
        QVERIFY(secondOpen.ok());
        documentA = std::move(firstOpen).value();
        documentB = std::move(secondOpen).value();

        QSignalSpy initialAction(documentA.get(),
            &MarkoffDocument::localActionEnvelopeCommitted);
        QCOMPARE(documentA->importInitialMarkdown(
            QByteArrayLiteral("alpha\n\nbeta")).state,
            VNext::ActionPublicationState::committed);
        QCOMPARE(initialAction.size(), 1);
        const auto initialRecord = initialAction.takeFirst();
        const auto initialRoute = documentB->routeRemoteAction(
            initialRecord.at(1).toByteArray());
        QCOMPARE(initialRoute.state, RemoteActionRouteState::committed);
        originalBlocks = documentA->iterateBlocks();
        QCOMPARE(originalBlocks, documentB->iterateBlocks());
        QCOMPARE(originalBlocks.size(), size_t{2});

        QSignalSpy actionA(documentA.get(),
            &MarkoffDocument::localActionEnvelopeCommitted);
        QSignalSpy actionB(documentB.get(),
            &MarkoffDocument::localActionEnvelopeCommitted);
        QCOMPARE(documentA->applyBlockEdit({originalBlocks[0], 5, 0,
            QByteArrayLiteral("!")}).state,
            VNext::ActionPublicationState::committed);
        QCOMPARE(documentB->applyBlockEdit({originalBlocks[1], 0, 0,
            QByteArrayLiteral("B")}).state,
            VNext::ActionPublicationState::committed);
        QCOMPARE(actionA.size(), 1);
        QCOMPARE(actionB.size(), 1);
        const auto mabA = actionA.takeFirst().at(1).toByteArray();
        const auto mabB = actionB.takeFirst().at(1).toByteArray();

        QCOMPARE(documentA->routeRemoteAction(mabB).state,
            RemoteActionRouteState::committed);
        QCOMPARE(documentB->routeRemoteAction(mabA).state,
            RemoteActionRouteState::committed);
        QCOMPARE(documentA->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QCOMPARE(documentA->blockText(originalBlocks[1]), QByteArrayLiteral("Bbeta"));
        QCOMPARE(documentB->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QCOMPARE(documentB->blockText(originalBlocks[1]), QByteArrayLiteral("Bbeta"));

        documentA.reset();
        documentB.reset();
        providerA.reset();
        providerB.reset();

        firstProvider = VNext::makeReferenceDocumentStorageProvider(rootA);
        secondProvider = VNext::makeReferenceDocumentStorageProvider(rootB);
        QVERIFY(firstProvider.ok());
        QVERIFY(secondProvider.ok());
        providerA = std::move(firstProvider).value();
        providerB = std::move(secondProvider).value();
        firstOpen = MarkoffDocument::open(*providerA, scope,
            VNext::StorageMode::durable);
        secondOpen = MarkoffDocument::open(*providerB, scope,
            VNext::StorageMode::durable);
        QVERIFY(firstOpen.ok());
        QVERIFY(secondOpen.ok());
        documentA = std::move(firstOpen).value();
        documentB = std::move(secondOpen).value();
        QCOMPARE(documentA->iterateBlocks(), originalBlocks);
        QCOMPARE(documentB->iterateBlocks(), originalBlocks);
        for (const auto* document : {documentA.get(), documentB.get()}) {
            QCOMPARE(document->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
            QCOMPARE(document->blockText(originalBlocks[1]), QByteArrayLiteral("Bbeta"));
        }

        documentA->undoD2();
        documentB->undoD2();
        QCOMPARE(documentA->blockText(originalBlocks[0]), QByteArrayLiteral("alpha"));
        QCOMPARE(documentA->blockText(originalBlocks[1]), QByteArrayLiteral("Bbeta"));
        QCOMPARE(documentB->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QCOMPARE(documentB->blockText(originalBlocks[1]), QByteArrayLiteral("beta"));
        documentA->redoD2();
        documentB->redoD2();
        QCOMPARE(documentA->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QCOMPARE(documentA->blockText(originalBlocks[1]), QByteArrayLiteral("Bbeta"));
        QCOMPARE(documentB->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QCOMPARE(documentB->blockText(originalBlocks[1]), QByteArrayLiteral("Bbeta"));
    }

    void deferred_remote_action_backpressure_preserves_retry()
    {
        const QString key = QStringLiteral("deferred-backpressure");
        auto providerA = VNext::makeEphemeralDocumentStorageProvider();
        auto providerB = VNext::makeEphemeralDocumentStorageProvider();
        QVERIFY(providerA.ok());
        QVERIFY(providerB.ok());
        auto openedA = MarkoffDocument::open(*providerA.value(), {key, 1},
            VNext::StorageMode::ephemeral);
        auto openedB = MarkoffDocument::open(*providerB.value(), {key, 1},
            VNext::StorageMode::ephemeral);
        QVERIFY(openedA.ok());
        QVERIFY(openedB.ok());
        auto a = std::move(openedA).value();
        auto b = std::move(openedB).value();

        QSignalSpy aActions(a.get(), &MarkoffDocument::localActionEnvelopeCommitted);
        QCOMPARE(a->importInitialMarkdown(QByteArrayLiteral("base")).state,
            VNext::ActionPublicationState::committed);
        QVERIFY(b->routeRemoteAction(aActions.takeFirst().at(1).toByteArray()).state
            == RemoteActionRouteState::committed);
        const auto block = a->iterateBlocks().front();

        QCOMPARE(a->applyBlockEdit({block, 0, 0, QByteArrayLiteral("first")}).state,
            VNext::ActionPublicationState::committed);
        const QByteArray predecessor = aActions.takeFirst().at(1).toByteArray();
        QCOMPARE(a->applyBlockEdit({block, 9, 0, QByteArrayLiteral("second")}).state,
            VNext::ActionPublicationState::committed);
        const QByteArray dependent = aActions.takeFirst().at(1).toByteArray();

        QVERIFY(b->setRemoteActionQueueBudget({0, 0}).ok());
        const auto full = b->routeRemoteAction(dependent);
        QCOMPARE(full.state, RemoteActionRouteState::backpressured);
        QCOMPARE(b->pendingRemoteActionCount(), size_t{0});
        QVERIFY(full.diagnostic.has_value());
        QCOMPARE(full.diagnostic->code(), CollabText::ErrorCode::deferred_queue_full);

        QVERIFY(b->setRemoteActionQueueBudget({2,
            static_cast<size_t>(dependent.size() + predecessor.size())}).ok());
        const auto held = b->routeRemoteAction(dependent);
        QCOMPARE(held.state, RemoteActionRouteState::deferred);
        QCOMPARE(b->pendingRemoteActionCount(), size_t{1});
        const auto completed = b->routeRemoteAction(predecessor);
        QCOMPARE(completed.state, RemoteActionRouteState::committed);
        QCOMPARE(completed.committed.size(), size_t{2});
        QCOMPARE(b->pendingRemoteActionCount(), size_t{0});
        QCOMPARE(b->blockText(block), QByteArrayLiteral("firstbasesecond"));
    }

    void durableMixedPrimitiveActionsConvergeAcrossReopen()
    {
        QTemporaryDir directoryA;
        QTemporaryDir directoryB;
        QVERIFY(directoryA.isValid());
        QVERIFY(directoryB.isValid());
        const auto rootA = std::filesystem::path(directoryA.path().toStdString());
        const auto rootB = std::filesystem::path(directoryB.path().toStdString());
        const VNext::DocumentStorageScope scope{
            QStringLiteral("durable-mixed-primitive-convergence"), 1};
        std::vector<BlockId> originalBlocks;
        std::unique_ptr<VNext::DocumentStorageProvider> providerA;
        std::unique_ptr<VNext::DocumentStorageProvider> providerB;
        std::unique_ptr<MarkoffDocument> documentA;
        std::unique_ptr<MarkoffDocument> documentB;

        auto firstProvider = VNext::makeReferenceDocumentStorageProvider(rootA);
        auto secondProvider = VNext::makeReferenceDocumentStorageProvider(rootB);
        QVERIFY(firstProvider.ok());
        QVERIFY(secondProvider.ok());
        providerA = std::move(firstProvider).value();
        providerB = std::move(secondProvider).value();
        auto firstOpen = MarkoffDocument::open(*providerA, scope,
            VNext::StorageMode::durable);
        auto secondOpen = MarkoffDocument::open(*providerB, scope,
            VNext::StorageMode::durable);
        QVERIFY(firstOpen.ok());
        QVERIFY(secondOpen.ok());
        documentA = std::move(firstOpen).value();
        documentB = std::move(secondOpen).value();

        QSignalSpy initialAction(documentA.get(),
            &MarkoffDocument::localActionEnvelopeCommitted);
        QCOMPARE(documentA->importInitialMarkdown(
            QByteArrayLiteral("alpha\n\nbeta\n\ngamma")).state,
            VNext::ActionPublicationState::committed);
        QCOMPARE(initialAction.size(), 1);
        QCOMPARE(documentB->routeRemoteAction(
            initialAction.takeFirst().at(1).toByteArray()).state,
            RemoteActionRouteState::committed);
        originalBlocks = documentA->iterateBlocks();
        QCOMPARE(originalBlocks.size(), size_t{3});
        QCOMPARE(documentB->iterateBlocks(), originalBlocks);

        QSignalSpy actionA(documentA.get(),
            &MarkoffDocument::localActionEnvelopeCommitted);
        QSignalSpy actionB(documentB.get(),
            &MarkoffDocument::localActionEnvelopeCommitted);
        QCOMPARE(documentA->applyBlockMerge(
            {originalBlocks[0], 5, 0, QByteArrayLiteral("!")},
            {originalBlocks[1]}).state,
            VNext::ActionPublicationState::committed);
        QCOMPARE(documentB->setBlockAttribute(originalBlocks[2], AttrNames::Title,
            AttrValue{QStringLiteral("peer-title")}).state,
            VNext::ActionPublicationState::committed);
        QCOMPARE(actionA.size(), 1);
        QCOMPARE(actionB.size(), 1);
        const auto actionMabA = actionA.takeFirst().at(1).toByteArray();
        const auto actionMabB = actionB.takeFirst().at(1).toByteArray();
        const auto decodedMerge = VNext::ActionBundleCodec::decode(
            actionMabA, scope.documentKey.toUtf8().toStdString(), scope.epoch);
        QVERIFY(decodedMerge.ok());
        bool hasText = false;
        bool hasList = false;
        bool hasMap = false;
        for (const auto& operation : decodedMerge.value().operations) {
            hasText |= operation.primitive == VNext::OperationPrimitive::text;
            hasList |= operation.primitive == VNext::OperationPrimitive::idList;
            hasMap |= operation.primitive == VNext::OperationPrimitive::markoffMap;
        }
        QVERIFY(hasText);
        QVERIFY(hasList);
        QVERIFY(hasMap);

        QCOMPARE(documentA->iterateBlocks().size(), size_t{2});
        QCOMPARE(documentA->routeRemoteAction(actionMabB).state,
            RemoteActionRouteState::committed);
        QCOMPARE(std::get<QString>(documentA->blockAttrs(originalBlocks[2]).value(
            AttrNames::Title)), QStringLiteral("peer-title"));
        QCOMPARE(documentB->iterateBlocks(), originalBlocks);
        QCOMPARE(documentB->blockText(originalBlocks[0]), QByteArrayLiteral("alpha"));
        QCOMPARE(std::get<QString>(documentA->blockAttrs(originalBlocks[2]).value(
            AttrNames::Title)), QStringLiteral("peer-title"));

        // Reopen after A has durably accepted B's action but before B receives A's.
        documentA.reset();
        documentB.reset();
        providerA.reset();
        providerB.reset();
        firstProvider = VNext::makeReferenceDocumentStorageProvider(rootA);
        secondProvider = VNext::makeReferenceDocumentStorageProvider(rootB);
        QVERIFY(firstProvider.ok());
        QVERIFY(secondProvider.ok());
        providerA = std::move(firstProvider).value();
        providerB = std::move(secondProvider).value();
        firstOpen = MarkoffDocument::open(*providerA, scope,
            VNext::StorageMode::durable);
        secondOpen = MarkoffDocument::open(*providerB, scope,
            VNext::StorageMode::durable);
        QVERIFY(firstOpen.ok());
        QVERIFY(secondOpen.ok());
        documentA = std::move(firstOpen).value();
        documentB = std::move(secondOpen).value();

        const std::vector<BlockId> mergedBlocks{originalBlocks[0], originalBlocks[2]};
        QCOMPARE(documentA->iterateBlocks(), mergedBlocks);
        QCOMPARE(documentB->iterateBlocks(), originalBlocks);
        QCOMPARE(documentA->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QCOMPARE(documentB->blockText(originalBlocks[0]), QByteArrayLiteral("alpha"));
        QCOMPARE(std::get<QString>(documentA->blockAttrs(originalBlocks[2]).value(
            AttrNames::Title)), QStringLiteral("peer-title"));
        QCOMPARE(std::get<QString>(documentB->blockAttrs(originalBlocks[2]).value(
            AttrNames::Title)), QStringLiteral("peer-title"));

        QCOMPARE(documentB->routeRemoteAction(actionMabA).state,
            RemoteActionRouteState::committed);
        // Retrying B's exact MAB after its accepted-prefix reopen remains idempotent.
        QCOMPARE(documentA->routeRemoteAction(actionMabB).state,
            RemoteActionRouteState::committed);
        QCOMPARE(documentA->iterateBlocks(), mergedBlocks);
        QCOMPARE(documentB->iterateBlocks(), mergedBlocks);
        QCOMPARE(documentA->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QCOMPARE(documentB->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));

        documentA->undoD2();
        documentB->undoD2();
        QCOMPARE(documentA->iterateBlocks(), originalBlocks);
        QCOMPARE(documentA->blockText(originalBlocks[0]), QByteArrayLiteral("alpha"));
        QCOMPARE(documentB->iterateBlocks(), mergedBlocks);
        QCOMPARE(documentB->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QVERIFY(!documentB->blockAttrs(originalBlocks[2]).contains(AttrNames::Title));
        QCOMPARE(std::get<QString>(documentA->blockAttrs(originalBlocks[2]).value(
            AttrNames::Title)), QStringLiteral("peer-title"));

        documentA->redoD2();
        documentB->redoD2();
        QCOMPARE(documentA->iterateBlocks(), mergedBlocks);
        QCOMPARE(documentB->iterateBlocks(), mergedBlocks);
        QCOMPARE(documentA->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QCOMPARE(documentB->blockText(originalBlocks[0]), QByteArrayLiteral("alpha!"));
        QCOMPARE(std::get<QString>(documentA->blockAttrs(originalBlocks[2]).value(
            AttrNames::Title)), QStringLiteral("peer-title"));
        QCOMPARE(std::get<QString>(documentB->blockAttrs(originalBlocks[2]).value(
            AttrNames::Title)), QStringLiteral("peer-title"));
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    TstMarkoffDocument tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_markoff_document.moc"
