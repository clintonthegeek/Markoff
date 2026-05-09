// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveListModelBinding.h>

#include <markoff/core/MarkoffDocument.h>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcCursor, "markoff.live.cursor", QtWarningMsg)

namespace Markoff::Live {

LiveCursorState::LiveCursorState(const BlockKindRegistry *registry,
                                 const LiveBlockModel    *model,
                                 LiveListModelBinding    *binding,
                                 QObject                 *parent)
    : QObject(parent)
    , m_cursor(NoCursor{})
    , m_registry(registry)
    , m_model(model)
    , m_binding(binding)
{
    if (binding) {
        connect(binding, &LiveListModelBinding::structuralRowsInserted,
                this, &LiveCursorState::onStructuralRowsInserted);
        connect(binding, &LiveListModelBinding::structuralRowRemoved,
                this, &LiveCursorState::onStructuralRowRemoved);
    }

}

QString LiveCursorState::cursorKind() const
{
    if (std::holds_alternative<TextCaret>(m_cursor))           return QStringLiteral("TextCaret");
    if (std::holds_alternative<BlockSelected>(m_cursor))       return QStringLiteral("BlockSelected");
    if (std::holds_alternative<BlockInternalEdit>(m_cursor))   return QStringLiteral("BlockInternalEdit");
    return QStringLiteral("none");
}

int LiveCursorState::focusedAnchorRow() const
{
    if (!m_model) return -1;
    if (const auto *tc = std::get_if<TextCaret>(&m_cursor)) {
        return rowForBlock(tc->block);
    }
    return -1;
}

int LiveCursorState::focusedQtPos() const
{
    if (const auto *tc = std::get_if<TextCaret>(&m_cursor))
        return static_cast<int>(tc->cachedByteOffset);
    return -1;
}

void LiveCursorState::request(const Cursor &newCursor)
{
    if (!validateVariant(newCursor)) {
        qCWarning(lcCursor) << "cursor request rejected: invalid variant for kind";
        return;
    }
    // Explicit request supersedes any pending structural delivery. Without
    // this, a later structural signal could resolve a stale pending and clobber
    // the cursor we are trying to set right now (e.g. Enter-on-hole-at-EOB
    // commits the old hole AND opens a new one in immediate succession; the
    // commit's pending must not overwrite the new-hole request).
    m_pendingRow.reset();
    if (m_cursor == newCursor) return;
    m_cursor = newCursor;

    QString desc = QStringLiteral("None");
    if (auto *tc = std::get_if<TextCaret>(&newCursor)) {
        const int row = m_model ? rowForBlock(tc->block) : -1;
        desc = QStringLiteral("TextCaret(innerRow=%1, qtPos=%2)")
                   .arg(row).arg(tc->cachedByteOffset);
    } else if (std::holds_alternative<NoCursor>(newCursor)) {
        desc = QStringLiteral("NoCursor");
    }
    qInfo().noquote() << "[dogfood] CursorState: request" << desc;

    Q_EMIT cursorChanged();
}

void LiveCursorState::clear()
{
    if (std::holds_alternative<NoCursor>(m_cursor)) return;
    m_cursor = NoCursor{};
    Q_EMIT cursorChanged();
}

int LiveCursorState::rowForBlock(const Markoff::BlockAnchor &block) const
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (m_model->recordAt(i).blockAnchor == block)
            return i;
    }
    return -1;
}

