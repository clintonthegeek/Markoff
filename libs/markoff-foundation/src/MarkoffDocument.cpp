// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/BlockEdit.h>
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/StructuralOp.h>
#include <markoff-foundation/UndoLog.h>
#include <markoff-foundation/BlockSerializer.h>
#include <markoff-foundation/AttrNames.h>

#include <QSaveFile>

#include <markoff-parser/Document.h>

#include <algorithm>
#include <atomic>

#include <QTimer>

#include <crdt/IdList.h>
#include <crdt/IdListOperations.h>

#include "MarkoffDocumentPrivate.h"
#include "AnchorConversion.h"
#include "BlockAnchorComputation.h"
#include <markoff-foundation/WatermarkCoordinator.h>
#include <markoff-foundation/InlineParseCache.h>

namespace {

// Convert a CollabText::Crdt::Lamport timestamp to the UndoLog OpId encoding.
static Markoff::OpId lamportToOpId(CollabText::Crdt::Lamport ts) noexcept {
    return (static_cast<uint64_t>(ts.replica_id) << 48)
         | (static_cast<uint64_t>(ts.value) & 0x0000FFFFFFFFFFFFull);
}

}  // anonymous namespace

namespace Markoff {

MarkoffDocument::MarkoffDocument(quint16 replicaId,
                                 const Markoff::BlockSerializerRegistry *registry,
                                 QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(replicaId, registry))
{
    // Runtime registration so QList<BlockId> (= QList<BlockAnchor>) survives
    // any cross-thread QueuedConnection slot. Q_DECLARE_METATYPE on its own
    // is compile-time only; queued connections need this.
    qRegisterMetaType<Markoff::BlockId>("Markoff::BlockId");
    qRegisterMetaType<Markoff::BlockAnchor>("Markoff::BlockAnchor");
    qRegisterMetaType<QList<Markoff::BlockAnchor>>("QList<Markoff::BlockAnchor>");

    // ── D2: initialise per-CRDT signal proxies ───────────────────────────
    d->idListProxy          = new IdListProxy(this);
    d->kindTagMapProxy      = new SiblingMapProxy(this);
    d->blockAttrsMapProxy   = new SiblingMapProxy(this);
    d->frontmatterMapProxy  = new SiblingMapProxy(this);
    d->linkRefMapProxy      = new SiblingMapProxy(this);
    d->footnoteDefMapProxy  = new SiblingMapProxy(this);

    // ── D2: wire UndoLog dispatcher ──────────────────────────────────────
    d->undoLog.setDispatcher([this](const CrdtTarget &target, OpId opId, bool forward) {
        std::visit([&](const auto &t) {
            using T = std::decay_t<decltype(t)>;
            if constexpr (std::is_same_v<T, CrdtTarget::BufferT>) {
                auto it = d->blockBuffers.find(t.blockId);
                if (it == d->blockBuffers.end()) return;
                if (forward) it->second->redo();
                else         it->second->undo();
            } else if constexpr (std::is_same_v<T, CrdtTarget::IdListT>) {
                if (forward) d->idList.redo();
                else         d->idList.undo();
            } else if constexpr (std::is_same_v<T, CrdtTarget::KindTagMapT>) {
                if (forward) d->kindTagMap.redo();
                else         d->kindTagMap.undo();
            } else if constexpr (std::is_same_v<T, CrdtTarget::BlockAttrsMapT>) {
                if (forward) d->blockAttrsMap.redo();
                else         d->blockAttrsMap.undo();
            } else if constexpr (std::is_same_v<T, CrdtTarget::FrontmatterMapT>) {
                if (forward) d->frontmatterMap.redo();
                else         d->frontmatterMap.undo();
            } else if constexpr (std::is_same_v<T, CrdtTarget::LinkRefMapT>) {
                if (forward) d->linkRefMap.redo();
                else         d->linkRefMap.undo();
            } else if constexpr (std::is_same_v<T, CrdtTarget::FootnoteDefMapT>) {
                if (forward) d->footnoteDefMap.redo();
                else         d->footnoteDefMap.undo();
            }
            (void)opId;  // opId used for CausalLwwMap-specific undo; IdList/Buffer use stack-based undo
        }, target.kind);
    });

    // ── D2: initialise WatermarkCoordinator (Phase 9) ───────────────────────
    d->watermark = std::make_unique<WatermarkCoordinator>(*this);

    // ── D2: initialise InlineParseCache (Phase 10) ──────────────────────────
    d->inlineCache = std::make_unique<InlineParseCache>(*this);

}

MarkoffDocument::~MarkoffDocument() = default;

QByteArray MarkoffDocument::toMarkdownUtf8() const
{
    const std::string s = d->buffer.text();
    return QByteArray::fromStdString(s);
}

QString MarkoffDocument::toMarkdown() const
{
    return QString::fromUtf8(toMarkdownUtf8());
}

quint32 MarkoffDocument::visibleLength() const
{
    return d->buffer.visible_length();
}

const Markoff::Document *MarkoffDocument::parsedDocument() const
{
    return d->latestParse.get();
}

quint16 MarkoffDocument::replicaId() const
{
    return d->replicaId;
}

CollabText::Crdt::Global MarkoffDocument::version() const
{
    return d->buffer.version();
}

quint64 MarkoffDocument::editSequence() const noexcept
{
    return d->editSequence;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::undo()
{
    auto op = d->buffer.undo();
    if (!op.has_value())
        return std::nullopt;

    ++d->editSequence;
    return op;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::redo()
{
    auto op = d->buffer.redo();
    if (!op.has_value())
        return std::nullopt;

    ++d->editSequence;
    return op;
}

int MarkoffDocument::undoDepth() const
{
    return static_cast<int>(d->buffer.undo_depth());
}

bool MarkoffDocument::coalesceLastUndo()
{
    return d->buffer.coalesce_last_undo();
}

void MarkoffDocument::applyRemoteOps(
    const std::vector<CollabText::Crdt::Operation> &ops)
{
    if (ops.empty())
        return;

    ++d->editSequence;
    d->buffer.apply_ops(ops);
}

void MarkoffDocument::resetContent(const QByteArray &newContent, Origin origin)
{
    ++d->editSequence;

    switch (origin) {
    case Origin::FirstOpen:
    case Origin::ExternalReloadClean:
    case Origin::ExternalReloadResolved:
    case Origin::TestFixture: {
        d->buffer = CollabText::Crdt::Buffer(d->replicaId);
        if (!newContent.isEmpty()) {
            std::vector<std::pair<uint32_t, uint32_t>> ranges{ {0, 0} };
            std::vector<std::string> texts{
                std::string(newContent.constData(),
                            static_cast<size_t>(newContent.size())) };
            d->buffer.apply_local_edit(ranges, texts);
            // Clear the undo stack so this seed is not undoable.
            // Buffer doesn't expose a clear-undo API directly, but
            // set_max_undo_depth(0) trims the stack on shrink.  Reset
            // back to the default afterwards so subsequent local edits
            // can still be undone.
            const auto savedDepth = d->buffer.max_undo_depth();
            d->buffer.set_max_undo_depth(0);
            d->buffer.set_max_undo_depth(savedDepth);
        }
        break;
    }
    case Origin::UserRevertToSaved: {
        // Push one mega-edit that replaces the entire buffer with newContent.
        // Undo reverses the revert.
        const auto curLen = d->buffer.visible_length();
        std::vector<std::pair<uint32_t, uint32_t>> ranges{ {0u, curLen} };
        std::vector<std::string> texts{
            std::string(newContent.constData(),
                        static_cast<size_t>(newContent.size())) };
        d->buffer.apply_local_edit(ranges, texts);
        break;
    }
    case Origin::UserEdit:
        Q_UNREACHABLE();
        break;
    }
    Q_EMIT documentReloaded();
}

CollabText::Crdt::Anchor
MarkoffDocument::anchorAt(quint32 byteOffset, CollabText::Crdt::Bias bias) const
{
    return d->buffer.anchor_at(byteOffset, bias);
}

quint32 MarkoffDocument::resolveAnchor(const CollabText::Crdt::Anchor &a) const
{
    return d->buffer.resolve_anchor(a);
}

TextAnchor MarkoffDocument::textAnchorAt(quint32 byteOffset, bool rightBias) const
{
    // D2 path: if D2 block buffers exist, anchor within the per-block CRDT.
    const auto blocks = iterateBlocks();
    if (!blocks.empty()) {
        const auto bias = rightBias ? CollabText::Crdt::Bias::Right : CollabText::Crdt::Bias::Left;
        uint32_t cursor = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const BlockId id = blocks[i];
            auto it = d->blockBuffers.find(id);
            if (it == d->blockBuffers.end()) continue;
            const uint32_t sz = static_cast<uint32_t>(it->second->visible_length());
            const uint32_t blkEnd = cursor + sz;
            // Last block or byteOffset falls within this block.
            if (byteOffset <= blkEnd || i + 1 == blocks.size()) {
                const uint32_t localOff = byteOffset >= cursor
                    ? std::min(byteOffset - cursor, sz)
                    : 0;
                const CollabText::Crdt::Anchor a = it->second->anchor_at(localOff, bias);
                return Detail::toTextAnchor(id, a);
            }
            cursor = blkEnd;
        }
    }

    // Legacy fallback (pre-D2 or single-buffer path).
    BlockId blockId;
    for (int i = 0; i < d->latestBlockRanges.size(); ++i) {
        const auto &r = d->latestBlockRanges[i];
        if (byteOffset >= r.startByte && byteOffset <= r.endByte) {
            blockId = d->latestBlockAnchors[i];
            break;
        }
    }
    const auto bias = rightBias ? CollabText::Crdt::Bias::Right : CollabText::Crdt::Bias::Left;
    const CollabText::Crdt::Anchor a = anchorAt(byteOffset, bias);
    return Detail::toTextAnchor(blockId, a);
}

quint32 MarkoffDocument::resolveTextAnchor(const TextAnchor &t) const
{
    // D2 path: if the TextAnchor carries a block id, resolve against that block's
    // per-block CRDT buffer and add the cumulative offset of preceding blocks.
    const BlockId blk = t.block();
    if (!blk.isNull()) {
        auto it = d->blockBuffers.find(blk);
        if (it != d->blockBuffers.end()) {
            const CollabText::Crdt::Anchor a = Detail::toCrdtAnchor(t);
            const uint32_t localOff = it->second->resolve_anchor(a);
            // Sum the sizes of all preceding blocks.
            uint32_t baseOffset = 0;
            for (const auto &blockId : iterateBlocks()) {
                if (blockId == blk) break;
                auto jt = d->blockBuffers.find(blockId);
                if (jt != d->blockBuffers.end())
                    baseOffset += static_cast<uint32_t>(jt->second->visible_length());
            }
            return baseOffset + localOff;
        }
    }

    // Legacy fallback.
    return resolveAnchor(Detail::toCrdtAnchor(t));
}

std::optional<BlockAnchor> MarkoffDocument::blockAnchorAt(int blockIndex) const
{
    if (blockIndex < 0) return std::nullopt;
    if (blockIndex >= d->latestBlockAnchors.size()) return std::nullopt;
    return d->latestBlockAnchors.at(blockIndex);
}

std::optional<std::pair<quint32, quint32>>
MarkoffDocument::blockByteRange(const BlockAnchor &b) const
{
    for (int i = 0; i < d->latestBlockAnchors.size(); ++i) {
        if (d->latestBlockAnchors[i] == b) {
            const auto &r = d->latestBlockRanges[i];
            return std::make_pair(r.startByte, r.endByte);
        }
    }
    return std::nullopt;
}

std::optional<BlockAnchor> MarkoffDocument::blockAt(const TextAnchor &t) const
{
    const quint32 byte = resolveTextAnchor(t);
    for (int i = 0; i < d->latestBlockRanges.size(); ++i) {
        const auto &r = d->latestBlockRanges[i];
        if (byte >= r.startByte && byte < r.endByte) {
            return d->latestBlockAnchors[i];
        }
    }
    return std::nullopt;
}

int MarkoffDocument::offsetInBlock(const BlockAnchor &b, const TextAnchor &t) const
{
    const auto rng = blockByteRange(b);
    if (!rng.has_value()) return 0;
    const quint32 byte = resolveTextAnchor(t);
    if (byte <= rng->first)  return 0;
    if (byte >= rng->second) return static_cast<int>(rng->second - rng->first);
    return static_cast<int>(byte - rng->first);
}

TextAnchor MarkoffDocument::textAnchorAt(const BlockAnchor &b,
                                         int offset,
                                         bool rightBias) const
{
    const auto rng = blockByteRange(b);
    if (!rng.has_value()) return TextAnchor{};
    const int clamped = std::max(0, std::min(offset,
        static_cast<int>(rng->second - rng->first)));
    return textAnchorAt(rng->first + static_cast<quint32>(clamped), rightBias);
}

Session *MarkoffDocument::createSession(const SessionParams &params)
{
    auto *s = new Session(this, params);
    d->sessions.append(s);
    Q_EMIT sessionCreated(s);
    return s;
}

void MarkoffDocument::destroySession(Session *s)
{
    if (!s) return;
    if (!d->sessions.removeOne(s)) return;
    Q_EMIT sessionDestroyed(s);
    s->deleteLater();
}

QList<Session *> MarkoffDocument::sessions() const
{
    return d->sessions;
}

Session *MarkoffDocument::sessionForParticipant(const QString &participantId) const
{
    for (Session *s : d->sessions)
        if (s->participantId() == participantId) return s;
    return nullptr;
}

qsizetype MarkoffDocument::collectGarbage()
{
    return static_cast<qsizetype>(d->buffer.collect_garbage());
}

qsizetype MarkoffDocument::compact(const CollabText::Crdt::Global &watermark)
{
    return static_cast<qsizetype>(d->buffer.compact(watermark));
}

// ============================================================================
// D2: scheduleD2Changed — debounced d2DocumentChanged signal
// ============================================================================

void MarkoffDocument::scheduleD2Changed()
{
    if (!d->d2ChangePending) {
        d->d2ChangePending = true;
        QTimer::singleShot(0, this, [this]() {
            d->d2ChangePending = false;
            Q_EMIT d2DocumentChanged();
        });
    }
}

// ============================================================================
// D2: applyBlockEdit
// ============================================================================

void MarkoffDocument::applyBlockEdit(const BlockEdit &edit)
{
    auto it = d->blockBuffers.find(edit.blockId);
    if (it == d->blockBuffers.end()) return;  // unknown block; defensive

    UndoLog::Transaction t(d->undoLog);
    auto op = it->second->apply_local_edit(
        {{edit.withinBlockByteOffset, edit.withinBlockByteOffset + edit.removedBytes}},
        {edit.insertedUtf8.toStdString()});
    auto ts = std::visit([](const auto &o) -> CollabText::Crdt::Lamport { return o.timestamp; }, op);
    t.registerOp(CrdtTarget::buffer(edit.blockId), lamportToOpId(ts));

    ++d->blockEditSequences[edit.blockId];

    // Notify per-block buffer proxy synchronously (before debounced d2DocumentChanged).
    auto proxyIt = d->bufferProxies.find(edit.blockId);
    if (proxyIt != d->bufferProxies.end() && proxyIt.value())
        proxyIt.value()->notifyChanged();

    scheduleD2Changed();
}

// ============================================================================
// D2: applyStructural
// ============================================================================

void MarkoffDocument::applyStructural(const StructuralOp &op)
{
    UndoLog::Transaction t(d->undoLog);
    std::visit([&](auto &&payload) {
        using T = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<T, StructuralOp::InsertEntry>) {
            // Find after-anchor in IdList
            CollabText::Crdt::Anchor after = payload.afterBlockId.isNull()
                ? CollabText::Crdt::Anchor::min()
                : d->idList.anchor_of(payload.afterBlockId.raw(), CollabText::Crdt::Bias::Right);
            // Mint a new block ID
            static std::atomic<uint64_t> s_nextStructId{0x1000000};
            uint64_t raw = s_nextStructId.fetch_add(1);
            CollabText::Crdt::IdListOperation idOp = d->idList.insert_after(after, raw);
            BlockId newBlock = BlockId::fromRaw(raw);
            auto idTs = CollabText::Crdt::get_idlist_op_timestamp(idOp);
            t.registerOp(CrdtTarget::idList(), lamportToOpId(idTs));
            // Create buffer
            d->blockBuffers.emplace(newBlock, std::make_unique<CollabText::Crdt::Buffer>(d->replicaId));
            // Create buffer proxy for this block (parented to this; Qt owns it)
            d->bufferProxies.insert(newBlock, new BufferProxy(newBlock, this));
            // Set kind
            OpId kindOpId = d->kindTagMap.setWithNextStamp(newBlock, payload.kind);
            t.registerOp(CrdtTarget::kindTagMap(), kindOpId);
            // Notify structural + kind proxies
            d->idListProxy->notifyChanged();
            d->kindTagMapProxy->notifyChanged();
        } else if constexpr (std::is_same_v<T, StructuralOp::RemoveEntry>) {
            CollabText::Crdt::Anchor anchor = d->idList.anchor_of(payload.blockId.raw(), CollabText::Crdt::Bias::Left);
            CollabText::Crdt::IdListOperation idOp = d->idList.remove_at(anchor);
            auto idTs = CollabText::Crdt::get_idlist_op_timestamp(idOp);
            t.registerOp(CrdtTarget::idList(), lamportToOpId(idTs));
            OpId kindOpId = d->kindTagMap.removeWithNextStamp(payload.blockId);
            t.registerOp(CrdtTarget::kindTagMap(), kindOpId);
            // Buffer retained for GC (Phase 9)
            d->idListProxy->notifyChanged();
            d->kindTagMapProxy->notifyChanged();
        } else if constexpr (std::is_same_v<T, StructuralOp::ChangeKind>) {
            OpId kindOpId = d->kindTagMap.setWithNextStamp(payload.blockId, payload.newKind);
            t.registerOp(CrdtTarget::kindTagMap(), kindOpId);
            d->kindTagMapProxy->notifyChanged();
        }
    }, op.payload);

    ++d->structuralEditSequence;
    scheduleD2Changed();
}

// ============================================================================
// D2: undo/redo/undoForBlock
// ============================================================================

void MarkoffDocument::undoD2() { d->undoLog.undo(); scheduleD2Changed(); }
void MarkoffDocument::redoD2() { d->undoLog.redo(); scheduleD2Changed(); }
void MarkoffDocument::undoForBlock(BlockId block) { d->undoLog.undoForBlock(block); }

const Markoff::BlockSerializerRegistry *MarkoffDocument::serializerRegistry() const
{
    return d->serializerRegistry;
}

bool MarkoffDocument::canUndoForBlock(Markoff::BlockAnchor blockAnchor) const
{
    return d->undoLog.isBlockReferenced(blockAnchor);
}

void MarkoffDocument::toggleListItemChecked(BlockAnchor anchor)
{
    const BlockId id(anchor);
    if (blockKind(id) != BlockKind::ListItem) return;
    const auto attrs = blockAttrs(id);
    if (!attrs.contains(AttrNames::Checked)) return;
    const AttrValue val = attrs.value(AttrNames::Checked);
    const bool *v = std::get_if<bool>(&val);
    if (!v) return;
    UndoLog::Transaction t(d2UndoLog());
    d2SetBlockAttr(id, AttrNames::Checked, !*v, t);
}

// ============================================================================
// D2: block accessors
// ============================================================================

std::vector<BlockId> MarkoffDocument::iterateBlocks() const
{
    auto rawIds = d->idList.ids();
    std::vector<BlockId> out;
    out.reserve(rawIds.size());
    for (auto raw : rawIds) out.push_back(BlockId::fromRaw(raw));
    return out;
}

BlockKind MarkoffDocument::blockKind(BlockId id) const
{
    return d->kindTagMap.get(id).value_or(BlockKind::Paragraph);
}

QByteArray MarkoffDocument::blockText(BlockId id) const
{
    auto it = d->blockBuffers.find(id);
    if (it == d->blockBuffers.end()) return {};
    return QByteArray::fromStdString(it->second->text());
}

quint64 MarkoffDocument::blockEditSequence(BlockId id) const
{
    return d->blockEditSequences.value(id, 0);
}

quint64 MarkoffDocument::d2EditSequence() const noexcept
{
    quint64 sum = d->structuralEditSequence;
    for (const auto &seq : d->blockEditSequences)
        sum += seq;
    return sum;
}

QList<SourceSpan> MarkoffDocument::inlineSpansFor(BlockId id) const
{
    if (d->inlineCache) return d->inlineCache->spansFor(id);
    return {};
}

// ============================================================================
// D2: testInsertBlock (declared in public header under MARKOFF_TESTING guard;
// implementation always compiled so test executables can link against the lib)
// ============================================================================

BlockId MarkoffDocument::testInsertBlock(BlockKind kind, const QByteArray &content)
{
    static std::atomic<uint64_t> s_nextTestId{1};
    BlockId newId = BlockId::fromRaw(s_nextTestId.fetch_add(1));

    // Insert into IdList at the end
    CollabText::Crdt::Anchor afterAnchor = (d->idList.size() == 0)
        ? CollabText::Crdt::Anchor::min()
        : d->idList.anchor_of(d->idList.ids().back(), CollabText::Crdt::Bias::Right);
    d->idList.insert_after(afterAnchor, newId.raw());

    // Create a Buffer for it
    auto buf = std::make_unique<CollabText::Crdt::Buffer>(d->replicaId);
    if (!content.isEmpty()) {
        buf->apply_local_edit({{0, 0}}, {content.toStdString()});
    }
    d->blockBuffers.emplace(newId, std::move(buf));

    // Create buffer proxy for this block (parented to this; Qt owns it)
    d->bufferProxies.insert(newId, new BufferProxy(newId, this));

    // Set kind
    d->kindTagMap.setWithNextStamp(newId, kind);

    return newId;
}

// ============================================================================
// D2: internal helpers (Cmd layer)
// ============================================================================

UndoLog &MarkoffDocument::d2UndoLog() noexcept { return d->undoLog; }

void MarkoffDocument::d2ApplyBufferEdit(BlockId block, uint32_t offset,
                                         uint32_t removedBytes,
                                         const QByteArray &insert,
                                         UndoLog::Transaction &t)
{
    auto it = d->blockBuffers.find(block);
    if (it == d->blockBuffers.end()) return;
    auto op = it->second->apply_local_edit(
        {{offset, offset + removedBytes}},
        {insert.toStdString()});
    auto ts = std::visit([](const auto &o) -> CollabText::Crdt::Lamport { return o.timestamp; }, op);
    t.registerOp(CrdtTarget::buffer(block), lamportToOpId(ts));
    ++d->blockEditSequences[block];

    // Notify per-block buffer proxy synchronously (same as applyBlockEdit).
    auto proxyIt = d->bufferProxies.find(block);
    if (proxyIt != d->bufferProxies.end() && proxyIt.value())
        proxyIt.value()->notifyChanged();

    scheduleD2Changed();
}

BlockId MarkoffDocument::d2InsertBlock(BlockId afterBlock, BlockKind kind,
                                        UndoLog::Transaction &t)
{
    static std::atomic<uint64_t> s_nextId{0x2000000};
    uint64_t raw = s_nextId.fetch_add(1);
    BlockId newId = BlockId::fromRaw(raw);

    CollabText::Crdt::Anchor after = afterBlock.isNull()
        ? CollabText::Crdt::Anchor::min()
        : d->idList.anchor_of(afterBlock.raw(), CollabText::Crdt::Bias::Right);

    auto idOp = d->idList.insert_after(after, raw);
    auto idTs = CollabText::Crdt::get_idlist_op_timestamp(idOp);
    t.registerOp(CrdtTarget::idList(), lamportToOpId(idTs));

    d->blockBuffers.emplace(newId, std::make_unique<CollabText::Crdt::Buffer>(d->replicaId));
    d->bufferProxies.insert(newId, new BufferProxy(newId, this));

    OpId kindOpId = d->kindTagMap.setWithNextStamp(newId, kind);
    t.registerOp(CrdtTarget::kindTagMap(), kindOpId);

    ++d->structuralEditSequence;
    d->idListProxy->notifyChanged();
    d->kindTagMapProxy->notifyChanged();
    scheduleD2Changed();
    return newId;
}

// ===== D4: applyFlatEdit =====

void MarkoffDocument::applyFlatEdit(uint32_t oldStart,
                                    uint32_t oldEnd,
                                    const QByteArray &newText,
                                    Origin origin)
{
    Q_UNUSED(origin);
    Q_ASSERT(oldStart <= oldEnd);

    const auto blocks = iterateBlocks();

    // Walk blocks to find which block(s) the [oldStart, oldEnd) range touches.
    uint32_t cursor = 0;
    int startIdx = -1;
    int endIdx   = -1;
    uint32_t startWithin = 0;
    uint32_t endWithin   = 0;

    for (size_t i = 0; i < blocks.size(); ++i) {
        const uint32_t sz     = static_cast<uint32_t>(blockText(blocks[i]).size());
        const uint32_t blkEnd = cursor + sz;

        if (startIdx == -1 && oldStart <= blkEnd) {
            startIdx    = static_cast<int>(i);
            startWithin = oldStart - cursor;
        }
        if (oldEnd <= blkEnd) {
            endIdx    = static_cast<int>(i);
            endWithin = oldEnd - cursor;
            break;
        }
        cursor = blkEnd;
    }

    // Handle empty-document edge case: both indices unset.
    if (startIdx == -1 && endIdx == -1) {
        if (newText.isEmpty()) return;  // delete on empty doc — no-op
        // Insert into an empty document: auto-create one paragraph block.
        // This handles the case where a fresh MarkoffDocument (0 blocks) receives
        // its first keystroke via the binding's forward path.
        Q_ASSERT(oldStart == 0 && oldEnd == 0);
        UndoLog::Transaction t(d2UndoLog());
        BlockId newBlk = d2InsertBlock(BlockId{}, BlockKind::Paragraph, t);
        d2ApplyBufferEdit(newBlk, 0, 0, newText, t);
        return;
    }
    Q_ASSERT(startIdx >= 0 && endIdx >= 0);

    UndoLog::Transaction t(d2UndoLog());

    // ── Intra-block edit (no embedded newlines) ────────────────────────────
    if (startIdx == endIdx && newText.indexOf('\n') == -1) {
        d2ApplyBufferEdit(blocks[startIdx], startWithin,
                          endWithin - startWithin, newText, t);
        return;
    }

    // ── Intra-block edit with embedded newlines (block split) ─────────────
    if (startIdx == endIdx) {
        const QByteArray currentText = blockText(blocks[startIdx]);
        const uint32_t removeLen = endWithin - startWithin;
        const QByteArray tail = currentText.mid(static_cast<int>(endWithin));

        // Split newText on "\n\n" boundaries to determine new block count.
        QList<QByteArray> parts;
        int cursor2 = 0;
        while (true) {
            const int nextDouble = newText.indexOf("\n\n", cursor2);
            if (nextDouble == -1) {
                parts.append(newText.mid(cursor2));
                break;
            }
            parts.append(newText.mid(cursor2, nextDouble - cursor2));
            cursor2 = nextDouble + 2;
        }
        Q_ASSERT(parts.size() >= 1);

        // Replace the removed range + tail in the current block with the first part.
        // First block ends with '\n' as its delimiter.
        QByteArray firstReplacement = parts.front() + QByteArray("\n");
        d2ApplyBufferEdit(blocks[startIdx], startWithin,
                          removeLen + static_cast<uint32_t>(tail.size()),
                          firstReplacement, t);

        // Insert subsequent blocks for each additional part.
        BlockId after = blocks[startIdx];
        for (int i = 1; i < parts.size(); ++i) {
            BlockId newBlk = d2InsertBlock(after, BlockKind::Paragraph, t);
            const bool isLast = (i == parts.size() - 1);
            QByteArray seed = parts[i];
            if (isLast) {
                seed += tail;  // restore tail into last new block
            } else {
                seed += QByteArray("\n");  // delimiter for non-last blocks
            }
            if (!seed.isEmpty()) {
                d2ApplyBufferEdit(newBlk, 0, 0, seed, t);
            }
            after = newBlk;
        }
        return;
    }

    // ── Cross-block edit ──────────────────────────────────────────────────
    const QByteArray startTextBefore = blockText(blocks[startIdx]);
    const QByteArray endTextBefore   = blockText(blocks[endIdx]);
    const QByteArray endTail = endTextBefore.mid(static_cast<int>(endWithin));

    // Trim the start block: remove from startWithin to its end.
    d2ApplyBufferEdit(blocks[startIdx], startWithin,
                      static_cast<uint32_t>(startTextBefore.size()) - startWithin,
                      QByteArray(), t);

    // Remove intermediate blocks (between startIdx+1 and endIdx-1 inclusive).
    for (int i = startIdx + 1; i < endIdx; ++i) {
        d2RemoveBlock(blocks[i], t);
    }
    // Remove the end block.
    d2RemoveBlock(blocks[endIdx], t);

    // Re-stitch: split newText on "\n\n" boundaries.
    QList<QByteArray> parts;
    int cursor2 = 0;
    while (true) {
        const int nextDouble = newText.indexOf("\n\n", cursor2);
        if (nextDouble == -1) {
            parts.append(newText.mid(cursor2));
            break;
        }
        parts.append(newText.mid(cursor2, nextDouble - cursor2));
        cursor2 = nextDouble + 2;
    }
    Q_ASSERT(parts.size() >= 1);

    if (parts.size() == 1) {
        // No block splits in newText: append newText + endTail into start block.
        QByteArray combined = parts.front() + endTail;
        d2ApplyBufferEdit(blocks[startIdx], startWithin, 0, combined, t);
    } else {
        // newText has embedded "\n\n": append first part to start block,
        // then insert new blocks for the rest.
        QByteArray firstReplacement = parts.front() + QByteArray("\n");
        d2ApplyBufferEdit(blocks[startIdx], startWithin, 0, firstReplacement, t);

        BlockId after = blocks[startIdx];
        for (int i = 1; i < parts.size(); ++i) {
            BlockId newBlk = d2InsertBlock(after, BlockKind::Paragraph, t);
            const bool isLast = (i == parts.size() - 1);
            QByteArray seed = parts[i];
            if (isLast) {
                seed += endTail;
            } else {
                seed += QByteArray("\n");
            }
            if (!seed.isEmpty()) {
                d2ApplyBufferEdit(newBlk, 0, 0, seed, t);
            }
            after = newBlk;
        }
    }
}

void MarkoffDocument::d2RemoveBlock(BlockId block, UndoLog::Transaction &t)
{
    CollabText::Crdt::Anchor anchor = d->idList.anchor_of(block.raw(), CollabText::Crdt::Bias::Left);
    auto idOp = d->idList.remove_at(anchor);
    auto idTs = CollabText::Crdt::get_idlist_op_timestamp(idOp);
    t.registerOp(CrdtTarget::idList(), lamportToOpId(idTs));

    OpId kindOpId = d->kindTagMap.removeWithNextStamp(block);
    t.registerOp(CrdtTarget::kindTagMap(), kindOpId);

    ++d->structuralEditSequence;
    d->idListProxy->notifyChanged();
    d->kindTagMapProxy->notifyChanged();
    scheduleD2Changed();
}

void MarkoffDocument::d2SetBlockKind(BlockId block, BlockKind newKind,
                                      UndoLog::Transaction &t)
{
    OpId opId = d->kindTagMap.setWithNextStamp(block, newKind);
    t.registerOp(CrdtTarget::kindTagMap(), opId);
    d->touchedSinceLoad.insert(block);
    scheduleD2Changed();
}

void MarkoffDocument::d2SetBlockAttr(BlockId block, const QByteArray &attrName,
                                      const AttrValue &value,
                                      UndoLog::Transaction &t)
{
    BlockAttrKey key{block, attrName};
    OpId opId = d->blockAttrsMap.setWithNextStamp(key, value);
    t.registerOp(CrdtTarget::blockAttrsMap(), opId);
    d->touchedSinceLoad.insert(block);
    scheduleD2Changed();
}

// ============================================================================
// D2: CRDT proxy accessors
// ============================================================================

BufferProxy *MarkoffDocument::bufferProxy(BlockId id) const
{
    auto it = d->bufferProxies.constFind(id);
    return it != d->bufferProxies.constEnd() ? it.value() : nullptr;
}

IdListProxy *MarkoffDocument::idListProxy() const { return d->idListProxy; }

SiblingMapProxy *MarkoffDocument::kindTagMapProxy() const { return d->kindTagMapProxy; }

// ============================================================================
// D2: loadFromMarkdown — Phase 7
// ============================================================================

static BlockKind mapTopLevelKind(TopLevelBlock::Kind k)
{
    using Kind = TopLevelBlock::Kind;
    switch (k) {
    case Kind::Paragraph:             return BlockKind::Paragraph;
    case Kind::AtxHeading:
    case Kind::SetextHeading:         return BlockKind::Heading;
    case Kind::FencedCodeBlock:
    case Kind::IndentedCodeBlock:     return BlockKind::CodeBlock;
    case Kind::BlockQuote:            return BlockKind::BlockQuote;
    case Kind::ThematicBreak:         return BlockKind::HorizontalRule;
    case Kind::HtmlBlock:             return BlockKind::HtmlBlock;
    case Kind::Table:                 return BlockKind::Table;
    case Kind::ListItem:               return BlockKind::ListItem;
    default:                          return BlockKind::Paragraph;
    }
}

BlockId MarkoffDocument::allocateD2BlockId() noexcept
{
    return BlockId::fromRaw(d->nextBlockId.fetch_add(1));
}

void MarkoffDocument::materializeBlocksFromParsedDoc(const Markoff::Document &parsed,
                                                      const QString &body)
{
    using TLB = Markoff::TopLevelBlock;
    using CollabText::Crdt::Anchor;
    using CollabText::Crdt::Bias;

    // Pre-convert body to UTF-8 once so byte-range slicing is correct
    // (byteStart/byteEnd from tree-sitter are UTF-8 byte offsets)
    const QByteArray bodyUtf8 = body.toUtf8();

    Anchor lastAnchor = Anchor::min();

    for (const TLB &tb : parsed.topLevelBlocks()) {
        // Route link-ref definitions to LinkRefMap (not IdList)
        if (tb.kind == TLB::Kind::LinkReferenceDefinition) {
            QByteArray rawText = bodyUtf8.mid(tb.byteStart, tb.byteEnd - tb.byteStart);
            // Use first 40 bytes as key; Phase 8 refines with proper label extraction
            d->linkRefMap.setWithNextStamp(rawText.left(40), LinkRefValue{"", ""});
            continue;
        }

        // Mint a new block ID
        BlockId newId = allocateD2BlockId();
        uint64_t rawId = newId.raw();

        // Insert into IdList after the previous block (sequential append)
        d->idList.insert_after(lastAnchor, rawId);
        lastAnchor = d->idList.anchor_of(rawId, Bias::Right);

        // Set kind
        BlockKind kind = mapTopLevelKind(tb.kind);
        d->kindTagMap.setWithNextStamp(newId, kind);

        // Set kind-specific attrs
        if (kind == BlockKind::Heading && tb.headingLevel > 0) {
            d->blockAttrsMap.setWithNextStamp(
                BlockAttrKey{newId, "level"}, AttrValue{tb.headingLevel});
        }
        if (kind == BlockKind::CodeBlock && !tb.codeLanguage.isEmpty()) {
            d->blockAttrsMap.setWithNextStamp(
                BlockAttrKey{newId, "infoString"}, AttrValue{tb.codeLanguage});
        }
        if (kind == BlockKind::ListItem) {
            d->blockAttrsMap.setWithNextStamp(
                BlockAttrKey{newId, AttrNames::IndentLevel}, AttrValue{tb.indentDepth});
            d->blockAttrsMap.setWithNextStamp(
                BlockAttrKey{newId, AttrNames::MarkerStyle}, AttrValue{tb.markerStyle});
            if (tb.markerStyle == QStringLiteral("dot")
             || tb.markerStyle == QStringLiteral("paren")) {
                d->blockAttrsMap.setWithNextStamp(
                    BlockAttrKey{newId, AttrNames::MarkerNumber}, AttrValue{tb.markerNumber});
            }
            if (tb.markerStyle == QStringLiteral("task")) {
                d->blockAttrsMap.setWithNextStamp(
                    BlockAttrKey{newId, AttrNames::Checked}, AttrValue{tb.checked});
            }
            d->blockAttrsMap.setWithNextStamp(
                BlockAttrKey{newId, AttrNames::LooseRun}, AttrValue{tb.looseRun});
        }

        // Buffer content: full source range in UTF-8 bytes
        // For FencedCodeBlock, full source is stored (fences preserved for round-trip).
        // For ListItem (one per parser list_item node), the buffer holds the
        // item's content only — no marker, no leading indent whitespace, no
        // trailing newlines. Marker and indent are reconstructed from attrs at
        // serialize time.
        QByteArray content = bodyUtf8.mid(tb.byteStart, tb.byteEnd - tb.byteStart);

        auto buf = std::make_unique<CollabText::Crdt::Buffer>(d->replicaId);
        if (!content.isEmpty())
            buf->apply_local_edit({{0, 0}}, {content.toStdString()});
        d->blockBuffers.emplace(newId, std::move(buf));
        d->blockLoadTimeBytes[newId] = content;
        d->bufferProxies[newId] = new BufferProxy(newId, this);
    }
}

void MarkoffDocument::loadFromMarkdown(const QByteArray &src)
{
    // 1. Convert and extract frontmatter / footnotes
    const QString srcStr = QString::fromUtf8(src);
    Markoff::ExtractedSource extracted = Markoff::Document::extract(srcStr);

    // 2. Store raw frontmatter (Phase 7 simplification: one "raw" entry)
    if (!extracted.frontmatter.isEmpty()) {
        d->frontmatterMap.setWithNextStamp("raw", extracted.frontmatter.toUtf8());
    }

    // 3. Store footnote definitions
    for (const Markoff::FootnoteInfo &fn : extracted.footnotes) {
        d->footnoteDefMap.setWithNextStamp(
            fn.label.toUtf8(), fn.content.toUtf8());
    }

    // 4. Full parse of the post-frontmatter body
    auto parsedDoc = Markoff::Document::fromMarkdown(extracted.body);
    if (parsedDoc)
        materializeBlocksFromParsedDoc(*parsedDoc, extracted.body);

    // 5. Reset load baseline (mark "clean" post-load state)
    d->blockEditSequences.clear();
    d->structuralEditSequence = 0;
    d->touchedSinceLoad.clear();
    if (d->inlineCache) d->inlineCache->clear();

    // 6. Notify
    Q_EMIT documentLoaded();
    scheduleD2Changed();
}

QByteArray MarkoffDocument::blockLoadTimeBytes(BlockId id) const
{
    return d->blockLoadTimeBytes.value(id);
}

std::optional<QByteArray> MarkoffDocument::frontmatterValue(const QByteArray &key) const
{
    return d->frontmatterMap.get(key);
}

// ============================================================================
// D2 save (Phase 8)
// ============================================================================

QHash<AttrName, AttrValue> MarkoffDocument::blockAttrs(BlockId id) const
{
    QHash<AttrName, AttrValue> result;
    d->blockAttrsMap.forEachValue([&](const BlockAttrKey &k, const AttrValue &v) {
        if (k.block == id) result.insert(k.name, v);
    });
    return result;
}

bool MarkoffDocument::isBlockTouched(BlockId id) const
{
    // Born after load — no load-time bytes snapshot
    if (!d->blockLoadTimeBytes.contains(id)) return true;
    // Content edited via applyBlockEdit / d2ApplyBufferEdit
    if (d->blockEditSequences.value(id, 0) > 0) return true;
    // Kind or attrs changed via d2SetBlockKind / d2SetBlockAttr
    if (d->touchedSinceLoad.contains(id)) return true;
    return false;
}

namespace {

// Reconstruct the list marker bytes from a ListItem's attrs.
QByteArray markerForListItem(const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs)
{
    using namespace Markoff::AttrNames;
    auto it = attrs.constFind(MarkerStyle);
    if (it == attrs.cend()) return "-";
    const auto *stylePtr = std::get_if<QString>(&it.value());
    if (!stylePtr) return "-";
    const QString &style = *stylePtr;

    if (style == QStringLiteral("dot")) {
        auto ni = attrs.constFind(MarkerNumber);
        const auto *np = (ni != attrs.cend()) ? std::get_if<int>(&ni.value()) : nullptr;
        const int n = np ? *np : 1;
        return QByteArray::number(n) + ".";
    }
    if (style == QStringLiteral("paren")) {
        auto ni = attrs.constFind(MarkerNumber);
        const auto *np = (ni != attrs.cend()) ? std::get_if<int>(&ni.value()) : nullptr;
        const int n = np ? *np : 1;
        return QByteArray::number(n) + ")";
    }
    if (style == QStringLiteral("minus")) return "-";
    if (style == QStringLiteral("plus"))  return "+";
    if (style == QStringLiteral("star"))  return "*";
    if (style == QStringLiteral("task")) {
        auto ci = attrs.constFind(Checked);
        const auto *cp = (ci != attrs.cend()) ? std::get_if<bool>(&ci.value()) : nullptr;
        const bool c = cp ? *cp : false;
        return c ? "- [x]" : "- [ ]";
    }
    return "-";  // fallback
}

QByteArray serializeFrontmatter(const Markoff::FrontmatterMap &fm)
{
    auto raw = fm.get("raw");
    if (!raw.has_value() || raw->isEmpty()) return {};
    return "---\n" + *raw + "\n---\n\n";
}

QByteArray interBlockSeparator()
{
    // Each block's load-time bytes already end with '\n'. Adding one more '\n'
    // produces the blank line that separates top-level blocks in standard Markdown.
    // This matches the tree-sitter parser's block byte ranges: for
    // "Para one\n\nPara two\n", block 0 = "Para one\n" and block 1 = "Para two\n"
    // — the blank line ('\n' at offset 9) is not included in either block's range.
    return "\n";
}

QByteArray serializeFootnoteDefs(const Markoff::FootnoteDefMap &fdm)
{
    QByteArray out;
    fdm.forEachValue([&](const QByteArray &label, const QByteArray &content) {
        out += "[^" + label + "]: " + content + "\n";
    });
    return out;
}

}  // anonymous namespace

QByteArray MarkoffDocument::serializeForSave() const
{
    // Ensure built-in serializers are registered (idempotent)
    BuiltinBlockSerializerRegistry::instance().registerBuiltins();

    QByteArray out;

    // 1. Frontmatter
    out += serializeFrontmatter(d->frontmatterMap);

    // 2. Blocks
    auto blocks = iterateBlocks();
    auto &reg = BuiltinBlockSerializerRegistry::instance();
    for (size_t i = 0; i < blocks.size(); ++i) {
        BlockId id = blocks[i];
        const BlockKind kind = blockKind(id);

        // ListItem blocks need marker+indent reconstruction from attrs;
        // the buffer holds content-only (no marker, no indent, no newline).
        // Handle them unconditionally — bypassing the touched/untouched split.
        if (kind == BlockKind::ListItem) {
            const auto attrs = blockAttrs(id);

            int indent = 0;
            auto indIt = attrs.constFind(AttrNames::IndentLevel);
            if (indIt != attrs.cend()) indent = std::get<int>(indIt.value());

            bool looseRun = false;
            auto looseIt = attrs.constFind(AttrNames::LooseRun);
            if (looseIt != attrs.cend()) looseRun = std::get<bool>(looseIt.value());

            const QByteArray indentBytes(std::max(0, indent) * 3, ' ');
            const QByteArray marker = markerForListItem(attrs);
            const QByteArray content = blockText(id);

            // Emit: <indent><marker> <content>\n
            out += indentBytes + marker + " " + content + "\n";

            // For loose runs, insert a blank line after the item — but only
            // if there is a following block (no trailing blank line after the last item).
            if (looseRun && (i + 1 < blocks.size())) {
                out += "\n";
            }
            continue;
        }

        QByteArray bytes;
        if (!isBlockTouched(id)) {
            // Untouched: use original load-time bytes for byte-identical round-trip
            bytes = d->blockLoadTimeBytes.value(id);
        } else {
            // Touched: re-serialize from CRDT state
            auto fn = reg.get(kind);
            bytes = fn(kind, blockAttrs(id), blockText(id));
        }
        out += bytes;
        if (i + 1 < blocks.size())
            out += interBlockSeparator();
    }

    // 3. Link refs (v1: skip — stored with naive key, proper extraction is Phase 9+)
    // out += serializeLinkRefs(d->linkRefMap);

    // 4. Footnote definitions
    out += serializeFootnoteDefs(d->footnoteDefMap);

    return out;
}

bool MarkoffDocument::save(const QString &path)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(serializeForSave());
    if (!f.commit()) return false;
    if (d->watermark && !d->watermark->onSaveSucceeded())
        qWarning("MarkoffDocument::save: GC deferred — transaction open at save time");
    return true;
}

bool MarkoffDocument::triggerGc()
{
    if (d->watermark) return d->watermark->onSaveSucceeded();
    return false;
}

}  // namespace Markoff
