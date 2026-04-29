// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/SourceTextDocumentBinding.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

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

    if (m_qtDoc) {
        // Detach from old QTextDocument before switching.
        QObject::disconnect(m_qtDoc, &QTextDocument::contentsChange,
                            this, &SourceTextDocumentBinding::onQtContentsChange);
    }

    m_qtDoc = newQtDoc;
    if (m_qtDoc) {
        // Foundation's CRDT undo (via EditorBackend.undo/redo) is canonical.
        // Disable QTextDocument's own undo stack to prevent double-undo behavior.
        m_qtDoc->setUndoRedoEnabled(false);
        QObject::connect(m_qtDoc, &QTextDocument::contentsChange,
                         this, &SourceTextDocumentBinding::onQtContentsChange);
    }
}

void SourceTextDocumentBinding::onQtContentsChange(int qtPos, int charsRemoved, int charsAdded)
{
    // Cycle guard: when T13's reverse path is mid-application, don't loop back.
    if (m_applyingRemoteEdit) return;
    if (!m_editorBackend) return;
    Markoff::MarkoffDocument *doc = m_editorBackend->document();
    if (!doc) return;
    if (!m_qtDoc) return;

    // Compute byte offsets against the PRE-CHANGE document state.
    // doc->toMarkdownUtf8() still holds the pre-change bytes because we haven't
    // called applyLocalEdit yet.
    const QByteArray preBytes = doc->toMarkdownUtf8();
    const QString preText = QString::fromUtf8(preBytes);
    const quint32 oldStart = qtPosToByteOffset(preText, qtPos);
    const quint32 oldEnd   = qtPosToByteOffset(preText, qtPos + charsRemoved);

    // Extract the inserted text from the POST-CHANGE QTextDocument.
    const QString postPlain = m_qtDoc->toPlainText();
    const QString insertedText = postPlain.mid(qtPos, charsAdded);
    const QByteArray newText = insertedText.toUtf8();

    Markoff::MarkoffEdit ed;
    ed.oldStart = oldStart;
    ed.oldEnd   = oldEnd;
    ed.newText  = newText;

    m_applyingLocalEdit = true;
    doc->applyLocalEdit({ ed });
    m_applyingLocalEdit = false;
}

}  // namespace Markoff::View::Qml
