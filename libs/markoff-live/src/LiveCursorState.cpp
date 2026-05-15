// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockKind.h>
#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveListModelBinding.h>

#include <markoff/core/MarkoffDocument.h>

#include <QDateTime>
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
    // Resolve the row for whichever variant carries a block anchor.
    // Required so non-text-bearing blocks (HR, Image) report their row
    // when the cursor is in BlockSelected / BlockInternalEdit state.
    if (const auto *tc = std::get_if<TextCaret>(&m_cursor))
        return rowForBlock(tc->block);
    if (const auto *bs = std::get_if<BlockSelected>(&m_cursor))
        return rowForBlock(bs->block);
    if (const auto *bi = std::get_if<BlockInternalEdit>(&m_cursor))
        return rowForBlock(bi->block);
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
    // Thin wrapper around establishFocus. Callers that have not yet been
    // migrated (navigation controller) reach the chokepoint through here.
    if (!m_model) return;
    if (expectedRow < 0 || expectedRow >= m_model->rowCount()) return;
    // Flush queued D2 changes (no-op when nothing is pending) so the
    // delegate's QTextDocument is on post-edit text before focus lands.
    if (m_binding && m_binding->document())
        m_binding->document()->flushPendingD2Changed();
    const Markoff::BlockAnchor anchor = m_model->recordAt(expectedRow).blockAnchor;
    if (anchor == Markoff::BlockAnchor{}) return;
    establishFocus(anchor, qtPos);
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

