// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveProxyBlockModel.h>

#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveHoleLayer.h>
#include <markoff/live-render/BlockKind.h>
#include <markoff-foundation/MarkoffDocument.h>

namespace Markoff::LiveRender {

LiveProxyBlockModel::LiveProxyBlockModel(Markoff::MarkoffDocument *doc,
                                          LiveBlockModel          *inner,
                                          LiveHoleLayer           *layer,
                                          QObject                 *parent)
    : QAbstractListModel(parent), m_doc(doc), m_inner(inner), m_layer(layer)
{
    connect(inner, &QAbstractItemModel::rowsInserted,
            this, &LiveProxyBlockModel::onInnerRowsInserted);
    connect(inner, &QAbstractItemModel::rowsRemoved,
            this, &LiveProxyBlockModel::onInnerRowsAboutToBeRemoved);
    connect(inner, &QAbstractItemModel::dataChanged,
            this, &LiveProxyBlockModel::onInnerDataChanged);
    connect(inner, &QAbstractItemModel::modelReset,
            this, &LiveProxyBlockModel::onInnerModelReset);
    connect(layer, &LiveHoleLayer::holeInserted,
            this, &LiveProxyBlockModel::onHoleInserted);
    connect(layer, &LiveHoleLayer::holeBufferChanged,
            this, &LiveProxyBlockModel::onHoleBufferChanged);
    connect(layer, &LiveHoleLayer::holeAbandoned,
            this, &LiveProxyBlockModel::onHoleAbandoned);

    // Load-different-file case: when the document is replaced via resetContent,
    // abandon all open holes so the proxy rows are consistent. The inner
    // LiveBlockModel uses targeted insert/remove/dataChanged signals (never
    // beginResetModel), so modelAboutToBeReset never fires on it. Instead we
    // wire to the MarkoffDocument::documentReloaded signal which is emitted
    // synchronously from resetContent before the new parse arrives.
    connect(doc, &Markoff::MarkoffDocument::documentReloaded,
            this, [this]() {
        const auto ids = m_layer->holesInOrder();
        for (quint64 id : ids) m_layer->abandonBlockHole(id);
    });

    rebuildMapping();
}

int LiveProxyBlockModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_rows.size();
}

QVariant LiveProxyBlockModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const auto &r = m_rows[index.row()];
    if (r.isHole) {
        switch (role) {
            case IsHoleRole:     return true;
            case BufferTextRole: return m_layer->bufferText(r.holeId);
            case HoleIdRole:     return QVariant::fromValue(r.holeId);
            case LiveBlockModel::TextRole:
                return m_layer->bufferText(r.holeId);
            case LiveBlockModel::KindRole:
                // Hole rows are always paragraph-shaped for the DelegateChooser.
                // ParagraphDelegate.isHole guards the difference in behaviour.
                return BlockKind::Paragraph;
            default: return {};
        }
    }
    if (role == IsHoleRole) return false;
    if (role == HoleIdRole) return QVariant::fromValue(quint64(0));
    return m_inner->data(m_inner->index(r.innerRow, 0), role);
}

QHash<int, QByteArray> LiveProxyBlockModel::roleNames() const {
    QHash<int, QByteArray> roles = m_inner->roleNames();
    roles[IsHoleRole]     = "isHole";
    roles[BufferTextRole] = "bufferText";
    roles[HoleIdRole]     = "holeId";
    return roles;
}

quint32 LiveProxyBlockModel::innerStartByteForRow(int r) const {
    const Markoff::BlockAnchor &anchor = m_inner->recordAt(r).blockAnchor;
    const auto range = m_doc->blockByteRange(anchor);
    return range ? range->first : 0;
}

