// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff/core/BlockId.h>
#include <functional>
#include <optional>
#include <variant>
#include <vector>
#include <QtTypes>
#include <QtAssert>

namespace Markoff {

using OpId = uint64_t;
using UndoActionId = uint64_t;

/// Internal undo-log routing tag. Distinct from the public-boundary
/// Markoff::CrdtTarget enum (MarkoffOp.h) which routes ops across the
/// collab wire. Renamed from CrdtTarget in D5 phase 3 to resolve the
/// name collision. See D5 spec §2.3.
struct UndoCrdtTarget {
    struct IdListT {};
    struct KindTagMapT {};
    struct BlockAttrsMapT {};
    struct FrontmatterMapT {};
    struct LinkRefMapT {};
    struct FootnoteDefMapT {};
    struct BufferT { BlockId blockId; };
    std::variant<IdListT, KindTagMapT, BlockAttrsMapT, FrontmatterMapT,
                 LinkRefMapT, FootnoteDefMapT, BufferT> kind;

    static UndoCrdtTarget idList()          { return {IdListT{}}; }
    static UndoCrdtTarget kindTagMap()      { return {KindTagMapT{}}; }
    static UndoCrdtTarget blockAttrsMap()   { return {BlockAttrsMapT{}}; }
    static UndoCrdtTarget frontmatterMap()  { return {FrontmatterMapT{}}; }
    static UndoCrdtTarget linkRefMap()      { return {LinkRefMapT{}}; }
    static UndoCrdtTarget footnoteDefMap()  { return {FootnoteDefMapT{}}; }
    static UndoCrdtTarget buffer(BlockId id){ return {BufferT{id}}; }
};

struct UndoEntry {
    UndoActionId actionId;
    std::vector<std::pair<UndoCrdtTarget, OpId>> targets;
};

struct CoalesceContext {
    BlockId block;
    bool isPrintable;
    qint64 timestampMs;
    int focusGeneration = 0;
};

class UndoLog {
public:
    class Transaction {
    public:
        explicit Transaction(UndoLog &log);
        ~Transaction();
        void registerOp(UndoCrdtTarget target, OpId opId);
        void rollback() noexcept;
    private:
        UndoLog &m_log;
        bool m_isOutermost;
        bool m_rolledBack = false;
    };

    using Dispatcher = std::function<void(const UndoCrdtTarget &, OpId, bool forward)>;
    using IsCollapsedQuery = std::function<bool(const UndoCrdtTarget &, OpId)>;

    size_t entryCount() const noexcept { return m_entries.size(); }
    const UndoEntry &lastEntry() const { return m_entries.back(); }
    bool isTransactionOpen() const noexcept { return m_nestingDepth > 0; }
    bool isBlockReferenced(BlockId id) const noexcept {
        for (const auto &entry : m_entries)
            for (const auto &[target, opId] : entry.targets)
                if (auto *b = std::get_if<UndoCrdtTarget::BufferT>(&target.kind))
                    if (b->blockId == id) return true;
        return false;
    }

    void setDispatcher(Dispatcher d) { m_dispatcher = std::move(d); }

    /// Per-op record delivered to the OnCommitFn.
    struct CommittedOp {
        UndoCrdtTarget target;
        OpId           opId;     // same Lamport-based OpId as registerOp receives
    };

    using OnCommitFn = std::function<void(const std::vector<CommittedOp> &ops,
                                           UndoActionId actionId)>;

    /// Register a commit callback. Fires after every outermost Transaction
    /// commits with at least one op. Replaces any previous callback.
    void setOnCommit(OnCommitFn fn) { m_onCommit = std::move(fn); }

    void undo();
    void redo();
    void undoForBlock(BlockId block);
    void compact(IsCollapsedQuery query);

    template <typename Body>
    void maybeCoalesceOrTransaction(const CoalesceContext &ctx, Body &&body) {
        bool canExtend = !m_entries.empty()
            && m_lastCoalesceCtx.has_value()
            && ctx.isPrintable
            && m_lastCoalesceCtx->isPrintable
            && ctx.block == m_lastCoalesceCtx->block
            && ctx.focusGeneration == m_lastCoalesceCtx->focusGeneration
            && (ctx.timestampMs - m_lastCoalesceCtx->timestampMs) < 1000;

        if (canExtend) {
            // Extend last entry: set m_pendingEntry so the Transaction sees
            // itself as inner (no new entry pushed, no entry popped on dtor).
            // The body must not push new entries — asserted after.
            const size_t sizeBefore = m_entries.size();
            UndoEntry &entry = m_entries.back();
            const size_t targetsBefore = entry.targets.size();
            m_pendingEntry = &entry;
            Transaction t(*this);  // sees m_pendingEntry != nullptr → inner
            body(t);
            Q_ASSERT(m_entries.size() == sizeBefore); // body must not push entries
            m_pendingEntry = nullptr;

            // Because this Transaction is seen as inner, its destructor
            // takes the non-outermost branch and never calls m_onCommit —
            // that path is reserved for the entry's first (outermost)
            // Transaction. Left alone, every coalesced-in op after the
            // first would be applied to the local document but never
            // reported to onCommit, which is what MarkoffDocument wires to
            // localOpsProduced (see MarkoffDocument.cpp's D5 onCommit
            // hookup) — collab peers would silently never receive those
            // ops. Fire onCommit here for exactly the ops THIS call added
            // (targetsBefore..end), so each keystroke's op is still
            // reported individually and in order, even though it lands in
            // the same undo entry as the previous keystroke for coalesced
            // undo/redo granularity. Root-caused 2026-08-14 as a
            // convergence regression from P7.2a routing insertPrintable
            // through Cmd::insertCharacter's coalescing path.
            if (m_onCommit && entry.targets.size() > targetsBefore) {
                std::vector<CommittedOp> committed;
                committed.reserve(entry.targets.size() - targetsBefore);
                for (size_t i = targetsBefore; i < entry.targets.size(); ++i)
                    committed.push_back({entry.targets[i].first, entry.targets[i].second});
                m_onCommit(committed, entry.actionId);
            }
        } else {
            // m_pendingEntry == nullptr → Transaction ctor pushes new entry
            Transaction t(*this);
            body(t);
        }
        m_lastCoalesceCtx = ctx;
    }

private:
    friend class Transaction;

    std::vector<UndoEntry> m_entries;
    std::vector<UndoEntry> m_redoStack;
    UndoEntry *m_pendingEntry = nullptr;
    int m_nestingDepth = 0;
    UndoActionId m_nextActionId = 1;
    Dispatcher m_dispatcher;
    OnCommitFn m_onCommit;
    std::optional<CoalesceContext> m_lastCoalesceCtx;
};

}  // namespace Markoff