void LiveCursorState::syncFromTextEdit(Markoff::BlockAnchor anchor, int qtPos)
{
    if (anchor == Markoff::BlockAnchor{}) return;
    if (qtPos < 0) return;

    TextCaret tc;
    tc.block            = anchor;
    tc.cachedByteOffset = static_cast<quint32>(qtPos);
    Cursor newCursor    = tc;

    if (m_cursor == newCursor) return;
    if (!validateVariant(newCursor)) return;

    m_cursor = newCursor;
    Q_EMIT cursorChanged();
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

void LiveCursorState::attachModel(const LiveBlockModel *model) {
    m_model = model;
}

// --- §5.1 focus-chokepoint implementation ---

void LiveCursorState::establishFocus(Markoff::BlockAnchor blockAnchor, int qtPos) {
    // §7.3 — drop silently if the anchor is unknown to model, delegates,
    // AND the underlying CRDT document. A block just created by enterAtEnd
    // etc. is in the document but not yet in m_model (onD2Changed hasn't
    // fired), so we check the document as a last resort before dropping.
    if (m_model && m_model->kindFor(blockAnchor).isEmpty()
            && !m_delegates.contains(blockAnchor)) {
        bool inDoc = false;
        if (m_binding && m_binding->document()) {
            for (const auto &id : m_binding->document()->iterateBlocks()) {
                if (Markoff::BlockAnchor(id) == blockAnchor) { inDoc = true; break; }
            }
        }
        if (!inDoc) return;
    }
    m_pendingFocus = PendingFocus{
        blockAnchor,
        qtPos,
        QDateTime::currentMSecsSinceEpoch()
    };
    if (!m_inStructuralCascade) {
        tryResolvePending();
    }
}

void LiveCursorState::beginStructuralCascade() {
    m_inStructuralCascade = true;
}

void LiveCursorState::endStructuralCascade() {
    m_inStructuralCascade = false;
    tryResolvePending();
}

void LiveCursorState::delegateAvailable(Markoff::BlockAnchor blockAnchor,
                                        const QString &kind,
                                        QQuickItem *delegateRoot) {
    // Check BEFORE insert: a pre-existing entry means a kind-transition
    // replacement. With the delegate-root caching in
    // BlockOnlyDelegateBase/ParagraphDelegate/etc., the old delegate's
    // Component.onDestruction can fire either before or after the new
    // delegate's Component.onCompleted; either order is handled. See the
    // companion comment in delegateGoingAway.
    const bool wasRegistered = m_delegates.contains(blockAnchor);
    m_delegates.insert(blockAnchor, { kind, QPointer<QQuickItem>(delegateRoot) });

    // Kind-transition re-focus: stage a pending focus so the new delegate
    // inherits the caret. Works inside and outside the cascade — if we are
    // inside, endStructuralCascade's tryResolvePending will pick it up.
    if (wasRegistered && !m_pendingFocus) {
        if (const auto *tc = std::get_if<TextCaret>(&m_cursor)) {
            if (tc->block == blockAnchor) {
                m_pendingFocus = PendingFocus{
                    blockAnchor,
                    static_cast<int>(tc->cachedByteOffset),
                    QDateTime::currentMSecsSinceEpoch()
                };
            }
        }
    }

    if (!m_inStructuralCascade) {
        tryResolvePending();
    }
}

void LiveCursorState::delegateGoingAway(Markoff::BlockAnchor blockAnchor,
                                        QQuickItem *delegateRoot) {
    // Kind-transition replacement ordering: when DelegateChooser swaps the
    // delegate for a row whose `kind` role changed (Paragraph → HR via typed
    // `---`), the NEW delegate's Component.onCompleted (and thus
    // delegateAvailable) fires BEFORE the OLD delegate's
    // Component.onDestruction. A naive `m_delegates.remove(anchor)` here
    // would then clobber the freshly-registered replacement, leaving the
    // chokepoint with no delegate for the anchor and making the new HR
    // unreachable by arrow nav, click, or any other establishFocus path.
    // Only remove the entry if it still belongs to *this* dying delegate.
    auto it = m_delegates.find(blockAnchor);
    if (it == m_delegates.end()) return;
    if (delegateRoot != nullptr && it->root.data() != delegateRoot) return;
    m_delegates.erase(it);
    // Pending request NOT cleared — §7.2.
}

void LiveCursorState::tryResolvePending() {
    if (!m_pendingFocus) return;
    expireIfTimedOut(*m_pendingFocus);
    if (!m_pendingFocus) return;

    const auto anchor = m_pendingFocus->target;
    const auto it = m_delegates.find(anchor);
    if (it == m_delegates.end() || !it->root) return;

    // Stale-registration check — spec §5.1.1. Query the document directly:
    // during a kind-transition cascade the model's kindFor is stale (applyOps
    // for the new kind hasn't run yet), but the document already reflects the
    // post-`changeKind` state. Using the document lets us correctly detect
    // "old delegate is still registered against an outdated kind" and defer
    // resolution until the new delegate arrives. Fall back to model when no
    // document is wired (unit tests).
    QString currentKind;
    if (m_binding && m_binding->document()) {
        using BK = ::Markoff::BlockKind;
        switch (m_binding->document()->blockKind(::Markoff::BlockId(anchor))) {
        case BK::Heading:        currentKind = ::Markoff::Live::BlockKind::Heading;        break;
        case BK::CodeBlock:      currentKind = ::Markoff::Live::BlockKind::CodeBlock;      break;
        case BK::HorizontalRule: currentKind = ::Markoff::Live::BlockKind::HorizontalRule; break;
        case BK::Image:          currentKind = ::Markoff::Live::BlockKind::Image;          break;
        case BK::ListItem:       currentKind = ::Markoff::Live::BlockKind::ListItem;       break;
        case BK::BlockQuote:     currentKind = ::Markoff::Live::BlockKind::Blockquote;     break;
        case BK::Math:           currentKind = ::Markoff::Live::BlockKind::Math;           break;
        default:                 currentKind = ::Markoff::Live::BlockKind::Paragraph;      break;
        }
    } else if (m_model) {
        currentKind = m_model->kindFor(anchor);
    }
    if (it->kind != currentKind) return;

    const int qtPos = m_pendingFocus->qtPos;
    m_pendingFocus.reset();

    // Update m_cursor before invoking takeFocus. If takeFocus's
    // cursorPosition assignment is a no-op (cursor already at the target
    // position — common for empty new paragraphs where pos=0), the
    // delegate's onCursorPositionChanged won't fire and syncFromTextEdit
    // won't be called. Without this pre-update, m_cursor would retain
    // whatever stale anchor it held before the structural event.
    //
    // Variant selection respects the target kind's registered
    // capabilities: `TextCaret` if supported (the common case), otherwise
    // `BlockSelected` for non-text-bearing blocks (HR, Image). This is
    // the generalizable rule that makes click+arrow navigation work for
    // every kind: the chokepoint never stages a variant the target
    // delegate can't honour, so the corresponding `Keys.onPressed`
    // `isSelected`-style guards see the right state and route arrow
    // keys through the structural key handler.
    //
    // Bypass `request()` directly — its `validateVariant` call would
    // segfault on a null registry (unit tests) and would also reject
    // some valid transient states during a structural cascade.
    Cursor newCursor;
    {
        bool supportsText  = true;   // safe default if no registry
        bool supportsBlock = false;
        if (m_registry) {
            if (const auto *desc = m_registry->find(currentKind)) {
                supportsText  = desc->supportedCursorVariants.contains(
                                    QStringLiteral("TextCaret"));
                supportsBlock = desc->supportedCursorVariants.contains(
                                    QStringLiteral("BlockSelected"));
            }
        }
        if (supportsText) {
            TextCaret tc;
            tc.block            = anchor;
            tc.cachedByteOffset = static_cast<quint32>(qtPos);
            newCursor           = tc;
        } else if (supportsBlock) {
            BlockSelected bs;
            bs.block = anchor;
            newCursor = bs;
        } else {
            // No supported cursor variant — drop. Shouldn't happen for any
            // currently-registered kind, but keeps the chokepoint total.
            return;
        }
    }
    if (!(m_cursor == newCursor)) {
        m_pendingRow.reset();  // explicit request supersedes any pending row
        m_cursor = newCursor;
        Q_EMIT cursorChanged();
    }

    QMetaObject::invokeMethod(it->root.data(), "takeFocus",
                              Q_ARG(int, qtPos));
    // Clear visual-line hint AFTER takeFocus so the delegate can read it.
    if (m_pendingVlhint != VisualLineHint::None) {
        m_pendingVlhint = VisualLineHint::None;
        Q_EMIT visualLineHintChanged();
    }
}

void LiveCursorState::expireIfTimedOut(LiveCursorState::PendingFocus &p) {
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if ((now - p.enqueuedMs) > kPendingFocusTimeoutMs) {
        m_pendingFocus.reset();
    }
}

}  // namespace Markoff::Live
