// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveSelectionView.h>
#include <markoff/live-render/Coordinates.h>
#include <markoff/live-render/LiveBlockModel.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>

#include <QApplication>
#include <QClipboard>
#include <climits>

namespace Markoff::LiveRender {

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

void LiveSelectionView::copyToClipboard(const QStringList &blockTexts) const
{
    if (!hasSelection()) return;

    int fb, fo, lb, lo;
    normalized(fb, fo, lb, lo);

    QString text;
    for (int i = fb; i <= lb && i < blockTexts.size(); ++i) {
        const QString &bt = blockTexts[i];
        const int start = (i == fb) ? fo : 0;
        const int end   = qMin((i == lb) ? lo : bt.length(), bt.length());
        if (start > end) continue;
        if (!text.isEmpty()) text += QLatin1Char('\n');
        text += bt.mid(start, end - start);
    }

    QApplication::clipboard()->setText(text);
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
    m_session->setPrimarySelection(sel);
}

}  // namespace Markoff::LiveRender