void LiveCursorState::requestTextCaretAtRow(int expectedRow, int qtPos)
{
    if (!m_model) return;
    if (expectedRow < 0) return;
    // Drain any queued d2DocumentChanged before resolving. Required when this
    // request comes immediately after an in-place buffer mutation on the same
    // row (e.g. paragraph soft-break). resolvePendingForRow fires cursorChanged
    // synchronously when the row already exists, but the QML delegate's
    // QTextDocument hasn't been refreshed yet — onCursorChanged would set
    // edit.cursorPosition against the stale text and a later setPlainText
    // would reflow the caret. Flushing first ensures the model + bound
    // QTextDocument are on the post-edit text before the cursor lands.
    // No-op when nothing is pending. See Option A discussion for rationale.
    if (m_binding && m_binding->document())
        m_binding->document()->flushPendingD2Changed();
    qInfo().noquote() << "[dogfood] CursorState: requestTextCaretAtRow row=" << expectedRow
                      << "qtPos=" << qtPos
                      << "(model.rowCount=" << m_model->rowCount() << ")";
    m_pendingRow = PendingRow{ expectedRow, qtPos, std::nullopt };
    if (expectedRow < m_model->rowCount())
        resolvePendingForRow(expectedRow);
}

void LiveCursorState::requestTextCaretAtNewRow(int expectedRow, int qtPos)
{
    if (!m_model) return;
    if (expectedRow < 0) return;
    qInfo().noquote() << "[dogfood] CursorState: requestTextCaretAtNewRow row=" << expectedRow
                      << "qtPos=" << qtPos
                      << "(model.rowCount=" << m_model->rowCount() << ")";
    // Pure-pending: do NOT resolve against the current row at this index —
    // that would land the cursor on whatever block currently sits there
    // (the block that's about to be SHIFTED by the upcoming insertion).
    // Wait for the next structural signal whose range covers expectedRow.
    m_pendingRow = PendingRow{ expectedRow, qtPos, std::nullopt };
}

void LiveCursorState::requestTextCaretAtAnchor(Markoff::BlockAnchor expectedAnchor,
                                               int qtPos)
{
    if (!m_model) return;
    qInfo().noquote() << "[dogfood] CursorState: requestTextCaretAtAnchor qtPos=" << qtPos
                      << "(model.rowCount=" << m_model->rowCount() << ")";
    // Anchor-keyed pure-pending. Do NOT resolve immediately — the model
    // currently still reflects the PRE-edit state (the anchor sits at its
    // OLD row, which is about to be displaced by the upcoming insertion).
    // Wait for a structural signal to fire during parse-back applyOps; at that
    // point the anchor's CURRENT row is the right cursor target.
    PendingRow p;
    p.row = -1;
    p.qtPos = qtPos;
    p.anchor = expectedAnchor;
    m_pendingRow = std::move(p);
}

void LiveCursorState::onStructuralRowsInserted(int first, int last)
{
    if (!m_pendingRow) return;
    const int row = m_pendingRow->row;

    // Anchor-keyed: search for the block by anchor identity
    if (m_pendingRow->anchor) {
        resolvePendingForAnchor();
        return;
    }

    // Row-keyed: if the expected row falls in the inserted range (or just after)
    if (row >= first && row <= last + 1) {
        if (row < m_model->rowCount())
            resolvePendingForRow(row);
    }
}

void LiveCursorState::onStructuralRowRemoved(int row)
{
    if (!m_pendingRow) return;
    if (m_pendingRow->anchor) {
        // A block was removed. If we have an anchor-keyed pending, try to
        // resolve it now (the surviving block may have the right anchor).
        resolvePendingForAnchor();
        return;
    }
    // Row-keyed pending is orphaned if its target row is deleted.
    if (m_pendingRow->row == row)
        m_pendingRow.reset();
}

void LiveCursorState::resolvePendingForAnchor()
{
    if (!m_model) return;
    if (!m_pendingRow || !m_pendingRow->anchor) return;
    const Markoff::BlockAnchor target = *m_pendingRow->anchor;
    const int rows = m_model->rowCount();
    for (int r = 0; r < rows; ++r) {
        if (m_model->recordAt(r).blockAnchor == target) {
            const int qtPos = m_pendingRow->qtPos;
            m_pendingRow.reset();

            TextCaret tc;
            tc.block            = target;
            tc.cachedByteOffset = static_cast<quint32>(qtPos);
            request(tc);  // emits cursorChanged() — hint must still be set during this call

            // Clear the visual-line hint AFTER request() so that cursorChanged
            // handlers (e.g. QML's onCursorChanged → focusEditAt) can read it.
            if (m_pendingVlhint != VisualLineHint::None) {
                m_pendingVlhint = VisualLineHint::None;
                Q_EMIT visualLineHintChanged();
            }
            return;
        }
    }
    // Anchor not (yet) present in the model. Wait for the next event.
}

