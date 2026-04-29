// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/SourceTextDocumentBinding.h>

namespace Markoff::View::Qml {

SourceTextDocumentBinding::SourceTextDocumentBinding(QObject *parent)
    : QObject(parent) {}
SourceTextDocumentBinding::~SourceTextDocumentBinding() = default;

quint32 SourceTextDocumentBinding::qtPosToByteOffset(const QString &text, int qtOffset)
{
    if (qtOffset <= 0) return 0;
    if (qtOffset >= text.size()) return static_cast<quint32>(text.toUtf8().size());
    return static_cast<quint32>(text.left(qtOffset).toUtf8().size());
}

int SourceTextDocumentBinding::byteOffsetToQtPos(const QByteArray &utf8, quint32 byteOffset)
{
    if (byteOffset == 0) return 0;
    if (byteOffset >= static_cast<quint32>(utf8.size())) {
        return QString::fromUtf8(utf8).size();
    }
    return QString::fromUtf8(utf8.left(static_cast<int>(byteOffset))).size();
}

EditorBackend *SourceTextDocumentBinding::editorBackend() const
{
    return m_editorBackend;
}

void SourceTextDocumentBinding::setEditorBackend(EditorBackend *eb)
{
    if (m_editorBackend == eb) return;
    m_editorBackend = eb;
    Q_EMIT editorBackendChanged();
    tryCaptureQtDocument();
}

QQuickTextDocument *SourceTextDocumentBinding::qtQuickDocument() const
{
    return m_qtQuickDoc;
}

void SourceTextDocumentBinding::setQtQuickDocument(QQuickTextDocument *q)
{
    if (m_qtQuickDoc == q) return;
    m_qtQuickDoc = q;
    Q_EMIT qtQuickDocumentChanged();
    tryCaptureQtDocument();
}

void SourceTextDocumentBinding::tryCaptureQtDocument()
{
    if (!m_qtQuickDoc || !m_editorBackend) {
        m_qtDoc = nullptr;
        return;
    }
    QTextDocument *newQtDoc = m_qtQuickDoc->textDocument();
    if (m_qtDoc == newQtDoc) return;
    m_qtDoc = newQtDoc;
    if (m_qtDoc) {
        // Foundation's CRDT undo (via EditorBackend.undo/redo) is canonical.
        // Disable QTextDocument's own undo stack to prevent double-undo behavior.
        m_qtDoc->setUndoRedoEnabled(false);
    }
}

}  // namespace Markoff::View::Qml
