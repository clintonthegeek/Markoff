// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveEditBinding.h>

#include <QTextDocument>

#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/SourceTextDocumentBinding.h>

namespace Markoff::View::Qml {

LiveEditBinding::LiveEditBinding(QObject *parent)
    : QObject(parent)
{}

LiveEditBinding::~LiveEditBinding() = default;

// ---------------------------------------------------------------------------
// document property
// ---------------------------------------------------------------------------

Markoff::MarkoffDocument *LiveEditBinding::document() const
{
    return m_document;
}

void LiveEditBinding::setDocument(Markoff::MarkoffDocument *doc)
{
    if (m_document == doc) return;
    m_document = doc;
    Q_EMIT documentChanged();
}

// ---------------------------------------------------------------------------
// blockAnchor property
// ---------------------------------------------------------------------------

Markoff::BlockAnchor LiveEditBinding::blockAnchor() const
{
    return m_blockAnchor;
}

void LiveEditBinding::setBlockAnchor(const Markoff::BlockAnchor &anchor)
{
    if (m_blockAnchor == anchor) return;
    m_blockAnchor = anchor;
    m_lastEditWasPrintable = false;  // anchor change breaks coalesce chain
    Q_EMIT blockAnchorChanged();
}

// ---------------------------------------------------------------------------
// textDocument property
// ---------------------------------------------------------------------------

QQuickTextDocument *LiveEditBinding::textDocument() const
{
    return m_quickDoc;
}

void LiveEditBinding::setTextDocument(QQuickTextDocument *qtd)
{
    if (m_quickDoc == qtd) return;
    m_quickDoc = qtd;
    rewireTextDocument();
    Q_EMIT textDocumentChanged();
}

void LiveEditBinding::rewireTextDocument()
{
    if (m_textDoc) {
        QObject::disconnect(m_textDoc, &QTextDocument::contentsChange,
                            this, &LiveEditBinding::onContentsChange);
        m_textDoc = nullptr;
    }

    if (m_quickDoc) {
        m_textDoc = m_quickDoc->textDocument();
        if (m_textDoc) {
            // Disable QTextDocument undo — CRDT undo is canonical.
            m_textDoc->setUndoRedoEnabled(false);
            QObject::connect(m_textDoc, &QTextDocument::contentsChange,
                             this, &LiveEditBinding::onContentsChange,
                             Qt::UniqueConnection);
            // Guard against external destruction of the QTextDocument.
            connect(m_textDoc, &QObject::destroyed, this, [this]() {
                m_textDoc = nullptr;
                m_quickDoc = nullptr;
            });
        }
    }
}

// ---------------------------------------------------------------------------
// Cycle-guard invokables
// ---------------------------------------------------------------------------

void LiveEditBinding::setModelText(const QString &text)
{
    // Call QTextDocument::setPlainText directly so contentsChange fires
    // synchronously, while the guard is held. The QML path
    // (textEdit.text = x; endModelUpdate()) does NOT work reliably because
    // Qt Quick defers the TextEdit text update past the endModelUpdate() call.
    if (!m_textDoc) return;
    m_applyingModelUpdate = true;
    m_textDoc->setPlainText(text);
    m_applyingModelUpdate = false;
}

void LiveEditBinding::beginModelUpdate()
{
    m_applyingModelUpdate = true;
}

void LiveEditBinding::endModelUpdate()
{
    m_applyingModelUpdate = false;
}

// ---------------------------------------------------------------------------
// Core: forward user edits to the document
// ---------------------------------------------------------------------------

void LiveEditBinding::onContentsChange(int qtPos, int charsRemoved, int charsAdded)
{
    // Suppress changes that come from our own model-driven text assignment.
    if (m_applyingModelUpdate) return;
    if (!m_document || !m_textDoc) return;

    // Resolve the block's byte range in the pre-change document state.
    // blockByteRange uses the most-recently-parsed AST; the block anchor is
    // a stable CRDT identity so it remains valid as long as the block exists.
    // DEFERRED: blockByteRange resolves against the last-parsed AST, which may lag
    // by one edit during rapid typing. The CRDT generally handles approximate
    // positions gracefully. A proper fix requires CRDT-native block resolution.
    const auto rangeOpt = m_document->blockByteRange(m_blockAnchor);
    if (!rangeOpt) return;  // block no longer in the parse (rare during structural edits)

    const quint32 blockStart = rangeOpt->first;

    // Qt fires contentsChange AFTER the document is modified.
    // m_document->toMarkdownUtf8() is still PRE-change because we haven't
    // called applyLocalEdit yet — same invariant as SourceTextDocumentBinding.
    //
    // We need the PRE-change text to convert (qtPos, charsRemoved) to byte
    // offsets. We cannot read it from m_textDoc (already post-change).
    // Instead, reconstruct the pre-change text from the foundation, which
    // has NOT yet been mutated (we haven't called applyLocalEdit yet).
    //
    // doc->toMarkdownUtf8() is the full document; we slice out the block's
    // bytes. This is the same approach used by SourceTextDocumentBinding.
    const QByteArray docBytes  = m_document->toMarkdownUtf8();
    const QString    docText   = QString::fromUtf8(docBytes);
    // Block text starts at a code-unit position we derive via byteOffset→qtPos.
    const int blockStartQtPos =
        Markoff::SourceTextDocumentBinding::byteOffsetToQtPos(docBytes, blockStart);

    // Absolute UTF-16 positions within the full document.
    const int absOldStart = blockStartQtPos + qtPos;
    const int absOldEnd   = blockStartQtPos + qtPos + charsRemoved;

    // Convert to UTF-8 byte offsets in the full document using the pre-change text.
    const quint32 oldStartByte =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, absOldStart);
    const quint32 oldEndByte   =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, absOldEnd);

    // Extract the inserted text from the POST-change QTextDocument.
    // Qt fires contentsChange AFTER modifying the document, so toPlainText()
    // already holds the new content. We read the inserted portion at qtPos.
    const QString postText    = m_textDoc->toPlainText();
    const QString insertedStr = postText.mid(qtPos, charsAdded);
    const QByteArray newText  = insertedStr.toUtf8();

    Markoff::MarkoffEdit ed;
    ed.oldStart = oldStartByte;
    ed.oldEnd   = oldEndByte;
    ed.newText  = newText;

    m_document->applyLocalEdit({ ed });

    const bool isPrintable = (charsRemoved == 0 && charsAdded == 1);

    if (isPrintable
        && m_lastEditWasPrintable
        && m_blockAnchor == m_lastEditAnchor
        && m_lastEditTimer.isValid()
        && m_lastEditTimer.elapsed() < 1000)
    {
        m_document->coalesceLastUndo();
    }

    m_lastEditWasPrintable = isPrintable;
    m_lastEditAnchor       = m_blockAnchor;
    m_lastEditTimer.restart();

    Q_EMIT editApplied(m_blockAnchor, m_textDoc->toPlainText());
}

}  // namespace Markoff::View::Qml
