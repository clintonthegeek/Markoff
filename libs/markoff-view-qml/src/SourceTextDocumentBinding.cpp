// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/SourceTextDocumentBinding.h>

#include <algorithm>

#include <QTextCursor>

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

    if (m_editorBackend) {
        QObject::disconnect(m_editorBackend, &EditorBackend::documentChanged,
                            this, &SourceTextDocumentBinding::onEditorBackendDocumentChanged);
        QObject::disconnect(m_editorBackend, &EditorBackend::cursorAnchorChanged,
                            this, nullptr);
        QObject::disconnect(m_editorBackend, &EditorBackend::selectionAnchorChanged,
                            this, nullptr);
        QObject::disconnect(m_editorBackend, &EditorBackend::selectionActiveChanged,
                            this, nullptr);
    }

    m_editorBackend = eb;

    if (m_editorBackend) {
        QObject::connect(m_editorBackend, &EditorBackend::documentChanged,
                         this, &SourceTextDocumentBinding::onEditorBackendDocumentChanged);
        QObject::connect(m_editorBackend, &EditorBackend::cursorAnchorChanged,
                         this, [this]() { syncFromBackendCursor(); });
        QObject::connect(m_editorBackend, &EditorBackend::selectionAnchorChanged,
                         this, [this]() { syncFromBackendSelection(); });
        QObject::connect(m_editorBackend, &EditorBackend::selectionActiveChanged,
                         this, [this]() { syncFromBackendSelection(); });
    }

    Q_EMIT editorBackendChanged();
    rebindMarkoffDocumentSubscription();
    tryCaptureQtDocument();
}

void SourceTextDocumentBinding::onEditorBackendDocumentChanged()
{
    rebindMarkoffDocumentSubscription();
}

// ---------------------------------------------------------------------------
// T14: cursor/selection int properties (UTF-16 ↔ CRDT anchor bridge)
// ---------------------------------------------------------------------------

int SourceTextDocumentBinding::cursorPosition() const { return m_cursorPosition; }
int SourceTextDocumentBinding::selectionStart()  const { return m_selectionStart; }
int SourceTextDocumentBinding::selectionEnd()    const { return m_selectionEnd;   }

void SourceTextDocumentBinding::setCursorPosition(int pos)
{
    if (m_cursorPosition == pos) return;
    m_cursorPosition = pos;

    if (!m_applyingBackendCursor && m_editorBackend && m_qtDoc) {
        Markoff::MarkoffDocument *doc = m_editorBackend->document();
        if (doc) {
            const QString text = m_qtDoc->toPlainText();
            const quint32 byteOff = qtPosToByteOffset(text, pos);
            const auto anchor = doc->anchorAt(byteOff, CollabText::Crdt::Bias::Left);
            m_editorBackend->setCursorAnchor(anchor);
        }
    }
    Q_EMIT cursorPositionChanged();
}

void SourceTextDocumentBinding::setSelectionStart(int pos)
{
    if (m_selectionStart == pos) return;
    m_selectionStart = pos;

    if (!m_applyingBackendCursor && m_editorBackend && m_qtDoc) {
        Markoff::MarkoffDocument *doc = m_editorBackend->document();
        if (doc) {
            const QString text = m_qtDoc->toPlainText();
            const quint32 byteOff = qtPosToByteOffset(text, pos);
            const auto anchor = doc->anchorAt(byteOff, CollabText::Crdt::Bias::Left);
            m_editorBackend->setSelectionAnchor(anchor);
        }
    }
    Q_EMIT selectionStartChanged();
}

void SourceTextDocumentBinding::setSelectionEnd(int pos)
{
    if (m_selectionEnd == pos) return;
    m_selectionEnd = pos;

    if (!m_applyingBackendCursor && m_editorBackend && m_qtDoc) {
        Markoff::MarkoffDocument *doc = m_editorBackend->document();
        if (doc) {
            const QString text = m_qtDoc->toPlainText();
            const quint32 byteOff = qtPosToByteOffset(text, pos);
            const auto anchor = doc->anchorAt(byteOff, CollabText::Crdt::Bias::Right);
            m_editorBackend->setSelectionActive(anchor);
        }
    }
    Q_EMIT selectionEndChanged();
}

void SourceTextDocumentBinding::syncFromBackendCursor()
{
    if (!m_editorBackend || !m_qtDoc) return;
    Markoff::MarkoffDocument *doc = m_editorBackend->document();
    if (!doc) return;

    const quint32 byteOff = doc->resolveAnchor(m_editorBackend->cursorAnchor());
    const QByteArray utf8 = doc->toMarkdownUtf8();
    const int qtPos = byteOffsetToQtPos(utf8, byteOff);

    if (m_cursorPosition == qtPos) return;
    m_cursorPosition = qtPos;
    m_applyingBackendCursor = true;
    Q_EMIT cursorPositionChanged();
    m_applyingBackendCursor = false;
}

