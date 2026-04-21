// SPDX-License-Identifier: GPL-3.0-or-later
#include "InMemoryCanonicalBuffer.h"

namespace Markoff {

InMemoryCanonicalBuffer::InMemoryCanonicalBuffer() = default;
InMemoryCanonicalBuffer::~InMemoryCanonicalBuffer() = default;

const QString &InMemoryCanonicalBuffer::toMarkdown() const { return m_text; }
qsizetype      InMemoryCanonicalBuffer::length() const     { return m_text.size(); }

QString InMemoryCanonicalBuffer::substring(qsizetype offset, qsizetype len) const {
    return m_text.mid(offset, len);
}

void InMemoryCanonicalBuffer::applyDelta(qsizetype offset,
                                         qsizetype removedLength,
                                         const QString &inserted) {
    m_text.replace(offset, removedLength, inserted);

    const qsizetype delta       = inserted.size() - removedLength;
    const qsizetype deleteStart = offset;
    const qsizetype deleteEnd   = offset + removedLength;
    const qsizetype insertEnd   = offset + inserted.size();

    for (auto it = m_anchors.begin(); it != m_anchors.end(); ++it) {
        Anchor &a = it.value();
        if (a.offset <= deleteStart) {
            // Anchor precedes (or sits at) the start of the edit — unaffected.
        } else if (a.offset >= deleteEnd) {
            // Anchor follows the edit — shift by net delta.
            a.offset += delta;
        } else {
            // Anchor straddles the removed region — clamp per bias.
            a.offset = (a.bias == CursorBias::Left) ? deleteStart : insertEnd;
        }
    }
}

void InMemoryCanonicalBuffer::reset(const QString &newContent) {
    m_text = newContent;
    m_anchors.clear();
}

quint64 InMemoryCanonicalBuffer::createAnchor(qsizetype offset, CursorBias bias) {
    const quint64 h = m_nextHandle++;
    m_anchors.insert(h, {offset, bias});
    return h;
}

qsizetype InMemoryCanonicalBuffer::resolveAnchor(quint64 handle) const {
    const auto it = m_anchors.constFind(handle);
    return (it == m_anchors.constEnd()) ? qsizetype(-1) : it->offset;
}

void InMemoryCanonicalBuffer::releaseAnchor(quint64 handle) {
    m_anchors.remove(handle);
}

} // namespace Markoff
