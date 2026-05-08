// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/MarkoffOp.h>
#include <markoff/core/BlockSerializerRegistry.h>
#include <markoff/core/CausalLwwMap.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/CrdtProxies.h>
#include <markoff/core/KindTagMap.h>
#include <markoff/core/BlockAttrsMap.h>
#include <markoff/core/FrontmatterMap.h>
#include <markoff/core/LinkRefMap.h>
#include <markoff/core/FootnoteDefMap.h>

#include <QList>
#include <QHash>
#include <QSet>
#include <QByteArray>
#include <QString>

#include <atomic>
#include <memory>
#include <unordered_map>
#include <variant>

#include <crdt/Buffer.h>
#include <crdt/IdList.h>

#include "BlockAnchorComputation.h"

#include <markoff/core/InlineParseCache.h>

namespace Markoff {

class Session;
class WatermarkCoordinator;  // full type in WatermarkCoordinator.h; Private holds unique_ptr;

// ============================================================================
// MarkoffDocument::Private
// ============================================================================

struct MarkoffDocument::Private {
    explicit Private(uint16_t replicaId,
                     const Markoff::BlockSerializerRegistry *registry = nullptr)
        : buffer(replicaId)
        , replicaId(replicaId)
        , serializerRegistry(registry)
        , idList(replicaId)
        , kindTagMap(replicaId)
        , blockAttrsMap(replicaId)
        , frontmatterMap(replicaId)
        , linkRefMap(replicaId)
        , footnoteDefMap(replicaId)
    {}

    // ── Legacy single-buffer internals (retained until Phase 14) ──────────
    CollabText::Crdt::Buffer                  buffer;
    quint16                                   replicaId;
    bool                                      collabConfigured = false;
    const Markoff::BlockSerializerRegistry   *serializerRegistry = nullptr;
    quint64                                   editSequence = 0;   ///< Bumps on every state-change op.
    QList<Markoff::BlockAnchor>               latestBlockAnchors;
    QList<Markoff::Detail::BlockByteRange>    latestBlockRanges;
    QList<Session *>                          sessions;
    std::unique_ptr<const Markoff::Document>  latestParse;

    // ── New D2 internals (Phase 4+) ──────────────────────────────────────
    // BlockId hash for std::unordered_map
    struct BlockIdHash {
        std::size_t operator()(const BlockId &id) const noexcept {
            return std::hash<uint64_t>{}(id.raw());
        }
    };

    CollabText::Crdt::IdList                           idList;
    std::unordered_map<BlockId, std::unique_ptr<CollabText::Crdt::Buffer>, BlockIdHash> blockBuffers;
    KindTagMap      kindTagMap;
    BlockAttrsMap   blockAttrsMap;
    FrontmatterMap  frontmatterMap;
    LinkRefMap      linkRefMap;
    FootnoteDefMap  footnoteDefMap;
    Markoff::UndoLog undoLog;
    std::unique_ptr<Markoff::WatermarkCoordinator> watermark;
    std::unique_ptr<Markoff::InlineParseCache> inlineCache;

    // Per-block edit sequence counters (D2 dirty-tracking)
    QHash<BlockId, quint64> blockEditSequences;

    // Load-time bytes snapshot — set during loadFromMarkdown, used by
    // Phase 8 touch-test to check whether block content has changed.
    QHash<BlockId, QByteArray> blockLoadTimeBytes;

    // Structural edit sequence counter — bumps on every applyStructural call.
    quint64 structuralEditSequence = 0;

    // Blocks whose kind or attrs changed post-load (Phase 8 touch-test).
    // Cleared in loadFromMarkdown; populated by d2SetBlockKind/d2SetBlockAttr.
    QSet<BlockId> touchedSinceLoad;

    // Debounce flag for d2DocumentChanged signal
    bool d2ChangePending = false;

    // D5: monotonic bundle-ID counter; starts at 1 (0 is reserved / invalid)
    quint64 nextBundleId = 1;

    // D5: Build a MarkoffBundleMeta for a transaction that just committed.
    // actionIdRaw is the raw UndoLog UndoActionId (uint64_t); cast to the
    // public ActionId enum class here to bridge the two distinct type aliases.
    // opCountInBundle is left 0 and filled in by the emission site.
    MarkoffBundleMeta buildBundleMeta(quint64 actionIdRaw, quint64 producerLamport)
    {
        MarkoffBundleMeta meta;
        meta.producerReplicaId = replicaId;
        meta.bundleId          = nextBundleId++;
        meta.opCountInBundle   = 0;
        meta.actionId          = static_cast<ActionId>(actionIdRaw);
        meta.producerLamport   = producerLamport;
        return meta;
    }

    // D5: op payloads pending commit — keyed by OpId, populated at each CRDT
    // edit site, consumed (and removed) by the OnCommit callback.
    QHash<Markoff::OpId, QByteArray> pendingOpPayloads;

    // D5: buffer ops for blocks not yet present in blockBuffers (arrived before
    // the IdList op that inserts them). Drained when the IdList op lands.
    QHash<BlockId, QList<QByteArray>> pendingBufferOps;

    // Load-time block ID counter — block IDs minted during loadFromMarkdown
    // start here to avoid colliding with testInsertBlock (1...) and
    // d2InsertBlock (0x2000000...) and applyStructural (0x1000000...) ranges.
    std::atomic<uint64_t> nextBlockId{0x3000000};

    // Per-CRDT Qt signal proxies (parented to the MarkoffDocument; Qt owns lifetime).
    // Raw pointers are safe: these are QObjects with a parent and are destroyed
    // when the parent MarkoffDocument is destroyed.
    QHash<BlockId, Markoff::BufferProxy *> bufferProxies;
    Markoff::IdListProxy     *idListProxy     = nullptr;
    Markoff::SiblingMapProxy *kindTagMapProxy = nullptr;
    Markoff::SiblingMapProxy *blockAttrsMapProxy   = nullptr;
    Markoff::SiblingMapProxy *frontmatterMapProxy  = nullptr;
    Markoff::SiblingMapProxy *linkRefMapProxy      = nullptr;
    Markoff::SiblingMapProxy *footnoteDefMapProxy  = nullptr;
};

}  // namespace Markoff
