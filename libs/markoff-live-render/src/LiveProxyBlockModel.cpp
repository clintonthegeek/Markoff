// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveProxyBlockModel.h>

#include <markoff/live-render/LiveBlockModel.h>
#include <markoff/live-render/LiveHoleLayer.h>

namespace Markoff::LiveRender {

LiveProxyBlockModel::LiveProxyBlockModel(LiveBlockModel *inner,
                                          LiveHoleLayer  *layer,
                                          QObject        *parent)
    : QAbstractListModel(parent), m_inner(inner), m_layer(layer)
{
    connect(inner, &QAbstractItemModel::rowsInserted,
            this, &LiveProxyBlockModel::onInnerRowsInserted);
    connect(inner, &QAbstractItemModel::rowsAboutToBeRemoved,
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

void LiveProxyBlockModel::rebuildMapping() {
    beginResetModel();
    m_rows.clear();
    for (int r = 0; r < m_inner->rowCount(); ++r)
        m_rows.append({false, r, 0});
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

void LiveProxyBlockModel::onHoleInserted(quint64) { rebuildMapping(); }
void LiveProxyBlockModel::onHoleBufferChanged(quint64) {}
void LiveProxyBlockModel::onHoleAbandoned(quint64) { rebuildMapping(); }

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
