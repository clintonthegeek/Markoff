// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveHoleLayer.h>

#include <markoff-foundation/MarkoffDocument.h>

#include <algorithm>

namespace Markoff::LiveRender {

LiveHoleLayer::LiveHoleLayer(Markoff::MarkoffDocument *doc,
                             LiveBlockModel    *blockModel,
                             UndoCoalescer     *undoCoalescer,
                             QObject           *parent)
    : QObject(parent),
      m_doc(doc),
      m_blockModel(blockModel),
      m_undoCoalescer(undoCoalescer)
{}

LiveHoleLayer::~LiveHoleLayer() = default;

quint64 LiveHoleLayer::createBlockHole(HoleKind kind,
                                        Markoff::TextAnchor reifyAnchor) {
    HoleEntry entry;
    entry.kind = kind;
    entry.reifyAnchor = reifyAnchor;

    quint64 id = m_nextHoleId++;
    m_holes.insert(id, entry);
    Q_EMIT holeInserted(id);
    return id;
}

void LiveHoleLayer::setBlockHoleBuffer(quint64 holeId, const QString &text) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    if (it->bufferText == text) return;
    it->bufferText = text;
    Q_EMIT holeBufferChanged(holeId);
}

void LiveHoleLayer::abandonBlockHole(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    m_holes.erase(it);
    Q_EMIT holeAbandoned(holeId);
}

int LiveHoleLayer::holeCount() const noexcept { return m_holes.size(); }

bool LiveHoleLayer::exists(quint64 holeId) const noexcept {
    return m_holes.contains(holeId);
}

HoleKind LiveHoleLayer::kind(quint64 holeId) const {
    return m_holes.value(holeId).kind;
}

QString LiveHoleLayer::bufferText(quint64 holeId) const {
    return m_holes.value(holeId).bufferText;
}

Markoff::TextAnchor LiveHoleLayer::reifyAnchor(quint64 holeId) const {
    return m_holes.value(holeId).reifyAnchor;
}

QList<quint64> LiveHoleLayer::holesInOrder() const {
    // Order by reifyAnchor's currently-resolved byte offset; ties broken
    // by holeId (stable + deterministic).
    QList<quint64> ids;
    ids.reserve(m_holes.size());
    for (auto it = m_holes.begin(); it != m_holes.end(); ++it)
        ids.append(it.key());
    std::sort(ids.begin(), ids.end(), [&](quint64 a, quint64 b) {
        const quint32 ba = m_doc->resolveTextAnchor(m_holes[a].reifyAnchor);
        const quint32 bb = m_doc->resolveTextAnchor(m_holes[b].reifyAnchor);
        if (ba != bb) return ba < bb;
        return a < b;
    });
    return ids;
}

}  // namespace Markoff::LiveRender
