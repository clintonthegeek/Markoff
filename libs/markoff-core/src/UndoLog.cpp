// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/UndoLog.h>
#include <algorithm>
#include <QtAssert>

namespace Markoff {

UndoLog::Transaction::Transaction(UndoLog &log) : m_log(log) {
    if (log.m_pendingEntry != nullptr) {
        // Already inside a transaction (or coalescing) — we're inner.
        m_isOutermost = false;
    } else {
        m_isOutermost = true;
        log.m_entries.push_back(UndoEntry{log.m_nextActionId++, {}});
        log.m_pendingEntry = &log.m_entries.back();
        log.m_redoStack.clear();  // new action invalidates redo
    }
    ++log.m_nestingDepth;
}

UndoLog::Transaction::~Transaction() {
    --m_log.m_nestingDepth;
    if (m_isOutermost) {
        if (m_rolledBack || m_log.m_pendingEntry->targets.empty()) {
            m_log.m_entries.pop_back();
            --m_log.m_nextActionId;  // reclaim action id — entry was discarded
        }
        m_log.m_pendingEntry = nullptr;
    }
}

void UndoLog::Transaction::registerOp(UndoCrdtTarget target, OpId opId) {
    Q_ASSERT(m_log.m_pendingEntry && "registerOp called outside a transaction");
    m_log.m_pendingEntry->targets.emplace_back(std::move(target), opId);
}

void UndoLog::Transaction::rollback() noexcept { m_rolledBack = true; }

void UndoLog::undo() {
    if (m_entries.empty() || !m_dispatcher) return;
    UndoEntry e = std::move(m_entries.back());
    m_entries.pop_back();
    for (auto it = e.targets.rbegin(); it != e.targets.rend(); ++it)
        m_dispatcher(it->first, it->second, /*forward=*/false);
    m_redoStack.push_back(std::move(e));
}

void UndoLog::redo() {
    if (m_redoStack.empty() || !m_dispatcher) return;
    UndoEntry e = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    for (auto &[target, opId] : e.targets)
        m_dispatcher(target, opId, /*forward=*/true);
    m_entries.push_back(std::move(e));
}

void UndoLog::undoForBlock(BlockId block) {
    if (!m_dispatcher) return;
    auto it = std::find_if(m_entries.rbegin(), m_entries.rend(), [&](const UndoEntry &e) {
        return std::any_of(e.targets.begin(), e.targets.end(), [&](const auto &p) {
            if (auto *b = std::get_if<UndoCrdtTarget::BufferT>(&p.first.kind))
                return b->blockId == block;
            return false;
        });
    });
    if (it == m_entries.rend()) return;
    UndoEntry e = std::move(*it);
    m_entries.erase(std::next(it).base());
    for (auto rit = e.targets.rbegin(); rit != e.targets.rend(); ++rit)
        m_dispatcher(rit->first, rit->second, /*forward=*/false);
    m_redoStack.push_back(std::move(e));
}

void UndoLog::compact(IsCollapsedQuery query) {
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(), [&](const UndoEntry &e) {
            return std::all_of(e.targets.begin(), e.targets.end(),
                [&](const auto &p) { return query(p.first, p.second); });
        }),
        m_entries.end()
    );
}

}  // namespace Markoff