void LiveProxyBlockModel::rebuildMapping() {
    beginResetModel();
    m_rows.clear();

    const QList<quint64> holeIds = m_layer->holesInOrder();
    int holeCursor = 0;

    auto holeBeforeInner = [&](int innerRow) -> bool {
        if (holeCursor >= holeIds.size()) return false;
        const Markoff::TextAnchor a = m_layer->reifyAnchor(holeIds[holeCursor]);
        const quint32 byte = m_doc->resolveTextAnchor(a);
        return byte <= innerStartByteForRow(innerRow);
    };

    for (int r = 0; r < m_inner->rowCount(); ++r) {
        while (holeBeforeInner(r)) {
            m_rows.append({true, -1, holeIds[holeCursor]});
            ++holeCursor;
        }
        m_rows.append({false, r, 0});
    }
    while (holeCursor < holeIds.size()) {
        m_rows.append({true, -1, holeIds[holeCursor]});
        ++holeCursor;
    }
    endResetModel();
}

void LiveProxyBlockModel::onInnerRowsInserted(const QModelIndex &, int, int) {
    rebuildMapping();
}
void LiveProxyBlockModel::onInnerRowsAboutToBeRemoved(const QModelIndex &, int, int) {
    rebuildMapping();
}
void LiveProxyBlockModel::onInnerDataChanged(const QModelIndex &tl,
                                              const QModelIndex &br,
                                              const QVector<int> &roles) {
    Q_EMIT dataChanged(index(proxyRowForInner(tl.row()), 0),
                        index(proxyRowForInner(br.row()), 0), roles);
}
void LiveProxyBlockModel::onInnerModelReset() {
    rebuildMapping();
}

void LiveProxyBlockModel::onHoleInserted(quint64 holeId) {
    // Determine target proxy row by computing where this hole sits in
    // the (now updated) holesInOrder ordering relative to existing rows.
    const Markoff::TextAnchor a = m_layer->reifyAnchor(holeId);
    const quint32 holeByte = m_doc->resolveTextAnchor(a);

    int targetProxyRow = m_rows.size();
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto &row = m_rows[i];
        if (row.isHole) {
            // Holes already in order; check tie-break.
            const Markoff::TextAnchor ha = m_layer->reifyAnchor(row.holeId);
            const quint32 hb = m_doc->resolveTextAnchor(ha);
            if (holeByte < hb || (holeByte == hb && holeId < row.holeId)) {
                targetProxyRow = i;
                break;
            }
        } else {
            const quint32 innerByte = innerStartByteForRow(row.innerRow);
            if (holeByte <= innerByte) {
                targetProxyRow = i;
                break;
            }
        }
    }

    beginResetModel();
    m_rows.insert(targetProxyRow, {true, -1, holeId});
    endResetModel();
}

void LiveProxyBlockModel::onHoleBufferChanged(quint64 holeId) {
    int proxyRow = proxyRowForHole(holeId);
    if (proxyRow < 0) return;
    Q_EMIT dataChanged(index(proxyRow, 0), index(proxyRow, 0),
                        {BufferTextRole, LiveBlockModel::TextRole});
}

void LiveProxyBlockModel::onHoleAbandoned(quint64 holeId) {
    int proxyRow = proxyRowForHole(holeId);
    if (proxyRow < 0) return;
    beginRemoveRows({}, proxyRow, proxyRow);
    m_rows.removeAt(proxyRow);
    endRemoveRows();
}

int LiveProxyBlockModel::innerRowForProxy(int proxyRow) const {
    if (proxyRow < 0 || proxyRow >= m_rows.size() || m_rows[proxyRow].isHole)
        return -1;
    return m_rows[proxyRow].innerRow;
}
int LiveProxyBlockModel::proxyRowForInner(int innerRow) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (!m_rows[i].isHole && m_rows[i].innerRow == innerRow)
            return i;
    return -1;
}
int LiveProxyBlockModel::proxyRowForHole(quint64 holeId) const {
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].isHole && m_rows[i].holeId == holeId)
            return i;
    return -1;
}
bool LiveProxyBlockModel::proxyRowIsHole(int proxyRow) const noexcept {
    return proxyRow >= 0 && proxyRow < m_rows.size() && m_rows[proxyRow].isHole;
}
quint64 LiveProxyBlockModel::holeAtProxyRow(int proxyRow) const noexcept {
    if (!proxyRowIsHole(proxyRow)) return 0;
    return m_rows[proxyRow].holeId;
}

}  // namespace Markoff::LiveRender
