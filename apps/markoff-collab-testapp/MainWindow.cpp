// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"
#include "InMemoryTransport.h"
#include "CollabConsumer.h"
#include <markoff/core/MarkoffDocument.h>
#include <markoff/canvas/EditorWidget.h>
#include <markoff/canvas/View.h>
#include <markoff/vnext/ActionPublicationCoordinator.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <algorithm>

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace {

void publishCaret(Markoff::MarkoffDocument* document,
                  Markoff::Canvas::EditorWidget* editor,
                  CollabConsumer* consumer,
                  const QString& participantId,
                  const QString& participantLabel,
                  const QColor& color)
{
    if (!document || !editor || !editor->view() || !consumer
        || !document->canStartOrdinaryAction())
        return;
    auto* view = editor->view();
    const Markoff::BlockId block = view->caretBlock();
    if (block.isNull())
        return;
    const int activeByte = std::max(0, view->caretByteOffset());
    int anchorByte = activeByte;
    if (view->hasSelection() && view->selectionAnchorBlock() == block)
        anchorByte = std::max(0, view->selectionAnchorByteOffset());
    const auto anchor = document->boundaryTokenAt(
        block, static_cast<quint32>(anchorByte), false);
    const auto active = document->boundaryTokenAt(
        block, static_cast<quint32>(activeByte), true);
    if (!anchor.ok() || !active.ok())
        return;
    Markoff::Selection selection;
    selection.kind = Markoff::Selection::Kind::Presence;
    selection.participantId = participantId;
    selection.participantLabel = participantLabel;
    selection.presenceColor = color;
    selection.anchorPosition = Markoff::ScopedTextPosition{block, anchor.value()};
    selection.activePosition = Markoff::ScopedTextPosition{block, active.value()};
    (void)consumer->publishPresence(selection);
}

void showRemoteCaret(Markoff::Canvas::EditorWidget* editor,
                     const Markoff::Selection& selection)
{
    if (!editor)
        return;
    editor->setRemotePresences(
        {Markoff::Canvas::RemotePresence{selection}});
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_transportA(std::make_unique<InMemoryTransport>("A"))
    , m_transportB(std::make_unique<InMemoryTransport>("B"))
{
    m_transportA->connectPeer(m_transportB.get());
    m_transportB->connectPeer(m_transportA.get());

    auto providerA = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    auto providerB = Markoff::VNext::makeEphemeralDocumentStorageProvider();
    if (!providerA.ok() || !providerB.ok())
        qFatal("Could not create collaboration test providers");
    m_providerA = std::move(providerA).value();
    m_providerB = std::move(providerB).value();

    const Markoff::VNext::DocumentStorageScope scope{
        QStringLiteral("markoff-collab-testapp"), 1};
    auto documentA = Markoff::MarkoffDocument::open(
        *m_providerA, scope, Markoff::VNext::StorageMode::ephemeral);
    auto documentB = Markoff::MarkoffDocument::open(
        *m_providerB, scope, Markoff::VNext::StorageMode::ephemeral);
    if (!documentA.ok() || !documentB.ok())
        qFatal("Could not open collaboration test documents");
    m_docA = std::move(documentA).value();
    m_docB = std::move(documentB).value();

    m_consumerA = std::make_unique<CollabConsumer>(m_docA.get(), m_transportA.get());
    m_consumerB = std::make_unique<CollabConsumer>(m_docB.get(), m_transportB.get());
    const auto imported = m_docA->importInitialMarkdown(
        QByteArrayLiteral("Hello, collab!\n"));
    if (imported.state != Markoff::VNext::ActionPublicationState::committed)
        qFatal("Could not import collaboration test document");

    QMainWindow::setWindowTitle(QStringLiteral("Markoff provider-backed collaboration"));
    auto *panes = new QWidget(this);
    auto *layout = new QHBoxLayout(panes);
    auto *paneA = new QWidget(panes);
    auto *paneLayoutA = new QVBoxLayout(paneA);
    paneLayoutA->addWidget(new QLabel(QStringLiteral("Replica A"), paneA));
    m_editorA = new Markoff::Canvas::EditorWidget(paneA);
    m_editorA->setDocument(m_docA.get());
    paneLayoutA->addWidget(m_editorA);

    auto *paneB = new QWidget(panes);
    auto *paneLayoutB = new QVBoxLayout(paneB);
    paneLayoutB->addWidget(new QLabel(QStringLiteral("Replica B"), paneB));
    m_editorB = new Markoff::Canvas::EditorWidget(paneB);
    m_editorB->setDocument(m_docB.get());
    paneLayoutB->addWidget(m_editorB);
    layout->addWidget(paneA);
    layout->addWidget(paneB);
    setCentralWidget(panes);
    resize(1400, 700);

    QObject::connect(m_editorA->view(), &Markoff::Canvas::View::caretChanged, this,
        [this] {
            publishCaret(m_docA.get(), m_editorA, m_consumerA.get(),
                QStringLiteral("replica-a"), QStringLiteral("Replica A"),
                QColor(0x22, 0x88, 0xCC));
        });
    QObject::connect(m_editorB->view(), &Markoff::Canvas::View::caretChanged, this,
        [this] {
            publishCaret(m_docB.get(), m_editorB, m_consumerB.get(),
                QStringLiteral("replica-b"), QStringLiteral("Replica B"),
                QColor(0xCC, 0x66, 0x22));
        });
    QObject::connect(m_consumerA.get(), &CollabConsumer::remotePresenceReceived,
        m_editorB, [this](const Markoff::Selection& selection) {
            showRemoteCaret(m_editorB, selection);
        });
    QObject::connect(m_consumerB.get(), &CollabConsumer::remotePresenceReceived,
        m_editorA, [this](const Markoff::Selection& selection) {
            showRemoteCaret(m_editorA, selection);
        });
    publishCaret(m_docA.get(), m_editorA, m_consumerA.get(),
        QStringLiteral("replica-a"), QStringLiteral("Replica A"),
        QColor(0x22, 0x88, 0xCC));
    publishCaret(m_docB.get(), m_editorB, m_consumerB.get(),
        QStringLiteral("replica-b"), QStringLiteral("Replica B"),
        QColor(0xCC, 0x66, 0x22));
}

MainWindow::~MainWindow()
{
    // The child editors are destroyed by QMainWindow after this class's
    // document members, so detach their raw document pointers first.
    if (m_editorA) m_editorA->setDocument(nullptr);
    if (m_editorB) m_editorB->setDocument(nullptr);
}
