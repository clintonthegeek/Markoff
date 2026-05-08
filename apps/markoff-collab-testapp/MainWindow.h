// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>
#include <memory>

namespace Markoff { class MarkoffDocument; }
class InMemoryTransport;
class CollabConsumer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
private:
    std::unique_ptr<Markoff::MarkoffDocument> m_docA;
    std::unique_ptr<Markoff::MarkoffDocument> m_docB;
    std::unique_ptr<InMemoryTransport>        m_transportA;
    std::unique_ptr<InMemoryTransport>        m_transportB;
    std::unique_ptr<CollabConsumer>           m_consumerA;
    std::unique_ptr<CollabConsumer>           m_consumerB;
};
