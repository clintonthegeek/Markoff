// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QMainWindow>
#include <memory>

namespace Markoff { class MarkoffDocument; }
namespace Markoff::Canvas { class EditorWidget; }
namespace Markoff::VNext { class DocumentStorageProvider; }
class InMemoryTransport;
class CollabConsumer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
private:
    // Providers outlive the documents opened from them. Each peer has an
    // isolated ephemeral store but shares the same logical document scope.
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> m_providerA;
    std::unique_ptr<Markoff::VNext::DocumentStorageProvider> m_providerB;
    Markoff::Canvas::EditorWidget *m_editorA = nullptr;
    Markoff::Canvas::EditorWidget *m_editorB = nullptr;
    std::unique_ptr<Markoff::MarkoffDocument> m_docA;
    std::unique_ptr<Markoff::MarkoffDocument> m_docB;
    std::unique_ptr<InMemoryTransport>        m_transportA;
    std::unique_ptr<InMemoryTransport>        m_transportB;
    std::unique_ptr<CollabConsumer>           m_consumerA;
    std::unique_ptr<CollabConsumer>           m_consumerB;
};
