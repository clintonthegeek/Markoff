// SPDX-License-Identifier: GPL-3.0-or-later
#include "MainWindow.h"
#include "InMemoryTransport.h"
#include "CollabConsumer.h"
#include <markoff/core/MarkoffDocument.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_docA(std::make_unique<Markoff::MarkoffDocument>(quint16(1)))
    , m_docB(std::make_unique<Markoff::MarkoffDocument>(quint16(2)))
    , m_transportA(std::make_unique<InMemoryTransport>("A"))
    , m_transportB(std::make_unique<InMemoryTransport>("B"))
    , m_consumerA(std::make_unique<CollabConsumer>(m_docA.get(), m_transportA.get()))
    , m_consumerB(std::make_unique<CollabConsumer>(m_docB.get(), m_transportB.get()))
{
    m_transportA->connectPeer(m_transportB.get());
    m_transportB->connectPeer(m_transportA.get());

    const QByteArray initial = QByteArrayLiteral("Hello, collab!\n");
    m_docA->loadFromMarkdown(initial);
    m_docB->loadFromMarkdown(initial);

    resize(1400, 700);
    setWindowTitle(QStringLiteral("Markoff D5 Collab Testapp"));
}

MainWindow::~MainWindow() = default;
