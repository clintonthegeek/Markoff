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
            m_pendingEntry = &m_entries.back();
            Transaction t(*this);  // sees m_pendingEntry != nullptr → inner
            body(t);
            Q_ASSERT(m_entries.size() == sizeBefore); // body must not push entries
            m_pendingEntry = nullptr;
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
    std::optional<CoalesceContext> m_lastCoalesceCtx;
};

}  // namespace Markoff