void SourceTextDocumentBinding::syncFromBackendSelection()
{
    if (!m_editorBackend || !m_qtDoc) return;
    Markoff::MarkoffDocument *doc = m_editorBackend->document();
    if (!doc) return;

    const quint32 anchorByte = doc->resolveAnchor(m_editorBackend->selectionAnchor());
    const quint32 activeByte = doc->resolveAnchor(m_editorBackend->selectionActive());
    const QByteArray utf8 = doc->toMarkdownUtf8();
    const int newStart = byteOffsetToQtPos(utf8, anchorByte);
    const int newEnd   = byteOffsetToQtPos(utf8, activeByte);

    m_applyingBackendCursor = true;
    if (m_selectionStart != newStart) {
        m_selectionStart = newStart;
        Q_EMIT selectionStartChanged();
    }
    if (m_selectionEnd != newEnd) {
        m_selectionEnd = newEnd;
        Q_EMIT selectionEndChanged();
    }
    m_applyingBackendCursor = false;
}

void SourceTextDocumentBinding::rebindMarkoffDocumentSubscription()
{
    Markoff::MarkoffDocument *newDoc =
        m_editorBackend ? m_editorBackend->document() : nullptr;

    if (newDoc == m_subscribedDoc) return;

    if (m_subscribedDoc) {
        QObject::disconnect(m_subscribedDoc, &Markoff::MarkoffDocument::contentsChanged,
                            this, &SourceTextDocumentBinding::onMarkoffContentsChanged);
    }

    m_subscribedDoc = newDoc;

    if (m_subscribedDoc) {
        QObject::connect(m_subscribedDoc, &Markoff::MarkoffDocument::contentsChanged,
                         this, &SourceTextDocumentBinding::onMarkoffContentsChanged);
    }

    // If both doc and qtDoc are captured, seed the qtDoc with the doc's
    // current content. This handles the case where MarkoffDocument was
    // populated (e.g. via resetContent) BEFORE the binding subscribed.
    syncQtDocumentFromMarkoff();
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

    // Seed the qtDoc from the foundation's current content (if both are now ready).
    syncQtDocumentFromMarkoff();
}

void SourceTextDocumentBinding::syncQtDocumentFromMarkoff()
{
    if (!m_subscribedDoc || !m_qtDoc) return;
    const QByteArray utf8 = m_subscribedDoc->toMarkdownUtf8();
    const QString text = QString::fromUtf8(utf8);
    if (m_qtDoc->toPlainText() == text) return;  // already in sync
    m_applyingRemoteEdit = true;
    m_qtDoc->setPlainText(text);
    m_applyingRemoteEdit = false;
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

void SourceTextDocumentBinding::onMarkoffContentsChanged(const QList<Markoff::MarkoffEdit> &edits)
{
    // Cycle guard: if WE just called applyLocalEdit (T12 forward path), this is
    // the echo of our own change — don't re-apply.
    if (m_applyingLocalEdit) return;
    if (!m_qtDoc) return;
    if (!m_subscribedDoc) return;

    // Capture the pre-change plain text from QTextDocument (it hasn't been
    // touched yet) for byte→Qt-pos conversion.
    const QString preText = m_qtDoc->toPlainText();
    const QByteArray preBytes = preText.toUtf8();

    m_applyingRemoteEdit = true;
    QTextCursor cursor(m_qtDoc);

    // Walk edits in reverse byte-offset order so that earlier edits' positions
    // are not invalidated by the mutations we apply to later (higher-offset) regions.
    // All offsets in the list are OLD-text coordinates (pre-batch state), so
    // reverse application keeps them valid throughout the loop.
    QList<Markoff::MarkoffEdit> sorted = edits;
    std::sort(sorted.begin(), sorted.end(),
              [](const Markoff::MarkoffEdit &a, const Markoff::MarkoffEdit &b) {
                  return a.oldStart > b.oldStart;
              });

    for (const Markoff::MarkoffEdit &ed : sorted) {
        const int qtStart = byteOffsetToQtPos(preBytes, ed.oldStart);
        const int qtEnd   = byteOffsetToQtPos(preBytes, ed.oldEnd);

        cursor.setPosition(qtStart);
        cursor.setPosition(qtEnd, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        if (!ed.newText.isEmpty()) {
            cursor.insertText(QString::fromUtf8(ed.newText));
        }
    }

    m_applyingRemoteEdit = false;
}

}  // namespace Markoff::View::Qml
