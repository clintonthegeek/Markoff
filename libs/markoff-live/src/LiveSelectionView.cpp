// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveSelectionView.h>
#include <markoff/live/Coordinates.h>
#include <markoff/live/LiveBlockModel.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>

#include <QApplication>
#include <QClipboard>
#include <climits>

namespace Markoff::Live {

LiveSelectionView::LiveSelectionView(QObject *parent) : QObject(parent) {}

void LiveSelectionView::setDocument(Markoff::MarkoffDocument *doc) { m_document = doc; }
void LiveSelectionView::setSession(Markoff::Session *session)       { m_session  = session; }
void LiveSelectionView::setModel(const LiveBlockModel *model)       { m_model    = model; }

bool LiveSelectionView::hasSelection() const
{
    return m_anchorBlock >= 0 && m_activeBlock >= 0
        && !(m_anchorBlock == m_activeBlock && m_anchorQtPos == m_activeQtPos);
}

void LiveSelectionView::begin(int blockIndex, int qtPos)
{
    m_anchorBlock = blockIndex; m_anchorQtPos = qtPos;
    m_activeBlock = blockIndex; m_activeQtPos = qtPos;
    syncToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::extend(int blockIndex, int qtPos)
{
    m_activeBlock = blockIndex;
    m_activeQtPos = qtPos;
    syncToSession();
    Q_EMIT selectionChanged();
}

void LiveSelectionView::clear()
{
    if (m_anchorBlock < 0) return;
    m_anchorBlock = m_anchorQtPos = m_activeBlock = m_activeQtPos = -1;
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
    if (m_anchorBlock < 0 || m_activeBlock < 0)
        return QPoint(-1, -1);

    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);

    if (blockIndex < fb || blockIndex > lb)
        return QPoint(-1, -1);

    if (fb == lb)
        return QPoint(qMin(fo, lo), qMax(fo, lo));

    if (blockIndex == fb) return QPoint(fo, INT_MAX);
    if (blockIndex == lb) return QPoint(0, lo);
    return QPoint(0, INT_MAX);  // intermediate: whole block
}

void LiveSelectionView::copyToClipboard() const
{
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

    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);

    const int rowCount = m_model->rowCount();
    if (fb < 0 || fb >= rowCount || lb < 0 || lb >= rowCount) return;

    // Compute flat byte start/end by walking iterateBlocks().
    // applyFlatEdit uses the same cumulative-blockText walk internally, so
    // the byte offsets we produce here are in the same coordinate space.
    const auto blocks = m_document->iterateBlocks();

    uint32_t startByte = 0;
    uint32_t endByte   = 0;
    uint32_t cursor    = 0;
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        const QByteArray rawText = m_document->blockText(blocks[i]);
        const uint32_t blockSize = static_cast<uint32_t>(rawText.size());

        if (i == fb) {
            // Model text has the trailing '\n' stripped; qtPos is within
            // the content portion. Use the model record for the conversion.
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
