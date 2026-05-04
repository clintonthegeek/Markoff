// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveHoleLayer.h>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/MarkoffEdit.h>

#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <algorithm>

namespace Markoff::LiveRender {

LiveHoleLayer::LiveHoleLayer(Markoff::MarkoffDocument *doc,
                             LiveBlockModel    *blockModel,
                             QObject           *parent)
    : QObject(parent),
      m_doc(doc),
      m_blockModel(blockModel)
{}

LiveHoleLayer::~LiveHoleLayer() = default;

quint64 LiveHoleLayer::createBlockHole(HoleKind kind,
                                        Markoff::TextAnchor reifyAnchor) {
    HoleEntry entry;
    entry.kind = kind;
    entry.reifyAnchor = reifyAnchor;

    quint64 id = m_nextHoleId++;
    auto *t = new QTimer(this);
    t->setSingleShot(true);
    t->setInterval(kIdleCommitMs);
    connect(t, &QTimer::timeout, this, [this, id]() {
        qInfo().noquote() << "[dogfood] HoleLayer: idleCommitDue id=" << id;
        Q_EMIT idleCommitDue(id);
    });
    entry.idleTimer = t;
    m_holes.insert(id, entry);
    qInfo().noquote() << "[dogfood] HoleLayer: createBlockHole id=" << id
                      << "kind=" << int(kind)
                      << "reifyByte=" << m_doc->resolveTextAnchor(reifyAnchor);
    Q_EMIT holeInserted(id);
    return id;
}

void LiveHoleLayer::setBlockHoleBuffer(quint64 holeId, const QString &text) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    if (it->bufferText == text) return;

    // 1-second coalesce-break for undo grouping: if more than 1000 ms
    // since the last edit, snapshot the pre-edit buffer onto undoStack
    // before overwriting. Consecutive edits within the threshold
    // collapse into one undo entry (matches UndoCoalescer's printable-
    // coalesce policy).
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (it->lastEditMs == 0) {
        // First edit on a fresh hole — snapshot so undoing the first
        // character returns to empty buffer rather than nothing.
        it->undoStack.push(it->bufferText);
        it->redoStack.clear();
    } else if (now - it->lastEditMs > 1000) {
        it->undoStack.push(it->bufferText);
        it->redoStack.clear();
    }
    it->lastEditMs = now;

    it->bufferText = text;
    qInfo().noquote() << "[dogfood] HoleLayer: setBlockHoleBuffer id=" << holeId
                      << "text=" << text.left(40)
                      << "len=" << text.length()
                      << "composing=" << it->composing;
    Q_EMIT holeBufferChanged(holeId);

    if (!it->composing && !text.isEmpty())
        restartIdleTimer(holeId);
    else
        stopIdleTimer(holeId);
}

void LiveHoleLayer::setHoleComposition(quint64 holeId, bool composing) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    if (it->composing == composing) return;
    it->composing = composing;
    if (composing)
        stopIdleTimer(holeId);
    else if (!it->bufferText.isEmpty())
        restartIdleTimer(holeId);
}

void LiveHoleLayer::abandonBlockHole(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    qInfo().noquote() << "[dogfood] HoleLayer: abandonBlockHole id=" << holeId
                      << "buffer=" << it->bufferText.left(40);
    if (it->idleTimer) it->idleTimer->deleteLater();
    m_holes.erase(it);
    Q_EMIT holeAbandoned(holeId);
}

void LiveHoleLayer::restartIdleTimer(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end() || !it->idleTimer) return;
    it->idleTimer->start();
}

void LiveHoleLayer::stopIdleTimer(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end() || !it->idleTimer) return;
    it->idleTimer->stop();
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

void LiveHoleLayer::recordHoleUndoPoint(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return;
    it->undoStack.push(it->bufferText);
    it->redoStack.clear();
}

bool LiveHoleLayer::undoBlockHole(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) return false;

    if (it->undoStack.isEmpty() && it->bufferText.isEmpty())
        return false;   // signal: drop the hole

    if (it->undoStack.isEmpty()) {
        // Current buffer is the only state; clear it and push to redo.
        it->redoStack.push(it->bufferText);
        it->bufferText.clear();
    } else {
        it->redoStack.push(it->bufferText);
        it->bufferText = it->undoStack.pop();
    }
    Q_EMIT holeBufferChanged(holeId);
    return true;
}

bool LiveHoleLayer::redoBlockHole(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end() || it->redoStack.isEmpty()) return false;
    it->undoStack.push(it->bufferText);
    it->bufferText = it->redoStack.pop();
    Q_EMIT holeBufferChanged(holeId);
    return true;
}

void LiveHoleLayer::commitBlockHole(quint64 holeId) {
    auto it = m_holes.find(holeId);
    if (it == m_holes.end()) {
        qInfo().noquote() << "[dogfood] HoleLayer: commitBlockHole id=" << holeId
                          << "NO-OP (already gone)";
        return;
    }

    const QString buffer = it->bufferText;
    const Markoff::TextAnchor reifyAnchor = it->reifyAnchor;

    if (buffer.isEmpty()) {
        qInfo().noquote() << "[dogfood] HoleLayer: commitBlockHole id=" << holeId
                          << "EMPTY -> abandon";
        if (it->idleTimer) it->idleTimer->deleteLater();
        m_holes.erase(it);
        Q_EMIT holeAbandoned(holeId);
        return;
    }

    const quint32 reifyByte = m_doc->resolveTextAnchor(reifyAnchor);
    const QByteArray insertion = (QStringLiteral("\n\n") + buffer).toUtf8();

    qInfo().noquote() << "[dogfood] HoleLayer: commitBlockHole id=" << holeId
                      << "buffer=" << buffer.left(40)
                      << "reifyByte=" << reifyByte
                      << "insertionLen=" << insertion.size();

    // Fire BEFORE the hole row vanishes so listeners can capture proxy-row
    // and bufferText.length() for post-commit cursor delivery.
    Q_EMIT aboutToCommit(holeId);

    if (it->idleTimer) it->idleTimer->deleteLater();
    m_holes.erase(it);
    Q_EMIT holeAbandoned(holeId);

    Markoff::MarkoffEdit edit;
    edit.oldStart = reifyByte;
    edit.oldEnd   = reifyByte;
    edit.newText  = insertion;
    m_doc->applyLocalEdit({ edit });

    Markoff::TextAnchor newRowAnchor = m_doc->textAnchorAt(reifyByte + 2, /*rightBias=*/false);
    qInfo().noquote() << "[dogfood] HoleLayer: holeReified id=" << holeId
                      << "newRowByte=" << m_doc->resolveTextAnchor(newRowAnchor);
    Q_EMIT holeReified(holeId, newRowAnchor);
}

void LiveHoleLayer::commitAllPendingHoles() {
    // Commit in ascending reifyAnchor byte order. Anchors survive each
    // edit (TextAnchor handles byte-shift through CRDT identity).
    const QList<quint64> ids = holesInOrder();
    for (quint64 id : ids) commitBlockHole(id);
}

}  // namespace Markoff::LiveRender
