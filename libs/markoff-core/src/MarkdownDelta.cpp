// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/MarkdownDelta.h>
#include <markoff/MarkoffDocument.h>

namespace Markoff {

static constexpr int kMarkdownDeltaCommandId = 0x4D44;  // "MD"

MarkdownDelta::MarkdownDelta(MarkoffDocument *doc,
                             qsizetype offset,
                             qsizetype removedLength,
                             QString inserted,
                             QUndoCommand *parent)
    : QUndoCommand(parent),
      m_doc(doc),
      m_offset(offset),
      m_inserted(std::move(inserted)),
      m_removedLengthHint(removedLength) {
    Q_ASSERT(doc != nullptr);
    // m_removed is initially empty; sized on first redo() call when we
    // capture the text being replaced from the canonical buffer so undo()
    // can replay it.
    m_removed.reserve(removedLength);
    // Store the expected removed length so first redo() knows how much to snapshot.
    // We use m_removed.size() post-snapshot as the source of truth going forward;
    // before first redo, m_removedLengthHint carries the construction-time value.
}

void MarkdownDelta::redo() {
    if (m_firstRedo) {
        m_removed = m_doc->canonicalSubstring(m_offset, m_removedLengthHint);
        m_firstRedo = false;
    }
    m_doc->applyCanonicalDelta(m_offset, m_removed.size(), m_inserted);
}

void MarkdownDelta::undo() {
    m_doc->applyCanonicalDelta(m_offset, m_inserted.size(), m_removed);
}

int MarkdownDelta::id() const {
    return kMarkdownDeltaCommandId;
}

bool MarkdownDelta::mergeWith(const QUndoCommand *other) {
    if (other->id() != id()) return false;
    const auto *next = static_cast<const MarkdownDelta *>(other);
    if (next->m_doc != m_doc) return false;

    // We only merge pure-insert-with-pure-insert or pure-delete-with-pure-delete.
    // Mixed edits keep separate undo granularity.
    const bool thisIsInsert = m_removed.isEmpty() && !m_inserted.isEmpty();
    const bool nextIsInsert = next->m_removed.isEmpty() && !next->m_inserted.isEmpty();
    const bool thisIsDelete = !m_removed.isEmpty() && m_inserted.isEmpty();
    const bool nextIsDelete = !next->m_removed.isEmpty() && next->m_inserted.isEmpty();

    if (thisIsInsert && nextIsInsert) {
        // Adjacent inserts: next.offset == this.offset + this.inserted.size().
        if (next->m_offset != m_offset + m_inserted.size()) return false;
        m_inserted += next->m_inserted;
        return true;
    }

    if (thisIsDelete && nextIsDelete) {
        // Adjacent backspace: next.offset + next.removed.size() == this.offset.
        if (next->m_offset + next->m_removed.size() != m_offset) return false;
        m_removed = next->m_removed + m_removed;
        m_offset = next->m_offset;
        return true;
    }

    return false;
}

} // namespace Markoff
