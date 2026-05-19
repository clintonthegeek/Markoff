// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/BlockKind.h>
#include <markoff/live/BlockKindRegistry.h>
#include <markoff/live/Coordinates.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveListModelBinding.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/Selection.h>

#include "KindDispatch.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QLoggingCategory>
#include <climits>

Q_LOGGING_CATEGORY(lcCursor, "markoff.live.cursor", QtWarningMsg)

namespace Markoff::Live {

namespace coords = Detail::Coordinates;

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
        return static_cast<int>(tc->cachedQtPos);
    return -1;
}

void LiveCursorState::request(const Cursor &newCursor)
{
    if (!validateVariant(newCursor)) {
        qCWarning(lcCursor) << "cursor request rejected: invalid variant for kind";
        return;
    }
    // Explicit request supersedes any pending chokepoint delivery. Without
    // this, a later delegate-registration could resolve a stale pending and
    // clobber the cursor being set right now (e.g. Enter-on-hole-at-EOB
    // commits the old hole AND opens a new one in immediate succession; the
    // commit's pending must not overwrite the new-hole request). Note: when
    // tryResolvePending calls request() it has ALREADY reset m_pendingFocus
    // at that point, so this is a no-op for the resolution path; the line
    // exists for direct request() callers (LiveStructuralKeyHandler's
    // BlockInternalEdit/BlockSelected entries).
    m_pendingFocus.reset();
    if (m_cursor == newCursor) return;
    m_cursor = newCursor;

    QString desc = QStringLiteral("None");
    if (auto *tc = std::get_if<TextCaret>(&newCursor)) {
        const int row = m_model ? rowForBlock(tc->block) : -1;
        desc = QStringLiteral("TextCaret(innerRow=%1, qtPos=%2)")
                   .arg(row).arg(tc->cachedQtPos);
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

bool LiveCursorState::hasSelection() const noexcept
{
    if (!m_selectionAnchor) return false;
    const auto tc = currentTextCaret();
    if (!tc) return false;  // selection only meaningful when active end is TextCaret
    return !(m_selectionAnchor->block == tc->block
             && m_selectionAnchor->qtPos == tc->cachedQtPos);
}

void LiveCursorState::setSelectionAnchor(SelectionAnchor anchor)
{
    if (m_selectionAnchor && *m_selectionAnchor == anchor) return;
    m_selectionAnchor = anchor;
    Q_EMIT selectionChanged();
}

void LiveCursorState::clearSelectionAnchor() noexcept
{
    if (!m_selectionAnchor) return;
    m_selectionAnchor.reset();
    Q_EMIT selectionChanged();
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
    // Ensure the model reflects all pending CRDT edits before we resolve
    // the row → anchor mapping. The binding owns the doc and exposes this
    // as a single entry point; cursor state stays out of doc internals
    // (queue #2 concern #5).
    if (m_binding)
        m_binding->flushPendingDocumentChanges();
    const Markoff::BlockAnchor anchor = m_model->recordAt(expectedRow).blockAnchor;
    if (anchor == Markoff::BlockAnchor{}) return;
    establishFocus(anchor, qtPos);
}

void LiveCursorState::syncFromTextEdit(Markoff::BlockAnchor anchor, int qtPos)
{
    if (anchor == Markoff::BlockAnchor{}) return;
    if (qtPos < 0) return;

    TextCaret tc;
    tc.block            = anchor;
    tc.cachedQtPos = static_cast<quint32>(qtPos);
    Cursor newCursor    = tc;

    if (m_cursor == newCursor) return;
    if (!validateVariant(newCursor)) return;

    m_cursor = newCursor;
    Q_EMIT cursorChanged();
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

Markoff::BlockAnchor LiveCursorState::blockAnchorAt(int blockIndex) const
{
    if (!m_binding || !m_binding->document()) return {};
    if (blockIndex < 0) return {};
    const auto ids = m_binding->document()->iterateBlocks();
    if (blockIndex >= static_cast<int>(ids.size())) return {};
    return ids[static_cast<std::size_t>(blockIndex)];
}

bool LiveCursorState::validateVariant(const Cursor &c) const
{
    if (std::holds_alternative<NoCursor>(c)) return true;

    const BlockId *blockIdPtr = nullptr;
    if (auto *tc = std::get_if<TextCaret>(&c))               blockIdPtr = &tc->block;
    else if (auto *bs = std::get_if<BlockSelected>(&c))      blockIdPtr = &bs->block;
    else if (auto *bi = std::get_if<BlockInternalEdit>(&c))  blockIdPtr = &bi->block;
    if (!blockIdPtr) return false;

    // Registry-less paths (unit tests with no QML) cannot enforce variant
    // capability — accept the request. Queue #2 concern #9: previously the
    // chokepoint had to bypass this entire path to avoid the null deref.
    if (!m_registry) return true;

    // Look up the current kind. Prefer the document over the model so we
    // accept transient states during a structural cascade where the doc
    // has already applied a `changeKind` but `applyOps` hasn't re-emitted
    // the model rows yet — model.kind and doc.kind disagree for that
    // window. The chokepoint and its callers already operate against
    // doc state; validateVariant follows. Falls back to the model when no
    // binding/document is wired.
    QString kind;
    if (m_binding && m_binding->document()) {
        using BK = ::Markoff::BlockKind;
        switch (m_binding->document()->blockKind(::Markoff::BlockId(*blockIdPtr))) {
        case BK::Heading:        kind = ::Markoff::Live::BlockKind::Heading;        break;
        case BK::CodeBlock:      kind = ::Markoff::Live::BlockKind::CodeBlock;      break;
        case BK::HorizontalRule: kind = ::Markoff::Live::BlockKind::HorizontalRule; break;
        case BK::Image:          kind = ::Markoff::Live::BlockKind::Image;          break;
        case BK::ListItem:       kind = ::Markoff::Live::BlockKind::ListItem;       break;
        case BK::BlockQuote:     kind = ::Markoff::Live::BlockKind::Blockquote;     break;
        case BK::Math:           kind = ::Markoff::Live::BlockKind::Math;           break;
        default:                 kind = ::Markoff::Live::BlockKind::Paragraph;      break;
        }
    } else if (m_model) {
        const int row = rowForBlock(*blockIdPtr);
        if (row < 0) {
            qCWarning(lcCursor) << "cursor request for unknown block";
            return false;
        }
        kind = m_model->recordAt(row).kind;
    } else {
        // No model AND no doc — can't validate. Accept.
        return true;
    }

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
                    static_cast<int>(tc->cachedQtPos),
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
    // Unknown block: no row in model AND no binding (or binding's doc agrees
    // it's gone). Without a known kind we have no basis to dispatch — hold
    // the pending until either expiry or the structural signal that adds
    // the row.
    if (currentKind.isEmpty()) return;

    // Compare delegate *classes*, not literal kinds. Cross-class transitions
    // (paragraph → code-block, paragraph → hr, …) destroy the old delegate
    // and create a new one — bail and wait for the new delegate's
    // delegateAvailable callback to retry. Within-class transitions
    // (paragraph ↔ heading ↔ blockquote ↔ list-item, all in the
    // `text-inline` delegateClass introduced by tier-3) keep the same
    // QQuickItem; `m_delegates[anchor].kind` never gets refreshed
    // because Component.onCompleted doesn't re-fire, so a literal-kind
    // comparison would falsely bail and the cursor request would never
    // resolve. Self-heal the entry once we've confirmed the class matches.
    // Regression: queue.md #6 (`nav_into_runtime_promoted_heading`).
    if (delegateClassFor(it->kind) != delegateClassFor(currentKind)) return;
    if (it->kind != currentKind) it->kind = currentKind;

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
    // Queue #2 concern #9: previously this constructed `newCursor` directly
    // and assigned `m_cursor`, bypassing `request()` because
    // `validateVariant` could segfault on a null registry and reject
    // valid transient states. validateVariant is now null-safe and queries
    // the document instead of the model, so this can route through
    // `request()` like every other mutator.
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
            tc.cachedQtPos = static_cast<quint32>(qtPos);
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
    request(newCursor);

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

// ---------------------------------------------------------------------------
// Tier 4c — selection operations (Phase A shadow copies)
// ---------------------------------------------------------------------------

namespace {
struct SelectionCorners {
    int anchorRow, anchorQtPos, activeRow, activeQtPos;
    bool valid;
};
} // namespace

static SelectionCorners cornersFromCanonical(const LiveCursorState *cs,
                                             const LiveBlockModel *model)
{
    SelectionCorners c{-1, -1, -1, -1, false};
    if (!model) return c;
    const auto anchor = cs->selectionAnchor();
    const auto active = cs->currentTextCaret();
    if (!anchor || !active) return c;
    const int aRow = cs->rowForBlock(anchor->block);
    const int xRow = cs->rowForBlock(active->block);
    if (aRow < 0 || xRow < 0) return c;
    c.anchorRow   = aRow;
    c.anchorQtPos = static_cast<int>(anchor->qtPos);
    c.activeRow   = xRow;
    c.activeQtPos = static_cast<int>(active->cachedQtPos);
    c.valid = true;
    return c;
}

static void normalizeCorners(const SelectionCorners &c, int &fb, int &fo, int &lb, int &lo)
{
    if (c.anchorRow < c.activeRow
        || (c.anchorRow == c.activeRow && c.anchorQtPos <= c.activeQtPos)) {
        fb = c.anchorRow; fo = c.anchorQtPos;
        lb = c.activeRow; lo = c.activeQtPos;
    } else {
        fb = c.activeRow; fo = c.activeQtPos;
        lb = c.anchorRow; lo = c.anchorQtPos;
    }
}

QPoint LiveCursorState::selectionRangeForBlock(int row) const
{
    const auto c = cornersFromCanonical(this, m_model);
    if (!c.valid) return QPoint(-1, -1);

    int fb, fo, lb, lo;
    normalizeCorners(c, fb, fo, lb, lo);

    if (row < fb || row > lb)         return QPoint(-1, -1);
    if (fb == lb)                     return QPoint(qMin(fo, lo), qMax(fo, lo));
    if (row == fb)                    return QPoint(fo, INT_MAX);
    if (row == lb)                    return QPoint(0, lo);
    return QPoint(0, INT_MAX);
}

void LiveCursorState::copySelectionToClipboard() const
{
    if (!hasSelection() || !m_model) return;

    const auto c = cornersFromCanonical(this, m_model);
    if (!c.valid) return;
    int fb, fo, lb, lo;
    normalizeCorners(c, fb, fo, lb, lo);

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

void LiveCursorState::selectAllBlocks()
{
    if (!m_model) return;
    const int rowCount = m_model->rowCount();
    if (rowCount <= 0) return;

    const auto firstAnchor = m_model->recordAt(0).blockAnchor;
    const auto lastRow     = rowCount - 1;
    const auto lastAnchor  = m_model->recordAt(lastRow).blockAnchor;
    const auto lastText    = m_model->recordAt(lastRow).text;

    // Active end at the end of the last block.
    syncFromTextEdit(lastAnchor, lastText.length());
    // Anchor at the start of the first block.
    setSelectionAnchor({firstAnchor, /*qtPos=*/0});
}

void LiveCursorState::deleteSelectionRange()
{
    if (!hasSelection() || !m_model || !m_binding || !m_binding->document())
        return;

    auto *doc = m_binding->document();
    const auto c = cornersFromCanonical(this, m_model);
    if (!c.valid) return;
    int fb, fo, lb, lo;
    normalizeCorners(c, fb, fo, lb, lo);

    const int rowCount = m_model->rowCount();
    if (fb < 0 || fb >= rowCount || lb < 0 || lb >= rowCount) return;

    // Compute flat byte start/end by walking iterateBlocks().
    const auto blocks = doc->iterateBlocks();
    if (static_cast<int>(blocks.size()) != rowCount) return;
    qsizetype startByte = 0, endByte = 0, cursor = 0;
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        const QByteArray rawText = doc->blockText(blocks[i]);
        const qsizetype blockSize = rawText.size();

        if (i == fb) {
            const QByteArray modelUtf8 = m_model->recordAt(fb).text.toUtf8();
            startByte = cursor + coords::qtPosToByte(modelUtf8, fo);
        }
        if (i == lb) {
            const QByteArray modelUtf8 = m_model->recordAt(lb).text.toUtf8();
            endByte = cursor + coords::qtPosToByte(modelUtf8, lo);
            break;
        }
        cursor += blockSize;
    }

    if (endByte <= startByte) return;

    doc->applyFlatEdit(static_cast<uint32_t>(startByte),
                       static_cast<uint32_t>(endByte),
                       QByteArray(), Markoff::Origin::UserEdit);
    clearSelectionAnchor();
}

// ---------------------------------------------------------------------------
// Session bridge (tier 4c Phase A)
// ---------------------------------------------------------------------------

void LiveCursorState::setSession(Markoff::Session *session)
{
    if (m_session == session) return;
    if (m_session)
        QObject::disconnect(m_session, &Markoff::Session::primarySelectionChanged,
                            this, &LiveCursorState::onSessionPrimarySelectionChanged);
    m_session = session;
    if (m_session)
        QObject::connect(m_session, &Markoff::Session::primarySelectionChanged,
                         this, &LiveCursorState::onSessionPrimarySelectionChanged);
}

void LiveCursorState::syncSelectionToSession()
{
    if (!m_session || !m_binding || !m_binding->document() || !m_model) return;
    // Do NOT gate on hasSelection() — collapsed selections (begin without extend,
    // i.e. anchor == active) are still valid cursor positions to propagate for
    // remote presence. The original LiveSelectionView::syncToSession() had no
    // hasSelection() check; that behaviour is preserved here.
    const auto anchor = m_selectionAnchor;
    const auto active = currentTextCaret();
    if (!anchor || !active) return;
    const int aRow = rowForBlock(anchor->block);
    const int xRow = rowForBlock(active->block);
    if (aRow < 0 || xRow < 0) return;

    auto *doc = m_binding->document();
    const auto makeAnchor = [&](Markoff::BlockAnchor block, int qtPos)
                                  -> Markoff::TextAnchor {
        const int row = rowForBlock(block);
        if (row < 0) return Markoff::TextAnchor{};
        const auto utf8 = m_model->recordAt(row).text.toUtf8();
        const int byteOff = static_cast<int>(
            coords::qtPosToByte(utf8, qMax(0, qtPos)));
        return doc->textAnchorAt(block, byteOff, /*rightBias=*/true);
    };

    Markoff::Selection sel;
    sel.kind   = Markoff::Selection::Kind::Primary;
    sel.anchor = makeAnchor(anchor->block, static_cast<int>(anchor->qtPos));
    sel.active = makeAnchor(active->block, static_cast<int>(active->cachedQtPos));
    m_session->setPrimarySelection(sel);
    // No m_applying re-entrance guard — onSessionPrimarySelectionChanged
    // short-circuits on equality when the round-trip fires back.
}

void LiveCursorState::onSessionPrimarySelectionChanged(const Markoff::Selection &sel)
{
    if (!m_binding || !m_binding->document() || !m_model) return;

    auto *doc = m_binding->document();
    const auto resolveAnchor = [&](const Markoff::TextAnchor &ta)
                                      -> std::optional<SelectionAnchor> {
        if (ta.isNull()) return std::nullopt;
        const Markoff::BlockAnchor ba = ta.block();
        if (rowForBlock(ba) < 0) return std::nullopt;  // not in model
        const int byteOff = doc->offsetInBlock(ba, ta);
        const int row = rowForBlock(ba);
        const auto utf8 = m_model->recordAt(row).text.toUtf8();
        const int clamped = qBound(0, byteOff, static_cast<int>(utf8.size()));
        const int qtPos = static_cast<int>(coords::byteToQtPos(utf8, clamped));
        return SelectionAnchor{ba, static_cast<quint32>(qtPos)};
    };

    const auto resolvedAnchor = resolveAnchor(sel.anchor);
    const auto resolvedActive = resolveAnchor(sel.active);

    if (!resolvedAnchor || !resolvedActive) {
        // Orphaned anchor → clear selection. Active end stays put.
        clearSelectionAnchor();
        return;
    }

    // Equality short-circuit (supersedes the m_applyingSessionSelection guard).
    // Compares derived (BlockAnchor, qtPos) pairs — intentionally looser than
    // TextAnchor identity, per spec §4.3 / §11. Stable across CRDT edits that
    // preserve visual position.
    const auto currentActive = currentTextCaret();
    const bool sameActive = currentActive
        && currentActive->block == resolvedActive->block
        && currentActive->cachedQtPos == resolvedActive->qtPos;
    const bool sameAnchor = m_selectionAnchor
        && *m_selectionAnchor == *resolvedAnchor;
    if (sameActive && sameAnchor) return;

    if (!sameActive) {
        // Active end mutates via the typing-authority hook (idempotent).
        syncFromTextEdit(resolvedActive->block, static_cast<int>(resolvedActive->qtPos));
    }
    if (!sameAnchor) {
        setSelectionAnchor(*resolvedAnchor);
    }
    // Notify the facade that an externally-originated session change landed.
    // This signal is only emitted here (not from local begin/extend/clear
    // paths) so the facade can forward it to its own selectionChanged without
    // double-counting local emissions.
    Q_EMIT selectionChangedFromSession();
}

// ---------------------------------------------------------------------------
// Selection operations (migrated from LiveSelectionView, 2026-05-19)
// ---------------------------------------------------------------------------

void LiveCursorState::begin(int blockIndex, int qtPos)
{
    const auto anchor = blockAnchorAt(blockIndex);
    if (anchor.isNull()) return;
    syncFromTextEdit(anchor, qtPos);
    setSelectionAnchor({anchor, static_cast<quint32>(qtPos)});  // emits selectionChanged()
    syncSelectionToSession();
}

void LiveCursorState::extend(int blockIndex, int qtPos)
{
    const auto anchor = blockAnchorAt(blockIndex);
    if (anchor.isNull()) return;
    syncFromTextEdit(anchor, qtPos);
    syncSelectionToSession();
    Q_EMIT selectionChanged();
}

void LiveCursorState::clearSelection()
{
    clearSelectionAnchor();  // emits selectionChanged()
}

void LiveCursorState::selectAll()
{
    selectAllBlocks();  // calls setSelectionAnchor() → emits selectionChanged()
    syncSelectionToSession();
}

void LiveCursorState::deleteSelection()
{
    deleteSelectionRange();  // calls clearSelectionAnchor() → emits selectionChanged()
}

QPoint LiveCursorState::rangeForBlock(int blockIndex) const
{
    return selectionRangeForBlock(blockIndex);
}

void LiveCursorState::copyToClipboard() const
{
    copySelectionToClipboard();
}

int LiveCursorState::anchorBlock() const
{
    const auto a = selectionAnchor();
    if (!a) return -1;
    return rowForBlock(a->block);
}

int LiveCursorState::anchorQtPos() const
{
    const auto a = selectionAnchor();
    if (!a) return -1;
    return static_cast<int>(a->qtPos);
}

int LiveCursorState::activeBlock() const
{
    const auto tc = currentTextCaret();
    if (!tc) return -1;
    return rowForBlock(tc->block);
}

int LiveCursorState::activeQtPos() const
{
    const auto tc = currentTextCaret();
    if (!tc) return -1;
    return static_cast<int>(tc->cachedQtPos);
}

}  // namespace Markoff::Live
