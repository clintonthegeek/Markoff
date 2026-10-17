// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QAccessible>
#include <QCoreApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QImage>
#include <QMimeData>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "A11yEventSpy.h"

#include <markoff/canvas/EditorWidget.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <ReferenceProviderTestHooks.h>

#include <memory>
#include <filesystem>
#include <stdexcept>

namespace {
struct Fixture {
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> provider;
    std::unique_ptr<Markoff::MarkoffDocument> document;
};

Fixture makeDocument()
{
    auto provider = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!provider.ok()) throw std::runtime_error("provider creation failed");
    Fixture fixture{std::move(provider).value(), {}};
    auto opened = Markoff::MarkoffDocument::open(*fixture.provider,
        {QStringLiteral("canvas-token-session"), 1},
        Markoff::VNext::StorageMode::ephemeral);
    if (!opened.ok()) throw std::runtime_error("document open failed");
    fixture.document = std::move(opened).value();
    if (fixture.document->importInitialMarkdown(QByteArrayLiteral("Hello world"))
            .state != Markoff::VNext::ActionPublicationState::committed)
        throw std::runtime_error("initial import failed");
    return fixture;
}

int countColorPixels(const QImage& image, const QColor& color)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixel(x, y) == color.rgb()) ++count;
    return count;
}

int durableCanvasWorker(const QString& mode, const QString& root)
{
    const QString providerRoot = QDir(root).filePath(QStringLiteral("provider"));
    const QString sessionPath = QDir(root).filePath(QStringLiteral("canvas-session.json"));
    const Markoff::VNext::DocumentStorageScope scope{
        QStringLiteral("canvas-process-reopen"), 1};

    auto providerResult = Markoff::VNext::makeReferenceDocumentStorageProvider(
        std::filesystem::path(providerRoot.toStdString()));
    if (!providerResult.ok()) return 10;
    auto provider = std::move(providerResult).value();
    auto documentResult = Markoff::MarkoffDocument::open(
        *provider, scope, Markoff::VNext::StorageMode::durable);
    if (!documentResult.ok()) return 11;
    auto document = std::move(documentResult).value();

    if (mode == QStringLiteral("--write-durable-canvas")) {
        if (document->importInitialMarkdown(
                QByteArrayLiteral("# Durable canvas\n\nCanvas survives restart."))
                .state != Markoff::VNext::ActionPublicationState::committed)
            return 12;
        const auto blocks = document->iterateBlocks();
        if (blocks.size() != 2) return 13;
        const auto edit = document->applyBlockEdit(
            {blocks[1], 0, 0, QByteArrayLiteral("Persisted: ")});
        if (edit.state != Markoff::VNext::ActionPublicationState::committed)
            return 14;

        auto* session = document->createSession();
        if (!session) return 15;
        Markoff::Canvas::View view;
        view.resize(420, 280);
        view.setDocument(document.get());
        view.setSession(session);
        view.setCaretPosition(blocks[1], 10);
        const auto selection = session->primarySelection();
        if (!selection.activePosition
            || selection.activePosition->block != blocks[1]) return 16;
        QFile output(sessionPath);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 17;
        const auto bytes = QJsonDocument(session->toJson()).toJson(
            QJsonDocument::Compact);
        if (output.write(bytes) != bytes.size()) return 18;
        if (!output.flush()) return 19;
        return 0;
    }

    if (mode != QStringLiteral("--read-durable-canvas")) return 20;
    if (document->iterateBlocks().size() != 2) return 21;
    const auto blocks = document->iterateBlocks();
    if (document->blockText(blocks[1])
        != QByteArrayLiteral("Persisted: Canvas survives restart.")) return 22;
    QFile input(sessionPath);
    if (!input.open(QIODevice::ReadOnly)) return 23;
    QJsonParseError parseError;
    const auto saved = QJsonDocument::fromJson(input.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !saved.isObject()) return 24;

    auto* session = document->createSession();
    if (!session) return 25;
    session->fromJson(saved.object());
    Markoff::Canvas::View view;
    view.resize(420, 280);
    view.setSession(session);
    view.setDocument(document.get());
    view.show();
    QCoreApplication::processEvents();

    const auto selection = session->primarySelection();
    if (!selection.activePosition || selection.activePosition->block != blocks[1])
        return 26;
    const auto offset = document->resolveBoundaryToken(
        blocks[1], selection.activePosition->boundaryToken);
    if (!offset.ok() || offset.value() != 10) return 27;
    if (view.caretBlock() != blocks[1] || view.caretByteOffset() != 10)
        return 28;
    return 0;
}
}

