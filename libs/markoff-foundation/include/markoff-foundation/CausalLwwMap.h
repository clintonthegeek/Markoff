// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <tuple>
#include <vector>
#include <QHash>

namespace Markoff {

struct CausalStamp {
    uint16_t replicaId;
    uint64_t counter;

    bool operator<(const CausalStamp &o) const noexcept {
        return std::tie(counter, replicaId) < std::tie(o.counter, o.replicaId);
    }
    bool operator==(const CausalStamp &o) const noexcept {
        return counter == o.counter && replicaId == o.replicaId;
    }
    bool operator<=(const CausalStamp &o) const noexcept {
        return !(o < *this);
    }
    bool operator>(const CausalStamp &o) const noexcept {
        return o < *this;
    }
};

template <typename Key, typename Value>
class CausalLwwMap {
public:
    using OpId = uint64_t;  // (replicaId << 48) | counter

    struct RemoteOp { Key key; Value value; CausalStamp stamp; bool tombstone; };

    using ChangeCallback = std::function<void(const Key &, std::optional<Value>, std::optional<Value>)>;

    explicit CausalLwwMap(uint16_t replicaId) : m_replicaId(replicaId) {}

    std::optional<Value> get(const Key &k) const {
        auto it = m_entries.constFind(k);
        if (it == m_entries.cend()) return std::nullopt;
        if (it->tombstone) return std::nullopt;
        return it->value;
    }

    // Local write with explicit stamp (e.g. for remote-merge or replaying ops).
    // Returns true if the write was accepted (stamp wins).
    bool set(const Key &k, Value v, CausalStamp stamp) {
        auto it = m_entries.find(k);
        std::optional<Entry> oldEntry;
        if (it != m_entries.end()) {
            if (stamp <= it->stamp) return false;  // stale — reject
            oldEntry = *it;
        }
        Entry newEntry{std::move(v), stamp, false};
        m_entries.insert(k, newEntry);
        recordUndo(k, oldEntry, newEntry);
        fireChange(k, oldEntry, newEntry);
        return true;
    }

    bool remove(const Key &k, CausalStamp stamp) {
        auto it = m_entries.find(k);
        std::optional<Entry> oldEntry;
        if (it != m_entries.end()) {
            if (stamp <= it->stamp) return false;
            oldEntry = *it;
        }
        Entry newEntry{Value{}, stamp, true};
        m_entries.insert(k, newEntry);
        recordUndo(k, oldEntry, newEntry);
        fireChange(k, oldEntry, newEntry);
        return true;
    }

    // Convenience: use internal counter for local edits.
    OpId setWithNextStamp(const Key &k, Value v) {
        CausalStamp s = nextStamp();
        set(k, std::move(v), s);
        return stampToOpId(s);
    }

    OpId removeWithNextStamp(const Key &k) {
        CausalStamp s = nextStamp();
        remove(k, s);
        return stampToOpId(s);
    }

    CausalStamp nextStamp() { return CausalStamp{m_replicaId, ++m_localCounter}; }
    CausalStamp currentStamp() const noexcept { return CausalStamp{m_replicaId, m_localCounter}; }

    static OpId stampToOpId(CausalStamp s) noexcept {
        return (static_cast<uint64_t>(s.replicaId) << 48) | (s.counter & 0x0000FFFFFFFFFFFFull);
    }
    static CausalStamp opIdToStamp(OpId id) noexcept {
        return CausalStamp{static_cast<uint16_t>(id >> 48), id & 0x0000FFFFFFFFFFFFull};
    }

    void setOnChange(ChangeCallback cb) { m_onChange = std::move(cb); }

    // Undo the most recent local write.
    void undo() {
        if (m_undoStack.empty()) return;
        UndoOp op = m_undoStack.back();
        m_undoStack.pop_back();
        m_redoStack.push_back(op);

        std::optional<Entry> before = op.before;
        Entry after = op.after;  // current (to be reverted)

        if (before.has_value()) {
            m_entries.insert(op.key, *before);
        } else {
            m_entries.remove(op.key);
        }

        // Fire change callback
        if (m_onChange) {
            std::optional<Value> oldV = (!after.tombstone) ? std::optional<Value>(after.value) : std::nullopt;
            std::optional<Value> newV;
            if (before.has_value() && !before->tombstone) newV = before->value;
            m_onChange(op.key, oldV, newV);
        }
    }

    // Redo the most recently undone write.
    void redo() {
        if (m_redoStack.empty()) return;
        UndoOp op = m_redoStack.back();
        m_redoStack.pop_back();
        m_undoStack.push_back(op);

        Entry after = op.after;
        m_entries.insert(op.key, after);

        if (m_onChange) {
            std::optional<Value> oldV;
            if (op.before.has_value() && !op.before->tombstone) oldV = op.before->value;
            std::optional<Value> newV = (!after.tombstone) ? std::optional<Value>(after.value) : std::nullopt;
            m_onChange(op.key, oldV, newV);
        }
    }

    // Remove undo entries at or below watermark (they are permanently committed).
    void compact(CausalStamp watermark) {
        m_undoStack.erase(
            std::remove_if(m_undoStack.begin(), m_undoStack.end(),
                [&](const UndoOp &op) { return op.after.stamp <= watermark; }),
            m_undoStack.end()
        );
    }

    /// Iterate all non-tombstoned entries. Calls fn(key, value) for each live entry.
    /// Order is unspecified (QHash iteration order).
    template <typename F>
    void forEachValue(F &&fn) const {
        for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
            if (!it->tombstone)
                fn(it.key(), it->value);
        }
    }

    // Apply a write from a remote replica — does NOT enter the local undo stack.
    void applyRemote(const RemoteOp &op) {
        auto it = m_entries.find(op.key);
        if (it == m_entries.end() || it->stamp < op.stamp) {
            std::optional<Entry> oldEntry = (it != m_entries.end()) ? std::optional<Entry>(*it) : std::nullopt;
            Entry newEntry{op.value, op.stamp, op.tombstone};
            m_entries.insert(op.key, newEntry);
            if (m_onChange) {
                std::optional<Value> oldV;
                if (oldEntry.has_value() && !oldEntry->tombstone) oldV = oldEntry->value;
                std::optional<Value> newV = (!op.tombstone) ? std::optional<Value>(op.value) : std::nullopt;
                m_onChange(op.key, oldV, newV);
            }
        }
    }

private:
    struct Entry { Value value; CausalStamp stamp; bool tombstone = false; };
    struct UndoOp {
        Key key;
        std::optional<Entry> before;  // nullopt if key didn't exist
        Entry after;
    };

    void recordUndo(const Key &k, std::optional<Entry> before, Entry after) {
        m_undoStack.push_back({k, std::move(before), std::move(after)});
        m_redoStack.clear();
    }

    void fireChange(const Key &k, const std::optional<Entry> &oldEntry, const Entry &newEntry) {
        if (!m_onChange) return;
        std::optional<Value> oldV;
        if (oldEntry.has_value() && !oldEntry->tombstone) oldV = oldEntry->value;
        std::optional<Value> newV = (!newEntry.tombstone) ? std::optional<Value>(newEntry.value) : std::nullopt;
        m_onChange(k, oldV, newV);
    }

    uint16_t m_replicaId;
    uint64_t m_localCounter = 0;
    QHash<Key, Entry> m_entries;
    std::vector<UndoOp> m_undoStack;
    std::vector<UndoOp> m_redoStack;
    ChangeCallback m_onChange;
};

}  // namespace Markoff
