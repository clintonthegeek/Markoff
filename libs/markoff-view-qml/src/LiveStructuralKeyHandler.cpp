// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveStructuralKeyHandler.h>

#include <Qt>

#include <markoff-foundation/MarkoffEdit.h>
#include <markoff-foundation/SourceTextDocumentBinding.h>

#include <markoff/view/qml/LiveBlockModel.h>

namespace Markoff::View::Qml {

LiveStructuralKeyHandler::LiveStructuralKeyHandler(QObject *parent)
    : QObject(parent)
{}

Markoff::MarkoffDocument *LiveStructuralKeyHandler::document() const
{
    return m_document;
}

void LiveStructuralKeyHandler::setDocument(Markoff::MarkoffDocument *doc)
{
    if (m_document == doc) return;
    m_document = doc;
    Q_EMIT documentChanged();
}

LiveBlockModel *LiveStructuralKeyHandler::model() const
{
    return m_model;
}

void LiveStructuralKeyHandler::setModel(LiveBlockModel *m)
{
    if (m_model == m) return;
    m_model = m;
    Q_EMIT modelChanged();
}

bool LiveStructuralKeyHandler::tryHandle(int key, int /*modifiers*/,
                                         const Markoff::BlockAnchor &rowAnchor,
                                         int blockIndex,
                                         int qtPos,
                                         bool selectionEmpty,
                                         const QString &blockText)
{
    if (!m_document || !m_model) return false;

    const auto rangeOpt = m_document->blockByteRange(rowAnchor);
    if (!rangeOpt) return false;

    const quint32 blockStart = rangeOpt->first;
    const quint32 blockEnd   = rangeOpt->second;  // exclusive of content; \n\n separator follows

    const int rowCount = m_model->rowCount();

    if (key == Qt::Key_Backspace && selectionEmpty && qtPos == 0) {
        // Merge with previous block by deleting the newline separator before blockStart.
        if (blockIndex <= 0 || blockStart == 0) return false;
        Markoff::MarkoffEdit ed;
        ed.oldStart = blockStart - 1;
        ed.oldEnd   = blockStart;
        ed.newText  = QByteArray();
        m_document->applyLocalEdit({ ed });
        return true;
    }

    if (key == Qt::Key_Delete && selectionEmpty && qtPos == blockText.length()) {
        // Merge with next block by deleting the newline separator after blockEnd.
        if (blockIndex >= rowCount - 1) return false;
        const quint32 docLength = m_document->visibleLength();
        if (blockEnd >= docLength) return false;
        Markoff::MarkoffEdit ed;
        ed.oldStart = blockEnd;
        ed.oldEnd   = blockEnd + 1;
        ed.newText  = QByteArray();
        m_document->applyLocalEdit({ ed });
        return true;
    }

    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        // Check whether this block is a code_block — pass through if so.
        const QString kind = m_model->data(
            m_model->index(blockIndex, 0),
            m_model->roleForName("kind")
        ).toString();

        if (kind == QStringLiteral("code_block")) return false;

        if (qtPos == blockText.length()) {
            // At the end of a non-code block: insert paragraph break after block.
            Markoff::MarkoffEdit ed;
            ed.oldStart = blockEnd;
            ed.oldEnd   = blockEnd;
            ed.newText  = QByteArrayLiteral("\n\n");
            m_document->applyLocalEdit({ ed });
            return true;
        }

        if (qtPos > 0) {
            // Mid-block: split by inserting paragraph break at cursor byte offset.
            // Convert block-local qtPos to a document-global byte offset.
            const QByteArray docBytes   = m_document->toMarkdownUtf8();
            const QString    docText    = QString::fromUtf8(docBytes);
            const int blockStartQtPos   =
                Markoff::SourceTextDocumentBinding::byteOffsetToQtPos(docBytes, blockStart);
            const quint32 byteOffset    =
                Markoff::SourceTextDocumentBinding::qtPosToByteOffset(
                    docText, blockStartQtPos + qtPos);

            Markoff::MarkoffEdit ed;
            ed.oldStart = byteOffset;
            ed.oldEnd   = byteOffset;
            ed.newText  = QByteArrayLiteral("\n\n");
            m_document->applyLocalEdit({ ed });
            return true;
        }

        // qtPos == 0: insert paragraph break before block start.
        Markoff::MarkoffEdit ed;
        ed.oldStart = blockStart;
        ed.oldEnd   = blockStart;
        ed.newText  = QByteArrayLiteral("\n\n");
        m_document->applyLocalEdit({ ed });
        return true;
    }

    if (key == Qt::Key_Tab) {
        const QString kind = m_model->data(
            m_model->index(blockIndex, 0),
            m_model->roleForName("kind")
        ).toString();
        if (kind == QStringLiteral("code_block")) return false;
        // Tab outside code_block: also pass through (not consumed here).
        return false;
    }

    return false;
}

void LiveStructuralKeyHandler::insertFirstCharacter(const QString &text)
{
    if (!m_document || text.isEmpty()) return;
    Markoff::MarkoffEdit ed;
    ed.oldStart = 0;
    ed.oldEnd   = 0;
    ed.newText  = text.toUtf8();
    m_document->applyLocalEdit({ ed });
}

}  // namespace Markoff::View::Qml