class TstCanvasVNextSessionPositions final : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void publicationPendingTracksTentativeCommitAndRollbackSignals()
    {
        auto emptyProvider = Markoff::VNext::makeEphemeralDocumentStorageProvider();
        QVERIFY(emptyProvider.ok());
        auto emptyOpen = Markoff::MarkoffDocument::open(*emptyProvider.value(),
            {QStringLiteral("canvas-readiness-gate"), 1},
            Markoff::VNext::StorageMode::ephemeral);
        QVERIFY(emptyOpen.ok());
        auto emptyDocument = std::move(emptyOpen).value();
        Markoff::Canvas::View emptyView;
        emptyView.setDocument(emptyDocument.get());
        QVERIFY(emptyView.isReadOnly());
        QVERIFY(!emptyView.publicationPending());
        QCOMPARE(emptyDocument->importInitialMarkdown(
            QByteArrayLiteral("ready")).state,
            Markoff::VNext::ActionPublicationState::committed);
        QVERIFY(!emptyView.isReadOnly());

        auto fixture = makeDocument();
        Markoff::Canvas::View view;
        QSignalSpy changes(&view,
            &Markoff::Canvas::View::publicationPendingChanged);
        QSignalSpy readOnlyChanges(&view,
            &Markoff::Canvas::View::readOnlyChanged);
        view.setDocument(fixture.document.get());
        QVERIFY(!view.publicationPending());
        QVERIFY(!view.isReadOnly());

        const auto block = fixture.document->iterateBlocks().front();
        emit fixture.document->tentativeBlockChanged(block);
        QVERIFY(view.publicationPending());
        QVERIFY(view.isReadOnly());
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.takeFirst().at(0).toBool(), true);
        QCOMPARE(readOnlyChanges.size(), 1);
        QCOMPARE(readOnlyChanges.takeFirst().at(0).toBool(), true);

        emit fixture.document->tentativeBlockCommitted(block);
        QVERIFY(!view.publicationPending());
        QVERIFY(!view.isReadOnly());
        QCOMPARE(changes.size(), 1);
        QCOMPARE(changes.takeFirst().at(0).toBool(), false);
        QCOMPARE(readOnlyChanges.size(), 1);
        QCOMPARE(readOnlyChanges.takeFirst().at(0).toBool(), false);

        emit fixture.document->tentativeBlockChanged(block);
        QVERIFY(view.publicationPending());
        QVERIFY(view.isReadOnly());
        emit fixture.document->tentativeBlockReverted(block);
        QVERIFY(!view.publicationPending());
        QVERIFY(!view.isReadOnly());
        QCOMPARE(changes.size(), 2);
        QCOMPARE(changes.at(0).at(0).toBool(), true);
        QCOMPARE(changes.at(1).at(0).toBool(), false);
    }

    void providerRecoveryTransitionsReachCanvasHost()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto providerResult = Markoff::VNext::makeReferenceDocumentStorageProvider(
            std::filesystem::path(directory.path().toStdString()));
        QVERIFY(providerResult.ok());
        auto provider = std::move(providerResult).value();
        auto opened = Markoff::MarkoffDocument::open(*provider,
            {QStringLiteral("canvas-provider-recovery"), 1},
            Markoff::VNext::StorageMode::durable);
        QVERIFY(opened.ok());
        auto document = std::move(opened).value();
        QCOMPARE(document->importInitialMarkdown(QByteArrayLiteral("stable")).state,
            Markoff::VNext::ActionPublicationState::committed);

        Markoff::Canvas::View view;
        QSignalSpy pendingChanges(&view,
            &Markoff::Canvas::View::publicationPendingChanged);
        QSignalSpy readOnlyChanges(&view,
            &Markoff::Canvas::View::readOnlyChanged);
        QSignalSpy providerChanges(document.get(),
            &Markoff::MarkoffDocument::providerStateChanged);
        view.setDocument(document.get());
        QVERIFY(!view.publicationPending());
        QVERIFY(!view.isReadOnly());

        const auto block = document->iterateBlocks().front();
        Markoff::VNext::Testing::failNextReferenceProviderCommitAt(
            Markoff::VNext::Testing::ReferenceProviderFaultPoint::appendBeforeAtomicRename);
        const auto rejected = document->applyBlockEdit(
            {block, 6, 0, QByteArrayLiteral("!")});
        QCOMPARE(rejected.state, Markoff::VNext::ActionPublicationState::absent);
        QCOMPARE(document->providerState(),
            Markoff::VNext::ProviderDocumentState::recoveryRequired);
        QVERIFY(view.publicationPending());
        QVERIFY(view.isReadOnly());
        QCOMPARE(providerChanges.size(), 1);
        QCOMPARE(pendingChanges.size(), 1);
        QCOMPARE(pendingChanges.takeFirst().at(0).toBool(), true);
        QCOMPARE(readOnlyChanges.size(), 1);
        QCOMPARE(readOnlyChanges.takeFirst().at(0).toBool(), true);

        QVERIFY(document->recoverPendingAllocations().ok());
        QCOMPARE(document->providerState(), Markoff::VNext::ProviderDocumentState::ready);
        QVERIFY(!view.publicationPending());
        QVERIFY(!view.isReadOnly());
        QCOMPARE(providerChanges.size(), 2);
        QCOMPARE(pendingChanges.size(), 1);
        QCOMPARE(pendingChanges.takeFirst().at(0).toBool(), false);
        QCOMPARE(readOnlyChanges.size(), 1);
        QCOMPARE(readOnlyChanges.takeFirst().at(0).toBool(), false);
    }

    void durableDocumentAndCanvasCaretRestoreAcrossProcessReopen()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto runWorker = [&](const QString& mode) {
            QProcess process;
            process.start(QCoreApplication::applicationFilePath(),
                {mode, directory.path()});
            if (!process.waitForStarted(5000)) {
                qWarning() << "Canvas durability worker failed to start:" << mode
                           << process.errorString();
                return false;
            }
            if (!process.waitForFinished(30000)) {
                process.kill();
                process.waitForFinished();
                qWarning() << "Canvas durability worker timed out:" << mode;
                return false;
            }
            if (process.exitStatus() != QProcess::NormalExit
                || process.exitCode() != 0) {
                qWarning().noquote() << "Canvas durability worker failed:" << mode
                    << "exit=" << process.exitCode()
                    << "stderr=" << process.readAllStandardError();
                return false;
            }
            return true;
        };
        QVERIFY(runWorker(QStringLiteral("--write-durable-canvas")));
        QVERIFY(runWorker(QStringLiteral("--read-durable-canvas")));
    }

    void caretSessionPositionFollowsAcceptedEdit()
    {
        auto fixture = makeDocument();
        Markoff::Canvas::EditorWidget editor;
        editor.resize(400, 280);
        editor.setDocument(fixture.document.get());
        editor.show();
        QCoreApplication::processEvents();

        const auto block = fixture.document->iterateBlocks().front();
        auto* session = editor.view()->session();
        QVERIFY(session);
        editor.view()->setCaretPosition(block, 6);

        auto selection = session->primarySelection();
        QVERIFY(selection.activePosition.has_value());
        QCOMPARE(selection.activePosition->block, block);
        auto resolved = fixture.document->resolveBoundaryToken(
            block, selection.activePosition->boundaryToken);
        QVERIFY(resolved.ok());
        QCOMPARE(resolved.value(), quint32(6));

        const auto edit = fixture.document->applyBlockEdit(
            {block, 0, 0, QByteArrayLiteral("Oh, ")});
        QCOMPARE(edit.state, Markoff::VNext::ActionPublicationState::committed);
        QCoreApplication::processEvents();

        QCOMPARE(editor.view()->caretBlock(), block);
        QCOMPARE(editor.view()->caretByteOffset(), 10);
        selection = session->primarySelection();
        QVERIFY(selection.activePosition.has_value());
        resolved = fixture.document->resolveBoundaryToken(
            block, selection.activePosition->boundaryToken);
        QVERIFY(resolved.ok());
        QCOMPARE(resolved.value(), quint32(10));
    }

    void remotePresenceResolvesScopedTokensAndDropsDetachedBlock()
    {
        auto sourceProviderResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
        QVERIFY(sourceProviderResult.ok());
        Fixture source{std::move(sourceProviderResult).value(), {}};
        const Markoff::VNext::DocumentStorageScope scope{
            QStringLiteral("canvas-remote-presence-peers"), 1};
        auto sourceOpen = Markoff::MarkoffDocument::open(*source.provider, scope,
            Markoff::VNext::StorageMode::ephemeral);
        QVERIFY(sourceOpen.ok());
        source.document = std::move(sourceOpen).value();
        QSignalSpy initialAction(source.document.get(),
            &Markoff::MarkoffDocument::localActionEnvelopeCommitted);
        QCOMPARE(source.document->importInitialMarkdown(
            QByteArrayLiteral("Hello world")).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(initialAction.size(), 1);
        const QByteArray initialMab = initialAction.takeFirst().at(1).toByteArray();

        auto receiverProviderResult = Markoff::VNext::makeEphemeralDocumentStorageProvider();
        QVERIFY(receiverProviderResult.ok());
        Fixture receiver{std::move(receiverProviderResult).value(), {}};
        auto receiverOpen = Markoff::MarkoffDocument::open(*receiver.provider, scope,
            Markoff::VNext::StorageMode::ephemeral);
        QVERIFY(receiverOpen.ok());
        receiver.document = std::move(receiverOpen).value();
        QCOMPARE(receiver.document->acceptRemoteAction(initialMab).state,
            Markoff::VNext::ActionPublicationState::committed);

        MarkoffTest::A11yEventSpy accessibilityEvents;
        Markoff::Canvas::EditorWidget editor;
        editor.resize(400, 280);
        editor.setDocument(receiver.document.get());
        editor.show();
        QCoreApplication::processEvents();
        auto* accessibleRoot = QAccessible::queryAccessibleInterface(editor.view());
        QVERIFY(accessibleRoot);
        QCOMPARE(accessibleRoot->childCount(), 1);

        const auto block = receiver.document->iterateBlocks().front();
        Markoff::Selection remote;
        remote.kind = Markoff::Selection::Kind::Presence;
        remote.participantId = QStringLiteral("peer-1");
        remote.participantLabel = QStringLiteral("Peer");
        remote.presenceColor = QColor(0x22, 0x88, 0xCC);
        const auto startToken = receiver.document->boundaryTokenAt(block, 1, false);
        const auto endToken = receiver.document->boundaryTokenAt(block, 4, true);
        QVERIFY(startToken.ok());
        QVERIFY(endToken.ok());
        remote.anchorPosition = Markoff::ScopedTextPosition{
            block, startToken.value()};
        remote.activePosition = Markoff::ScopedTextPosition{
            block, endToken.value()};
        editor.view()->setRemotePresences({Markoff::Canvas::RemotePresence{remote}});
        QCOMPARE(editor.view()->remotePresencesForBlock(block).size(), 1);

        QSignalSpy peerEdit(source.document.get(),
            &Markoff::MarkoffDocument::localActionEnvelopeCommitted);
        QCOMPARE(source.document->applyBlockEdit(
            {block, 0, 0, QByteArrayLiteral("Oh, ")}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(peerEdit.size(), 1);
        const auto remoteEdit = receiver.document->routeRemoteAction(
            peerEdit.takeFirst().at(1).toByteArray());
        QCOMPARE(remoteEdit.state, Markoff::RemoteActionRouteState::committed);
        QCOMPARE(remoteEdit.committed.size(), size_t{1});
        QCoreApplication::processEvents();
        QVERIFY(receiver.document->resolveBoundaryToken(
            block, remote.anchorPosition->boundaryToken).ok());
        QVERIFY(receiver.document->resolveBoundaryToken(
            block, remote.activePosition->boundaryToken).ok());
        QCOMPARE(receiver.document->resolveBoundaryToken(
            block, remote.anchorPosition->boundaryToken).value(), quint32(5));
        QCOMPARE(receiver.document->resolveBoundaryToken(
            block, remote.activePosition->boundaryToken).value(), quint32(8));
        QCOMPARE(editor.view()->remotePresencesForBlock(block).size(), 1);
        editor.view()->viewport()->repaint();
        const int selectedPixels = countColorPixels(
            editor.view()->grab().toImage(), remote.presenceColor);
        auto caretOnly = remote;
        caretOnly.activePosition = caretOnly.anchorPosition;
        editor.view()->setRemotePresences(
            {Markoff::Canvas::RemotePresence{caretOnly}});
        editor.view()->viewport()->repaint();
        const int caretPixels = countColorPixels(
            editor.view()->grab().toImage(), remote.presenceColor);
        QVERIFY2(selectedPixels > caretPixels + 20,
            qPrintable(QStringLiteral("remote selection tint was not painted: range=%1 caret=%2")
                .arg(selectedPixels).arg(caretPixels)));
        editor.view()->setRemotePresences({Markoff::Canvas::RemotePresence{remote}});

        accessibilityEvents.clear();
        QSignalSpy peerInsertion(source.document.get(),
            &Markoff::MarkoffDocument::localActionEnvelopeCommitted);
        QCOMPARE(source.document->applyStructural({
            Markoff::StructuralOp::InsertEntry{block, Markoff::BlockKind::Paragraph}}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCOMPARE(peerInsertion.size(), 1);
        const auto inserted = receiver.document->routeRemoteAction(
            peerInsertion.takeFirst().at(1).toByteArray());
        QCOMPARE(inserted.state, Markoff::RemoteActionRouteState::committed);
        QCOMPARE(receiver.document->iterateBlocks().size(), size_t{2});
        const auto insertedBlock = receiver.document->iterateBlocks().back();
        QVERIFY(insertedBlock != block);
        QCoreApplication::processEvents();
        QCOMPARE(accessibleRoot->childCount(), 2);
        const auto createdEvents = accessibilityEvents.eventsOfType(
            QAccessible::ObjectCreated);
        QCOMPARE(createdEvents.size(), 1);
        QCOMPARE(createdEvents.front().iface, accessibleRoot->child(1));

        QCOMPARE(receiver.document->applyStructural({
            Markoff::StructuralOp::RemoveEntry{block}}).state,
            Markoff::VNext::ActionPublicationState::committed);
        QCoreApplication::processEvents();
        QVERIFY(editor.view()->remotePresencesForBlock(block).isEmpty());
        QCOMPARE(accessibleRoot->childCount(), 1);
    }

    void lineMovePublishesOneUndoablePayloadSwap()
    {
        auto fixture = makeDocument();
        QVERIFY(fixture.document->replaceMarkdown(
            QByteArrayLiteral("## Heading\n\nplain")).state
            == Markoff::VNext::ActionPublicationState::committed);
        Markoff::Canvas::EditorWidget editor;
        editor.resize(400, 280);
        editor.setDocument(fixture.document.get());
        editor.show();
        QCoreApplication::processEvents();
        const auto blocks = fixture.document->iterateBlocks();
        editor.view()->setCaretPosition(blocks.front(), 3);
        editor.view()->moveLine(true);
        QCOMPARE(fixture.document->blockKind(blocks.front()), Markoff::BlockKind::Paragraph);
        QCOMPARE(fixture.document->blockText(blocks.front()), QByteArrayLiteral("plain"));
        QCOMPARE(fixture.document->blockKind(blocks.back()), Markoff::BlockKind::Heading);
        fixture.document->undoD2();
        QCOMPARE(fixture.document->blockKind(blocks.front()), Markoff::BlockKind::Heading);
        QCOMPARE(fixture.document->blockText(blocks.front()), QByteArrayLiteral("## Heading"));
    }

    void scopedSessionFoldsSyncAcrossViewsAndSerialization()
    {
        auto fixture = makeDocument();
        QCOMPARE(fixture.document->replaceMarkdown(QByteArrayLiteral(
            "# First\n\nFirst body.\n\n# Second\n\nSecond body.")).state,
            Markoff::VNext::ActionPublicationState::committed);
        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{4});
        auto* session = fixture.document->createSession();
        QVERIFY(session);

        Markoff::Canvas::View firstView;
        Markoff::Canvas::View secondView;
        firstView.resize(420, 280);
        secondView.resize(420, 280);
        firstView.setDocument(fixture.document.get());
        firstView.setSession(session);
        secondView.setDocument(fixture.document.get());
        secondView.setSession(session);
        firstView.show();
        secondView.show();
        QCoreApplication::processEvents();

        firstView.toggleFold(blocks[0]);
        QVERIFY(firstView.isBlockFolded(blocks[0]));
        QVERIFY(secondView.isBlockFolded(blocks[0]));
        QVERIFY(firstView.isBlockHidden(blocks[1]));
        QVERIFY(secondView.isBlockHidden(blocks[1]));
        QCOMPARE(session->foldedRegions().size(), 1);
        QVERIFY(session->foldedRegions().front().startPosition.has_value());
        QCOMPARE(session->foldedRegions().front().startPosition->block, blocks[0]);

        const QByteArray serialized = QJsonDocument(session->toJson()).toJson(
            QJsonDocument::Compact);
        QJsonParseError parseError;
        const auto saved = QJsonDocument::fromJson(serialized, &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        auto* restoredSession = fixture.document->createSession();
        QVERIFY(restoredSession);
        restoredSession->fromJson(saved.object());
        Markoff::Canvas::View restoredView;
        restoredView.setSession(restoredSession);
        restoredView.setDocument(fixture.document.get());
        restoredView.show();
        QCoreApplication::processEvents();
        QVERIFY(restoredView.isBlockFolded(blocks[0]));
        QVERIFY(restoredView.isBlockHidden(blocks[1]));

        // Detaching a view clears document-local projection state; attaching
        // the same document again must restore it from the still-attached
        // scoped Session even though setSession() itself is unchanged.
        secondView.setDocument(nullptr);
        QVERIFY(!secondView.isBlockFolded(blocks[0]));
        secondView.setDocument(fixture.document.get());
        QVERIFY(secondView.isBlockFolded(blocks[0]));

        // Session changes made by another view or an external session reader
        // must replace, rather than accumulate, the view's fold projection.
        session->setFoldedRegions({});
        QVERIFY(!firstView.isBlockFolded(blocks[0]));
        QVERIFY(!secondView.isBlockFolded(blocks[0]));
        QVERIFY(restoredView.isBlockFolded(blocks[0]));

        firstView.toggleFold(blocks[0]);
        QVERIFY(firstView.isBlockFolded(blocks[0]));
        QVERIFY(secondView.isBlockFolded(blocks[0]));
        const auto edit = fixture.document->applyBlockEdit(
            {blocks[0], 0, 0, QByteArrayLiteral("Updated ")});
        QCOMPARE(edit.state, Markoff::VNext::ActionPublicationState::committed);
        QVERIFY(firstView.isBlockFolded(blocks[0]));
        QVERIFY(secondView.isBlockFolded(blocks[0]));
        QVERIFY(firstView.isBlockHidden(blocks[1]));
        QVERIFY(secondView.isBlockHidden(blocks[1]));
    }

    void smartPastePreservesStructureAndTail()
    {
        auto fixture = makeDocument();
        Markoff::Canvas::EditorWidget editor;
        editor.resize(400, 280);
        editor.setDocument(fixture.document.get());
        editor.show();
        QCoreApplication::processEvents();
        const auto target = fixture.document->iterateBlocks().front();
        editor.view()->setCaretPosition(target, 5);
        auto* mime = new QMimeData;
        mime->setData(QStringLiteral("text/markdown"), QByteArrayLiteral("one\n\n- two"));
        mime->setText(QStringLiteral("one\n\n- two"));
        QGuiApplication::clipboard()->setMimeData(mime);
        editor.view()->paste();
        const auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{3});
        QCOMPARE(fixture.document->blockText(blocks[0]), QByteArrayLiteral("Hello"));
        QCOMPARE(fixture.document->blockText(blocks[1]), QByteArrayLiteral("one"));
        QCOMPARE(fixture.document->blockKind(blocks[2]), Markoff::BlockKind::ListItem);
        QCOMPARE(fixture.document->listItemDisplayMarker(blocks[2]), QByteArrayLiteral("- "));
        QCOMPARE(fixture.document->blockText(blocks[2]), QByteArrayLiteral("two world"));
        fixture.document->undoD2();
        QCOMPARE(fixture.document->iterateBlocks().size(), size_t{1});
        QCOMPARE(fixture.document->blockText(target), QByteArrayLiteral("Hello world"));
    }

    void tableInsertionIsOneUndoableAction()
    {
        auto fixture = makeDocument();
        Markoff::Canvas::EditorWidget editor;
        editor.resize(400, 280);
        editor.setDocument(fixture.document.get());
        editor.show();
        QCoreApplication::processEvents();
        const auto original = fixture.document->iterateBlocks().front();
        editor.view()->setCaretPosition(original, 11);
        editor.view()->insertTable();
        auto blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{2});
        QCOMPARE(fixture.document->blockKind(blocks[1]), Markoff::BlockKind::Table);
        QVERIFY(fixture.document->blockText(blocks[1]).startsWith("|  |  |"));
        fixture.document->undoD2();
        blocks = fixture.document->iterateBlocks();
        QCOMPARE(blocks.size(), size_t{1});
        QCOMPARE(blocks.front(), original);
    }
};

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() == 3
        && (args[1] == QStringLiteral("--write-durable-canvas")
            || args[1] == QStringLiteral("--read-durable-canvas")))
        return durableCanvasWorker(args[1], args[2]);
    TstCanvasVNextSessionPositions test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_canvas_vnext_session_positions.moc"
