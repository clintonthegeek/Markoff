// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Session.h>
#include <markoff-foundation/BlockEdit.h>
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/StructuralOp.h>
#include <markoff-foundation/UndoLog.h>
#include <markoff-foundation/BlockSerializer.h>

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

namespace {

// Convert a CollabText::Crdt::Lamport timestamp to the UndoLog OpId encoding.
static Markoff::OpId lamportToOpId(CollabText::Crdt::Lamport ts) noexcept {
    return (static_cast<uint64_t>(ts.replica_id) << 48)
         | (static_cast<uint64_t>(ts.value) & 0x0000FFFFFFFFFFFFull);
}

}  // anonymous namespace

namespace Markoff {

MarkoffDocument::MarkoffDocument(quint16 replicaId, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>(replicaId))
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

    QObject::connect(&d->parsePool, &Markoff::Parse::Detail::ParsePool::parseReady,
                     this, [this](const Markoff::Document *p, quint64 inputEditSeq) {
                         d->latestParse.reset(p);
                         ++d->parseSequence;

                         // Compute BlockAnchors against the CURRENT CRDT buffer (main-thread,
                         // synchronous). Anchors are derived from the parser's tree-sitter
                         // top-level-block enumeration (`Markoff::Document::topLevelBlocks()`),
                         // matching what view-qml's BlockWalker consumes — so anchors[i] and
                         // the view's records[i] always describe the same block. The parse
                         // itself reflects the buffer at parse-schedule time; if intervening
                         // edits moved a block's first-byte char, that block's BlockAnchor
                         // for one parse cycle identifies a slightly different character —
                         // the next parse cycle delivers a corrected anchor. See spec §3.
                         auto bundle = Markoff::Detail::computeBlockAnchors(*this, p);
                         d->latestBlockAnchors = std::move(bundle.anchors);
                         d->latestBlockRanges  = std::move(bundle.ranges);

                         Q_EMIT parseUpdated(p, d->parseSequence, d->latestBlockAnchors,
                                             inputEditSeq);
                     });
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

bool MarkoffDocument::parseIsPending() const
{
    return d->parsePool.isPending();
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

quint64 MarkoffDocument::parseSequence() const noexcept
{
    return d->parseSequence;
}

// applyLocalEdit, undo/redo, applyRemoteOps, resetContent, anchorAt,
// resolveAnchor, sessions, collectGarbage, compact, coalescing setters
// are filled in subsequent tasks (10-23).

CollabText::Crdt::Operation
MarkoffDocument::applyLocalEdit(const QList<MarkoffEdit> &edits)
{
    ++d->editSequence;

    // Snapshot version so we can compute the resulting TextEdits afterwards.
    const CollabText::Crdt::Global oldVersion = d->buffer.version();

    // Translate QList<MarkoffEdit> to the std::vector<pair> + std::vector<string>
    // pair shape that Buffer::apply_local_edit expects.
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    std::vector<std::string> newTexts;
    ranges.reserve(static_cast<size_t>(edits.size()));
    newTexts.reserve(static_cast<size_t>(edits.size()));
    for (const MarkoffEdit &e : edits) {
        ranges.emplace_back(e.oldStart, e.oldEnd);
        newTexts.emplace_back(e.newText.constData(),
                              static_cast<size_t>(e.newText.size()));
    }

    const CollabText::Crdt::Operation op = d->buffer.apply_local_edit(ranges, newTexts);

    // Compute resulting visible-text edits from the buffer's diff API.
    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }

    if (!resultingEdits.isEmpty()) {
        Q_EMIT contentsChanged(resultingEdits);
        d->parsePool.schedule(toMarkdownUtf8(), d->editSequence);
    }

    return op;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::undo()
{
    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    auto op = d->buffer.undo();
    if (!op.has_value())
        return std::nullopt;

    ++d->editSequence;

    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }
    if (!resultingEdits.isEmpty()) {
        Q_EMIT contentsChanged(resultingEdits);
        d->parsePool.schedule(toMarkdownUtf8(), d->editSequence);
    }

    return op;
}

std::optional<CollabText::Crdt::Operation> MarkoffDocument::redo()
{
    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    auto op = d->buffer.redo();
    if (!op.has_value())
        return std::nullopt;

    ++d->editSequence;

    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }
    if (!resultingEdits.isEmpty()) {
        Q_EMIT contentsChanged(resultingEdits);
        d->parsePool.schedule(toMarkdownUtf8(), d->editSequence);
    }

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

    const CollabText::Crdt::Global oldVersion = d->buffer.version();
    d->buffer.apply_ops(ops);

    const auto textEdits = d->buffer.edits_since(oldVersion);
    QList<MarkoffEdit> resultingEdits;
    resultingEdits.reserve(static_cast<int>(textEdits.size()));
    for (const auto &te : textEdits) {
        MarkoffEdit me;
        me.oldStart = te.old_start;
        me.oldEnd = te.old_end;
        me.newText = QByteArray(te.new_text.data(),
                                static_cast<int>(te.new_text.size()));
        resultingEdits << me;
    }

    if (!resultingEdits.isEmpty()) {
        Q_EMIT contentsChanged(resultingEdits);
        d->parsePool.schedule(toMarkdownUtf8(), d->editSequence);
    }
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
    }
    d->parsePool.scheduleReset(toMarkdownUtf8(), d->editSequence);
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
    // Find the block containing this byte offset
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

void MarkoffDocument::setRenderPhaseTaps(Markoff::Render::RenderPhaseTaps *taps) noexcept
{
    d->parsePool.setRenderPhaseTaps(taps);
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

void MarkoffDocument::undoD2() { d->undoLog.undo(); }
void MarkoffDocument::redoD2() { d->undoLog.redo(); }
void MarkoffDocument::undoForBlock(BlockId block) { d->undoLog.undoForBlock(block); }

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
    scheduleD2Changed();
    return newId;
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
    case Kind::ListTight:
    case Kind::ListLoose:             return BlockKind::ListItem;
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
                BlockAttrKey{newId, "info"}, AttrValue{tb.codeLanguage});
        }

        // Buffer content: full source range in UTF-8 bytes
        // For FencedCodeBlock, full source is stored (fences preserved for round-trip).
        // For ListTight/ListLoose, full list source is stored; item-level unwrapping
        // is deferred (the v1 parser doesn't expose item byte ranges).
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
    BlockSerializerRegistry::instance().registerBuiltins();

    QByteArray out;

    // 1. Frontmatter
    out += serializeFrontmatter(d->frontmatterMap);

    // 2. Blocks
    auto blocks = iterateBlocks();
    auto &reg = BlockSerializerRegistry::instance();
    for (size_t i = 0; i < blocks.size(); ++i) {
        BlockId id = blocks[i];
        QByteArray bytes;
        if (!isBlockTouched(id)) {
            // Untouched: use original load-time bytes for byte-identical round-trip
            bytes = d->blockLoadTimeBytes.value(id);
        } else {
            // Touched: re-serialize from CRDT state
            auto fn = reg.get(blockKind(id));
            bytes = fn(blockKind(id), blockAttrs(id), blockText(id));
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
