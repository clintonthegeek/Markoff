// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"
#include "InMemoryTransport.h"
#include "CollabConsumer.h"
#include <markoff/core/MarkoffDocument.h>
#include <markoff/canvas/EditorWidget.h>
#include <markoff/vnext/ActionPublicationCoordinator.h>
#include <markoff/vnext/DocumentStorageProvider.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

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
}

MainWindow::~MainWindow()
{
    // The child editors are destroyed by QMainWindow after this class's
    // document members, so detach their raw document pointers first.
    if (m_editorA) m_editorA->setDocument(nullptr);
    if (m_editorB) m_editorB->setDocument(nullptr);
}
