// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/CausalLwwMap.h>
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/UndoLog.h>
#include <markoff-foundation/CrdtProxies.h>
#include <markoff-foundation/KindTagMap.h>
#include <markoff-foundation/BlockAttrsMap.h>
#include <markoff-foundation/FrontmatterMap.h>
#include <markoff-foundation/LinkRefMap.h>
#include <markoff-foundation/FootnoteDefMap.h>

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
// ParsePool is deprecated (D4 will delete it); foundation still owns the impl.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include "ParsePool.h"
#pragma GCC diagnostic pop

#include <markoff-foundation/InlineParseCache.h>

namespace Markoff {

class Session;
class WatermarkCoordinator;  // full type in WatermarkCoordinator.h; Private holds unique_ptr;

// ============================================================================
// MarkoffDocument::Private
// ============================================================================

struct MarkoffDocument::Private {
    explicit Private(uint16_t replicaId)
        : buffer(replicaId)
        , replicaId(replicaId)
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
    quint64                                   editSequence = 0;   ///< Bumps on every state-change op.
    quint64                                   parseSequence = 0;  ///< Bumps each time parseUpdated is emitted.
    QList<Markoff::BlockAnchor>               latestBlockAnchors;
    QList<Markoff::Detail::BlockByteRange>    latestBlockRanges;
    QList<Session *>                          sessions;
QT_WARNING_PUSH
QT_WARNING_DISABLE_GCC("-Wdeprecated-declarations")
    Markoff::Parse::Detail::ParsePool         parsePool;
QT_WARNING_POP
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
