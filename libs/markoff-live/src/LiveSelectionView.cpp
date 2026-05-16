// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/Coordinates.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>

#include <QApplication>
#include <QClipboard>
#include <climits>

namespace Markoff::Live {

LiveSelectionView::LiveSelectionView(QObject *parent) : QObject(parent) {}

void LiveSelectionView::setDocument(Markoff::MarkoffDocument *doc) { m_document = doc; }

void LiveSelectionView::setSession(Markoff::Session *session)
{
    if (m_session == session) return;
    if (m_session)
        QObject::disconnect(m_session, &Markoff::Session::primarySelectionChanged,
                            this, &LiveSelectionView::onSessionPrimarySelectionChanged);
    m_session = session;
    if (m_session)
        QObject::connect(m_session, &Markoff::Session::primarySelectionChanged,
                         this, &LiveSelectionView::onSessionPrimarySelectionChanged);
}

void LiveSelectionView::setModel(const LiveBlockModel *model)       { m_model    = model; }

bool LiveSelectionView::hasSelection() const
{
    if (m_cursorState) return m_cursorState->hasSelection();
    return m_anchorBlock >= 0 && m_activeBlock >= 0
        && !(m_anchorBlock == m_activeBlock && m_anchorQtPos == m_activeQtPos);
}

int LiveSelectionView::anchorBlock() const
{
    if (m_cursorState) {
        const auto a = m_cursorState->selectionAnchor();
        if (!a) return -1;
        return m_cursorState->rowForBlock(a->block);
    }
    return m_anchorBlock;
}

int LiveSelectionView::anchorQtPos() const
{
    if (m_cursorState) {
        const auto a = m_cursorState->selectionAnchor();
        if (!a) return -1;
        return static_cast<int>(a->qtPos);
    }
    return m_anchorQtPos;
}

int LiveSelectionView::activeBlock() const
{
    if (m_cursorState) {
        const auto tc = m_cursorState->currentTextCaret();
        if (!tc) return -1;
        return m_cursorState->rowForBlock(tc->block);
    }
    return m_activeBlock;
}

int LiveSelectionView::activeQtPos() const
{
    if (m_cursorState) {
        const auto tc = m_cursorState->currentTextCaret();
        if (!tc) return -1;
        return static_cast<int>(tc->cachedQtPos);
    }
    return m_activeQtPos;
}

void LiveSelectionView::begin(int blockIndex, int qtPos)
{
    m_anchorBlock = blockIndex; m_anchorQtPos = qtPos;
    m_activeBlock = blockIndex; m_activeQtPos = qtPos;

    // FALSIFIABILITY PROOF, REVERTS NEXT — skip the canonical store write.
    // The shadow + canonical states will diverge; tests that consume the
    // canonical path should fail.
    // if (m_cursorState && m_model && blockIndex >= 0 && blockIndex < m_model->rowCount()) {
    //     const auto anchor = m_model->recordAt(blockIndex).blockAnchor;
    //     m_cursorState->establishFocus(anchor, qtPos);
    //     m_cursorState->setSelectionAnchor({anchor, static_cast<quint32>(qtPos)});
    // }

    syncToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::extend(int blockIndex, int qtPos)
{
    m_activeBlock = blockIndex;
    m_activeQtPos = qtPos;

    // Tier 4c: mirror active-end change to canonical store.
    // Anchor stays where begin() parked it.
    if (m_cursorState && m_model
        && blockIndex >= 0 && blockIndex < m_model->rowCount()) {
        const auto anchor = m_model->recordAt(blockIndex).blockAnchor;
        m_cursorState->establishFocus(anchor, qtPos);
    }

    syncToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::clear()
{
    if (m_anchorBlock < 0) return;
    m_anchorBlock = m_anchorQtPos = m_activeBlock = m_activeQtPos = -1;

    // Tier 4c: clear canonical anchor (active end unchanged).
    if (m_cursorState) m_cursorState->clearSelectionAnchor();

    Q_EMIT selectionChanged();
}

void LiveSelectionView::normalized(int &fb, int &fo, int &lb, int &lo) const
{
    if (m_anchorBlock < m_activeBlock
        || (m_anchorBlock == m_activeBlock && m_anchorQtPos <= m_activeQtPos)) {
        fb = m_anchorBlock; fo = m_anchorQtPos;
        lb = m_activeBlock; lo = m_activeQtPos;
    } else {
        fb = m_activeBlock; fo = m_activeQtPos;
        lb = m_anchorBlock; lo = m_anchorQtPos;
    }
}

QPoint LiveSelectionView::rangeForBlock(int blockIndex) const
{
    if (m_cursorState) return m_cursorState->selectionRangeForBlock(blockIndex);

    // Legacy fallback (unwired cursorState — should not happen in production).
    if (m_anchorBlock < 0 || m_activeBlock < 0) return QPoint(-1, -1);
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    if (blockIndex < fb || blockIndex > lb) return QPoint(-1, -1);
    if (fb == lb) return QPoint(qMin(fo, lo), qMax(fo, lo));
    if (blockIndex == fb) return QPoint(fo, INT_MAX);
    if (blockIndex == lb) return QPoint(0, lo);
    return QPoint(0, INT_MAX);
}

void LiveSelectionView::copyToClipboard() const
{
    if (m_cursorState) { m_cursorState->copySelectionToClipboard(); return; }

    // Legacy fallback (unwired cursorState — should not happen in production).
    if (!hasSelection() || !m_model) return;
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    const int rowCount = m_model->rowCount();
    QString text;
    for (int i = fb; i <= lb && i < rowCount; ++i) {
        const QString bt = m_model->recordAt(i).text;
        const int start = (i == fb) ? fo : 0;
        const int end   = qMin((i == lb) ? lo : bt.length(), bt.length());
        if (start > end) continue;
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text += bt.mid(start, end - start);
    }
    QApplication::clipboard()->setText(text);
}

void LiveSelectionView::selectAll()
{
    if (m_cursorState) {
        m_cursorState->selectAllBlocks();
        // Mirror back to local state so anchorBlock() etc. still report
        // correctly during the dual-store window. Phase D drops these.
        if (m_model && m_model->rowCount() > 0) {
            m_anchorBlock = 0;
            m_anchorQtPos = 0;
            m_activeBlock = m_model->rowCount() - 1;
            m_activeQtPos = m_model->recordAt(m_activeBlock).text.length();
        }
        syncToSession();
        Q_EMIT selectionChanged();
        return;
    }

    // Legacy fallback (unwired cursorState — should not happen in production).
    if (!m_model) return;
    const int rowCount = m_model->rowCount();
    if (rowCount <= 0) return;
    const int lastRow = rowCount - 1;
    const QString lastText = m_model->recordAt(lastRow).text;
    m_anchorBlock = 0;
    m_anchorQtPos = 0;
    m_activeBlock = lastRow;
    m_activeQtPos = lastText.length();
    syncToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::deleteSelection()
{
    if (!hasSelection() || !m_model || !m_document) return;

    // Tier 4c: delegate to canonical store. The two stores are in
    // sync at this point (begin/extend/clear all dual-write); the
    // canonical-store implementation performs the same flat-edit.
    if (m_cursorState) {
        m_cursorState->deleteSelectionRange();
        // The canonical store's clearSelectionAnchor was called inside
        // deleteSelectionRange. Mirror back to local state.
        m_anchorBlock = m_anchorQtPos = m_activeBlock = m_activeQtPos = -1;
        Q_EMIT selectionChanged();
        return;
    }

    // Fallback: legacy path (no cursorState wired — should not happen
    // in production after the binding pimpl wires setCursorState).
    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);
    const int rowCount = m_model->rowCount();
    if (fb < 0 || fb >= rowCount || lb < 0 || lb >= rowCount) return;
    const auto blocks = m_document->iterateBlocks();
    uint32_t startByte = 0, endByte = 0, cursor = 0;
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        const QByteArray rawText = m_document->blockText(blocks[i]);
        const uint32_t blockSize = static_cast<uint32_t>(rawText.size());
        if (i == fb) {
            const QByteArray modelUtf8 = m_model->recordAt(fb).text.toUtf8();
            startByte = cursor + static_cast<uint32_t>(
                Coordinates::qtPosToByte(modelUtf8, fo));
        }
        if (i == lb) {
            const QByteArray modelUtf8 = m_model->recordAt(lb).text.toUtf8();
            endByte = cursor + static_cast<uint32_t>(
                Coordinates::qtPosToByte(modelUtf8, lo));
            break;
        }
        cursor += blockSize;
    }
    if (endByte <= startByte) return;
    m_document->applyFlatEdit(startByte, endByte, QByteArray(), Markoff::Origin::UserEdit);
    clear();
}

void LiveSelectionView::onSessionPrimarySelectionChanged(const Markoff::Selection &sel)
{
    // Reentrancy guard: we ourselves called setPrimarySelection in syncToSession();
    // don't echo back.
    if (m_applyingSessionSelection) return;
    if (!m_document || !m_model) return;

    // Helper: resolve a TextAnchor → (blockRowIndex, qtPos).
    // TextAnchor carries its BlockId directly via t.block(), so we don't need
    // the (stale) latestBlockRanges-based blockAt() API.
    // Returns {-1, -1} if the anchor's block is not currently in the model
    // (orphaned anchor → callers must clear selection).
    const auto resolve = [&](const Markoff::TextAnchor &ta) -> std::pair<int,int> {
        if (ta.isNull()) return {-1, -1};
        const Markoff::BlockAnchor ba = ta.block();  // BlockAnchor == BlockId

        // Find the row in the model that has this BlockId.
        const int rowCount = m_model->rowCount();
        int row = -1;
        for (int i = 0; i < rowCount; ++i) {
            if (m_model->recordAt(i).blockAnchor == ba) {
                row = i;
                break;
            }
        }
        if (row < 0) return {-1, -1};

        // offsetInBlock() uses the per-block CRDT buffer directly (D2 path),
        // which is always current. Clamp to [0, model-text-byte-length].
        const int byteOff = m_document->offsetInBlock(ba, ta);
        const QByteArray utf8 = m_model->recordAt(row).text.toUtf8();
        const int clamped = qBound(0, byteOff, static_cast<int>(utf8.size()));
        const int qtPos   = static_cast<int>(Coordinates::byteToQtPos(utf8, clamped));
        return {row, qtPos};
    };

    const auto [anchorRow, anchorQtPos] = resolve(sel.anchor);
    const auto [activeRow,  activeQtPos] = resolve(sel.active);

    if (anchorRow < 0 || activeRow < 0) {
        // Orphaned anchor: clear selection gracefully.
        if (m_anchorBlock >= 0) {
            m_anchorBlock = m_anchorQtPos = m_activeBlock = m_activeQtPos = -1;
            Q_EMIT selectionChanged();
        }
        return;
    }

    if (m_anchorBlock == anchorRow && m_anchorQtPos == anchorQtPos
        && m_activeBlock == activeRow && m_activeQtPos == activeQtPos)
        return;  // No change.

    m_anchorBlock  = anchorRow;
    m_anchorQtPos  = anchorQtPos;
    m_activeBlock  = activeRow;
    m_activeQtPos  = activeQtPos;
    Q_EMIT selectionChanged();
}

void LiveSelectionView::syncToSession()
{
    if (!m_session || !m_document || !m_model) return;
    if (m_anchorBlock < 0 || m_anchorBlock >= m_model->rowCount()) return;
    if (m_activeBlock < 0 || m_activeBlock >= m_model->rowCount()) return;

    const auto makeAnchor = [&](int blockIdx, int qtPos) -> Markoff::TextAnchor {
        const BlockRecord &rec = m_model->recordAt(blockIdx);
        const QByteArray utf8  = rec.text.toUtf8();
        const int byteOff = static_cast<int>(
            Coordinates::qtPosToByte(utf8, qMax(0, qtPos)));
        return m_document->textAnchorAt(rec.blockAnchor, byteOff, /*rightBias=*/true);
    };

    Markoff::Selection sel;
    sel.kind   = Markoff::Selection::Kind::Primary;
    sel.anchor = makeAnchor(m_anchorBlock, m_anchorQtPos);
    sel.active = makeAnchor(m_activeBlock, m_activeQtPos);
    m_applyingSessionSelection = true;
    m_session->setPrimarySelection(sel);
    m_applyingSessionSelection = false;
}

}  // namespace Markoff::Live
