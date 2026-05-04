// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveStructuralKeyHandler.h>

#include <Qt>

#include <markoff-foundation/MarkoffEdit.h>

#include <markoff/view/qml/LiveBlockModel.h>
#include <markoff/view/qml/LiveProjectionLayer.h>
#include <markoff/view/qml/ProjectionItem.h>

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

LiveProjectionLayer *LiveStructuralKeyHandler::projectionLayer() const
{
    return m_layer;
}

void LiveStructuralKeyHandler::setProjectionLayer(LiveProjectionLayer *layer)
{
    if (m_layer == layer) return;
    m_layer = layer;
    Q_EMIT projectionLayerChanged();
}

bool LiveStructuralKeyHandler::tryHandle(int key, int /*modifiers*/,
                                         const Markoff::BlockAnchor &rowAnchor,
                                         int blockIndex,
                                         int qtPos,
                                         bool selectionEmpty,
                                         const QString &blockText)
{
    if (!m_document || !m_model) return false;

    // Sanity-check: block exists in the most-recent parse.
    if (!m_document->blockByteRange(rowAnchor)) return false;

    // CRDT-current block start byte. blockByteRange gives the most-recent
    // parse position; we already verified it has_value() above.
    const quint32 currentBlockStart = m_document->blockByteRange(rowAnchor)->first;
    // Current block end = start + UTF-8 byte count of the TextEdit's current text.
    // blockText is passed from QML as textEdit.getText(0, textEdit.length), i.e.
    // the live TextEdit content rather than the stale model role.
    const quint32 currentBlockEnd = currentBlockStart
                                    + static_cast<quint32>(blockText.toUtf8().size());

    const int rowCount = m_model->rowCount();

    if (key == Qt::Key_Backspace && selectionEmpty && qtPos == 0) {
        // Merge with previous block by deleting the newline separator before block start.
        if (blockIndex <= 0 || currentBlockStart == 0) return false;
        Markoff::MarkoffEdit ed;
        ed.oldStart = currentBlockStart - 1;
        ed.oldEnd   = currentBlockStart;
        ed.newText  = QByteArray();
        m_document->applyLocalEdit({ ed });
        return true;
    }

    if (key == Qt::Key_Delete && selectionEmpty && qtPos == blockText.length()) {
        // Merge with next block by deleting the newline separator after block end.
        if (blockIndex >= rowCount - 1) return false;
        const quint32 docLength = m_document->visibleLength();
        if (currentBlockEnd >= docLength) return false;
        Markoff::MarkoffEdit ed;
        ed.oldStart = currentBlockEnd;
        ed.oldEnd   = currentBlockEnd + 1;
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
            // End-of-block Enter: open a transient projection-layer hole
            // (preedit pattern). The layer auto-displaces any prior pending
            // hole (commit if non-empty, drop if empty) before creating the
            // new one, giving "Enter starts the next paragraph" semantics.
            if (!m_layer) return false;
            Markoff::View::Qml::BlockHole h;
            h.kind = QStringLiteral("paragraph");
            h.reifyOffset = currentBlockEnd;
            h.afterParsedRow = blockIndex;
            // bufferText starts empty.
            m_layer->createBlockHole(h);
            return true;
        }

        if (qtPos > 0) {
            // Mid-block split: cursor byte offset = block start + UTF-8 bytes
            // in the block up to qtPos (UTF-16 code units).
            const quint32 prefixBytes =
                static_cast<quint32>(blockText.left(qtPos).toUtf8().size());
            const quint32 byteOffset = currentBlockStart + prefixBytes;

            Markoff::MarkoffEdit ed;
            ed.oldStart = byteOffset;
            ed.oldEnd   = byteOffset;
            ed.newText  = QByteArrayLiteral("\n\n");
            m_document->applyLocalEdit({ ed });
            // After the parse-back, the original block stays at `blockIndex`
            // with its text truncated to the prefix and a new "second half"
            // row is inserted at `blockIndex + 1`. The caret should land at
            // qtPos 0 in that new row (start of the next paragraph).
            const int expectedRow = blockIndex + 1;
            Q_EMIT focusAfterStructuralEdit(expectedRow, /*qtPos=*/0);
            return true;
        }

        // qtPos == 0: insert paragraph break before block start.
        Markoff::MarkoffEdit ed;
        ed.oldStart = currentBlockStart;
        ed.oldEnd   = currentBlockStart;
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