void LiveCursorState::resolvePendingForRow(int row)
{
    if (!m_model) return;
    if (row < 0 || row >= m_model->rowCount()) return;

    const BlockRecord &rec = m_model->recordAt(row);
    const Markoff::BlockAnchor anchor = rec.blockAnchor;
    // A default-constructed BlockAnchor (id == 0) cannot be addressed; skip.
    if (anchor == Markoff::BlockAnchor{}) {
        return;
    }

    const int qtPos = m_pendingRow ? m_pendingRow->qtPos : 0;
    // Reset BEFORE request(): a cursorChanged consumer may re-enter and
    // call requestTextCaretAtRow; reading a stale m_pendingRow during
    // that re-entrance would produce the wrong qtPos.
    m_pendingRow.reset();

    TextCaret tc;
    tc.block            = anchor;
    tc.cachedByteOffset = static_cast<quint32>(qtPos);
    // positionAnchor: left default — selection projection refreshes it.
    request(tc);  // emits cursorChanged() — hint must still be set during this call

    // Clear the visual-line hint AFTER request() so that cursorChanged
    // handlers (e.g. QML's onCursorChanged → focusEditAt) can read it.
    if (m_pendingVlhint != VisualLineHint::None) {
        m_pendingVlhint = VisualLineHint::None;
        Q_EMIT visualLineHintChanged();
    }
}

void LiveCursorState::requestTextCaretAtRowVisualX(int expectedRow, VisualLineHint hint)
{
    m_pendingVlhint = hint;
    Q_EMIT visualLineHintChanged();
    requestTextCaretAtRow(expectedRow, 0);  // qtPos=0 is overridden by delegate hint path
}

void LiveCursorState::setDesiredVisualX(qreal x)
{
    if (qFuzzyCompare(m_desiredVisualX, x)) return;
    m_desiredVisualX = x;
    Q_EMIT desiredVisualXChanged();
}

void LiveCursorState::clearDesiredVisualX()
{
    setDesiredVisualX(-1.0);
}

bool LiveCursorState::validateVariant(const Cursor &c) const
{
    if (std::holds_alternative<NoCursor>(c)) return true;

    const BlockId *blockIdPtr = nullptr;
    if (auto *tc = std::get_if<TextCaret>(&c))               blockIdPtr = &tc->block;
    else if (auto *bs = std::get_if<BlockSelected>(&c))      blockIdPtr = &bs->block;
    else if (auto *bi = std::get_if<BlockInternalEdit>(&c))  blockIdPtr = &bi->block;
    if (!blockIdPtr) return false;

    const int row = rowForBlock(*blockIdPtr);
    if (row < 0) {
        qCWarning(lcCursor) << "cursor request for unknown block";
        return false;
    }

    const QString kind = m_model->recordAt(row).kind;
    const auto *desc = m_registry->find(kind);
    if (!desc) {
        qCWarning(lcCursor) << "cursor request for unregistered kind" << kind;
        return false;
    }

    QString variantName;
    if (std::holds_alternative<TextCaret>(c))            variantName = QStringLiteral("TextCaret");
    else if (std::holds_alternative<BlockSelected>(c))   variantName = QStringLiteral("BlockSelected");
    else if (std::holds_alternative<BlockInternalEdit>(c)) variantName = QStringLiteral("BlockInternalEdit");

    return desc->supportedCursorVariants.contains(variantName);
}

}  // namespace Markoff::Live
