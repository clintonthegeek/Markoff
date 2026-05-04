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
#include <QByteArray>
#include <QString>

#include <memory>
#include <unordered_map>
#include <variant>

#include <crdt/Buffer.h>
#include <crdt/IdList.h>

#include "BlockAnchorComputation.h"
#include "ParsePool.h"

namespace Markoff {

class Session;

// Stub placeholders — real implementations in their phases
class WatermarkCoordinator { public: explicit WatermarkCoordinator() = default; };
class InlineParseCache     { public: explicit InlineParseCache() = default; };

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
    Markoff::Parse::Detail::ParsePool         parsePool;
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
    WatermarkCoordinator watermark;
    InlineParseCache     inlineCache;

    // Per-block edit sequence counters (D2 dirty-tracking)
    QHash<BlockId, quint64> blockEditSequences;

    // Structural edit sequence counter — bumps on every applyStructural call.
    quint64 structuralEditSequence = 0;

    // Debounce flag for d2DocumentChanged signal
    bool d2ChangePending = false;

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
