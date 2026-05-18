// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/BlockEdit.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/StructuralOp.h>
#include <markoff/core/UndoLog.h>
#include <markoff/core/BlockSerializer.h>
#include <markoff/core/AttrNames.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>

#include <markoff/parser/Document.h>

#include <algorithm>
#include <atomic>

#include <QTimer>

#include <crdt/IdList.h>
#include <crdt/IdListOperations.h>
#include <collabtext/Serialization.h>

#include "MarkoffDocumentPrivate.h"
#include "AnchorConversion.h"
#include "BlockAnchorComputation.h"
#include <markoff/core/WatermarkCoordinator.h>
#include <markoff/core/InlineParseCache.h>
#include <markoff/core/SiblingMapOpHeader.h>

namespace {

// Convert a CollabText::Crdt::Lamport timestamp to the UndoLog OpId encoding.
static Markoff::OpId lamportToOpId(CollabText::Crdt::Lamport ts) noexcept {
    return (static_cast<uint64_t>(ts.replica_id) << 48)
         | (static_cast<uint64_t>(ts.value) & 0x0000FFFFFFFFFFFFull);
}

// ── D5 pending-op key helpers ─────────────────────────────────────────────────

// Return the variant index of the target (type disambiguation for pendingOpPayloads key).
static quint8 targetTypeIndex(const Markoff::UndoCrdtTarget &target) noexcept {
    return static_cast<quint8>(target.kind.index());
}

// ── D5 sibling-map payload builders ──────────────────────────────────────────

static QByteArray buildKindTagMapPayload(Markoff::BlockId blockId,
                                         Markoff::BlockKind kind,
                                         Markoff::OpId opId,
                                         bool tombstone)
{
    using namespace Markoff;
    // key: quint64 blockId raw
    QByteArray key;
    {
        QDataStream ds(&key, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setVersion(QDataStream::Qt_6_8);
        ds << quint64(blockId.raw());
    }
    // value: 1-byte kind enum, or empty for tombstone
    QByteArray value;
    if (!tombstone) {
        QDataStream ds(&value, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setVersion(QDataStream::Qt_6_8);
        ds << quint8(kind);
    }
    SiblingMapOpHeader hdr;
    hdr.key              = key;
    hdr.value            = value;
    hdr.lamportReplicaId = static_cast<quint16>(opId >> 48);
    hdr.lamportCounter   = opId & 0x0000FFFFFFFFFFFFull;
    hdr.isTombstone      = tombstone;
    return SiblingMapOpHeader::encode(hdr);
}

static QByteArray buildBlockAttrsMapPayload(const Markoff::BlockAttrKey &attrKey,
                                             const Markoff::AttrValue &attrValue,
                                             Markoff::OpId opId,
                                             bool tombstone)
{
    using namespace Markoff;
    // key: quint64 blockId + quint32 nameLen + name bytes
    QByteArray key;
    {
        QDataStream ds(&key, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setVersion(QDataStream::Qt_6_8);
        ds << quint64(attrKey.block.raw());
        ds << quint32(attrKey.name.size());
        ds.writeRawData(attrKey.name.constData(), attrKey.name.size());
    }
    // value: tag byte + payload, or empty for tombstone
    QByteArray value;
    if (!tombstone) {
        QDataStream ds(&value, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setVersion(QDataStream::Qt_6_8);
        if (std::holds_alternative<int>(attrValue)) {
            ds << quint8(0) << qint32(std::get<int>(attrValue));
        } else if (std::holds_alternative<QString>(attrValue)) {
            const QByteArray utf8 = std::get<QString>(attrValue).toUtf8();
            ds << quint8(1) << quint32(utf8.size());
            ds.writeRawData(utf8.constData(), utf8.size());
        } else if (std::holds_alternative<bool>(attrValue)) {
            ds << quint8(2) << quint8(std::get<bool>(attrValue) ? 1 : 0);
        }
    }
    SiblingMapOpHeader hdr;
    hdr.key              = key;
    hdr.value            = value;
    hdr.lamportReplicaId = static_cast<quint16>(opId >> 48);
    hdr.lamportCounter   = opId & 0x0000FFFFFFFFFFFFull;
    hdr.isTombstone      = tombstone;
    return SiblingMapOpHeader::encode(hdr);
}

static QByteArray buildFrontmatterMapPayload(const QByteArray &mapKey,
                                              const QByteArray &mapValue,
                                              Markoff::OpId opId,
                                              bool tombstone)
{
    using namespace Markoff;
    // key: quint32 len + bytes
    QByteArray key;
    {
        QDataStream ds(&key, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setVersion(QDataStream::Qt_6_8);
        ds << quint32(mapKey.size());
        ds.writeRawData(mapKey.constData(), mapKey.size());
    }
    // value: quint32 len + bytes, or empty for tombstone
    QByteArray value;
    if (!tombstone) {
        QDataStream ds(&value, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setVersion(QDataStream::Qt_6_8);
        ds << quint32(mapValue.size());
        ds.writeRawData(mapValue.constData(), mapValue.size());
    }
    SiblingMapOpHeader hdr;
    hdr.key              = key;
    hdr.value            = value;
    hdr.lamportReplicaId = static_cast<quint16>(opId >> 48);
    hdr.lamportCounter   = opId & 0x0000FFFFFFFFFFFFull;
    hdr.isTombstone      = tombstone;
    return SiblingMapOpHeader::encode(hdr);
}

static QByteArray buildLinkRefMapPayload(const QByteArray &mapKey,
                                          const Markoff::LinkRefValue &refValue,
                                          Markoff::OpId opId,
                                          bool tombstone)
{
    using namespace Markoff;
    // key: quint32 len + bytes
    QByteArray key;
    {
        QDataStream ds(&key, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setVersion(QDataStream::Qt_6_8);
        ds << quint32(mapKey.size());
        ds.writeRawData(mapKey.constData(), mapKey.size());
    }
    // value: two length-prefixed UTF-8 strings (url, title)
    QByteArray value;
    if (!tombstone) {
        QDataStream ds(&value, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setVersion(QDataStream::Qt_6_8);
        const QByteArray urlUtf8   = refValue.url.toUtf8();
        const QByteArray titleUtf8 = refValue.title.toUtf8();
        ds << quint32(urlUtf8.size());
        ds.writeRawData(urlUtf8.constData(), urlUtf8.size());
        ds << quint32(titleUtf8.size());
        ds.writeRawData(titleUtf8.constData(), titleUtf8.size());
    }
    SiblingMapOpHeader hdr;
    hdr.key              = key;
    hdr.value            = value;
    hdr.lamportReplicaId = static_cast<quint16>(opId >> 48);
    hdr.lamportCounter   = opId & 0x0000FFFFFFFFFFFFull;
    hdr.isTombstone      = tombstone;
    return SiblingMapOpHeader::encode(hdr);
}

static QByteArray buildFootnoteDefMapPayload(const QByteArray &mapKey,
                                              const QByteArray &mapValue,
                                              Markoff::OpId opId,
                                              bool tombstone)
{
    // Same encoding as FrontmatterMap
    return buildFrontmatterMapPayload(mapKey, mapValue, opId, tombstone);
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
    qRegisterMetaType<QList<Markoff::BlockId>>("QList<Markoff::BlockId>");

    // ── D2: initialise per-CRDT signal proxies ───────────────────────────
    d->idListProxy          = new IdListProxy(this);
    d->kindTagMapProxy      = new SiblingMapProxy(this);
    d->blockAttrsMapProxy   = new SiblingMapProxy(this);
    d->frontmatterMapProxy  = new SiblingMapProxy(this);
    d->linkRefMapProxy      = new SiblingMapProxy(this);
    d->footnoteDefMapProxy  = new SiblingMapProxy(this);

    // ── D2: wire UndoLog dispatcher ──────────────────────────────────────
    d->undoLog.setDispatcher([this](const UndoCrdtTarget &target, OpId opId, bool forward) {
        std::visit([&](const auto &t) {
            using T = std::decay_t<decltype(t)>;
            if constexpr (std::is_same_v<T, UndoCrdtTarget::BufferT>) {
                auto it = d->blockBuffers.find(t.blockId);
                if (it == d->blockBuffers.end()) return;
                if (forward) it->second->redo();
                else         it->second->undo();
            } else if constexpr (std::is_same_v<T, UndoCrdtTarget::IdListT>) {
                if (forward) d->idList.redo();
                else         d->idList.undo();
            } else if constexpr (std::is_same_v<T, UndoCrdtTarget::KindTagMapT>) {
                if (forward) d->kindTagMap.redo();
                else         d->kindTagMap.undo();
            } else if constexpr (std::is_same_v<T, UndoCrdtTarget::BlockAttrsMapT>) {
                if (forward) d->blockAttrsMap.redo();
                else         d->blockAttrsMap.undo();
            } else if constexpr (std::is_same_v<T, UndoCrdtTarget::FrontmatterMapT>) {
                if (forward) d->frontmatterMap.redo();
                else         d->frontmatterMap.undo();
            } else if constexpr (std::is_same_v<T, UndoCrdtTarget::LinkRefMapT>) {
                if (forward) d->linkRefMap.redo();
                else         d->linkRefMap.undo();
            } else if constexpr (std::is_same_v<T, UndoCrdtTarget::FootnoteDefMapT>) {
                if (forward) d->footnoteDefMap.redo();
                else         d->footnoteDefMap.undo();
            }
            (void)opId;  // opId used for CausalLwwMap-specific undo; IdList/Buffer use stack-based undo
        }, target.kind);
    });

    // ── D5: wire transaction commit → localOpsProduced emission ────────────
    d->undoLog.setOnCommit([this](const std::vector<UndoLog::CommittedOp> &committed,
                                   UndoActionId /*internalActionId*/) {
        if (!d->collabConfigured) {
            // Clean up pendingOpPayloads even in single-user mode to avoid leaks
            for (const auto &cop : committed)
                d->pendingOpPayloads.remove({targetTypeIndex(cop.target), cop.opId});
            return;
        }

        QList<MarkoffOp> ops;
        ops.reserve(int(committed.size()));
        quint64 maxLamport = 0;

        for (const auto &cop : committed) {
            QByteArray payload = d->pendingOpPayloads.take({targetTypeIndex(cop.target), cop.opId});
            if (payload.isEmpty()) {
                // skip ops with no payload
                continue;
            }

            MarkoffOp op;
            op.producerReplicaId = d->replicaId;
            op.payload = std::move(payload);

            if (auto *b = std::get_if<UndoCrdtTarget::BufferT>(&cop.target.kind)) {
                op.target = CrdtTarget::Buffer;
                op.blockId = b->blockId.raw();
            } else if (std::holds_alternative<UndoCrdtTarget::IdListT>(cop.target.kind)) {
                op.target = CrdtTarget::IdList;
                op.blockId = 0;
            } else if (std::holds_alternative<UndoCrdtTarget::KindTagMapT>(cop.target.kind)) {
                op.target = CrdtTarget::KindTagMap;
                op.blockId = 0;
            } else if (std::holds_alternative<UndoCrdtTarget::BlockAttrsMapT>(cop.target.kind)) {
                op.target = CrdtTarget::BlockAttrsMap;
                op.blockId = 0;
            } else if (std::holds_alternative<UndoCrdtTarget::FrontmatterMapT>(cop.target.kind)) {
                op.target = CrdtTarget::FrontmatterMap;
                op.blockId = 0;
            } else if (std::holds_alternative<UndoCrdtTarget::LinkRefMapT>(cop.target.kind)) {
                op.target = CrdtTarget::LinkRefMap;
                op.blockId = 0;
            } else if (std::holds_alternative<UndoCrdtTarget::FootnoteDefMapT>(cop.target.kind)) {
                op.target = CrdtTarget::FootnoteDefMap;
                op.blockId = 0;
            } else {
                continue;  // unknown target; skip
            }

            // Extract Lamport counter from OpId encoding: (replicaId << 48) | counter
            const quint64 counter = cop.opId & 0x0000FFFFFFFFFFFFull;
            maxLamport = std::max(maxLamport, counter);

            ops.append(std::move(op));
        }

        if (ops.isEmpty()) return;

        auto meta = d->buildBundleMeta(0, maxLamport);
        meta.opCountInBundle = quint16(ops.size());
        // Track the highest lamport ever produced, for the watermark gate.
        d->maxProducedLamport = std::max(d->maxProducedLamport, maxLamport);
        Q_EMIT localOpsProduced(std::move(ops), std::move(meta));
    });

    // ── D2: initialise WatermarkCoordinator (Phase 9) ───────────────────────
    d->watermark = std::make_unique<WatermarkCoordinator>(*this);

    // ── D2: initialise InlineParseCache (Phase 10) ──────────────────────────
    d->inlineCache = std::make_unique<InlineParseCache>(*this);

    d->collabConfigured = true;
}

MarkoffDocument::MarkoffDocument(QObject *parent)
    : MarkoffDocument(quint16(0x0001), nullptr, parent)
{
    d->collabConfigured = false;
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

quint16 MarkoffDocument::replicaId() const noexcept
{
    return d->replicaId;
}

bool MarkoffDocument::isCollabConfigured() const noexcept
{
    return d->collabConfigured;
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

void MarkoffDocument::applyRemoteOps(QList<MarkoffOp> ops, MarkoffBundleMeta meta)
{
    for (const MarkoffOp &op : ops) {
        if (op.producerReplicaId != meta.producerReplicaId) {
            qWarning() << "MarkoffDocument::applyRemoteOps: producer mismatch, skipping";
            continue;
        }
        switch (op.target) {
        case CrdtTarget::Buffer:
            applyRemoteBufferOp(BlockId::fromRaw(op.blockId), op.payload);
            break;
        case CrdtTarget::IdList:
            applyRemoteIdListOp(op.payload);
            break;
        case CrdtTarget::KindTagMap:
            applyRemoteKindTagMapOp(op.payload);
            break;
        case CrdtTarget::BlockAttrsMap:
            applyRemoteBlockAttrsMapOp(op.payload);
            break;
        case CrdtTarget::FrontmatterMap:
            applyRemoteFrontmatterMapOp(op.payload);
            break;
        case CrdtTarget::LinkRefMap:
            applyRemoteLinkRefMapOp(op.payload);
            break;
        case CrdtTarget::FootnoteDefMap:
            applyRemoteFootnoteDefMapOp(op.payload);
            break;
        }
    }
    scheduleD2Changed();
}

void MarkoffDocument::applyRemoteBufferOp(BlockId blockId, const QByteArray &payload)
{
    auto it = d->blockBuffers.find(blockId);
    if (it == d->blockBuffers.end()) {
        // Unknown block — queue until IdList op arrives and creates the buffer.
        d->pendingBufferOps[blockId].append(payload);
        return;
    }
    const std::string json(payload.constData(), size_t(payload.size()));
    const auto opt = CollabText::Crdt::decode_operation(json);
    if (!opt) {
        qWarning() << "applyRemoteBufferOp: decode_operation failed; skipping";
        return;
    }
    it->second->apply_ops({*opt});
    auto proxyIt = d->bufferProxies.find(blockId);
    if (proxyIt != d->bufferProxies.end() && proxyIt.value())
        proxyIt.value()->notifyChanged();
    Q_EMIT blocksChanged({blockId});
}

void MarkoffDocument::applyRemoteIdListOp(const QByteArray &payload)
{
    const std::string json(payload.constData(), size_t(payload.size()));
    const auto opt = CollabText::Crdt::decode_idlist_operation(json);
    if (!opt) {
        qWarning() << "applyRemoteIdListOp: decode_idlist_operation failed; skipping";
        return;
    }
    d->idList.apply_remote_op(*opt);
    d->idListProxy->notifyChanged();

    // Create Buffer + proxy for any block IDs that are now in the IdList but
    // not yet in blockBuffers. This happens when a remote insert arrives.
    for (uint64_t raw : d->idList.ids()) {
        const BlockId blockId = BlockId::fromRaw(raw);
        if (d->blockBuffers.find(blockId) == d->blockBuffers.end()) {
            d->blockBuffers.emplace(blockId,
                std::make_unique<CollabText::Crdt::Buffer>(d->replicaId));
            d->bufferProxies.insert(blockId, new BufferProxy(blockId, this));
        }
    }

    // Drain any pending buffer ops for blocks that are now known.
    auto pending = d->pendingBufferOps;
    d->pendingBufferOps.clear();
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        if (d->blockBuffers.find(it.key()) != d->blockBuffers.end()) {
            for (const QByteArray &p : it.value())
                applyRemoteBufferOp(it.key(), p);
        } else {
            d->pendingBufferOps[it.key()] = it.value();  // still unknown
        }
    }
}

void MarkoffDocument::applyRemoteKindTagMapOp(const QByteArray &payload)
{
    SiblingMapOpHeader hdr;
    if (!SiblingMapOpHeader::decode(payload, &hdr)) {
        qWarning() << "applyRemoteKindTagMapOp: decode failed; skipping";
        return;
    }
    if (hdr.key.size() < 8) { qWarning() << "applyRemoteKindTagMapOp: bad key size"; return; }
    quint64 raw = 0;
    QDataStream ks(hdr.key);
    ks.setByteOrder(QDataStream::LittleEndian);
    ks >> raw;
    BlockId blockId = BlockId::fromRaw(raw);

    BlockKind kind = BlockKind::Paragraph;
    if (!hdr.isTombstone) {
        if (hdr.value.isEmpty()) { qWarning() << "applyRemoteKindTagMapOp: empty value for set"; return; }
        kind = static_cast<BlockKind>(quint8(hdr.value[0]));
    }

    CausalStamp stamp{hdr.lamportReplicaId, hdr.lamportCounter};
    KindTagMap::RemoteOp op{blockId, kind, stamp, hdr.isTombstone};
    d->kindTagMap.applyRemote(op);
    d->kindTagMapProxy->notifyChanged();
}

void MarkoffDocument::applyRemoteBlockAttrsMapOp(const QByteArray &payload)
{
    SiblingMapOpHeader hdr;
    if (!SiblingMapOpHeader::decode(payload, &hdr)) {
        qWarning() << "applyRemoteBlockAttrsMapOp: decode failed; skipping";
        return;
    }
    // Decode key: quint64 blockId + quint32 nameLen + name bytes
    QDataStream ks(hdr.key);
    ks.setByteOrder(QDataStream::LittleEndian);
    quint64 rawId = 0; quint32 nameLen = 0;
    ks >> rawId >> nameLen;
    if (ks.status() != QDataStream::Ok || hdr.key.size() < int(12 + nameLen)) {
        qWarning() << "applyRemoteBlockAttrsMapOp: bad key encoding"; return;
    }
    QByteArray name(int(nameLen), Qt::Uninitialized);
    ks.readRawData(name.data(), int(nameLen));
    BlockAttrKey key{BlockId::fromRaw(rawId), name};

    AttrValue value;
    if (!hdr.isTombstone) {
        QDataStream vs(hdr.value);
        vs.setByteOrder(QDataStream::LittleEndian);
        quint8 tag = 0; vs >> tag;
        if (tag == 0) {
            qint32 v = 0; vs >> v; value = static_cast<int>(v);
        } else if (tag == 1) {
            quint32 len = 0; vs >> len;
            QByteArray utf8(int(len), Qt::Uninitialized);
            vs.readRawData(utf8.data(), int(len));
            value = QString::fromUtf8(utf8);
        } else if (tag == 2) {
            quint8 v = 0; vs >> v; value = (v != 0);
        } else {
            qWarning() << "applyRemoteBlockAttrsMapOp: unknown value tag" << tag; return;
        }
    }

    CausalStamp stamp{hdr.lamportReplicaId, hdr.lamportCounter};
    BlockAttrsMap::RemoteOp op{key, value, stamp, hdr.isTombstone};
    d->blockAttrsMap.applyRemote(op);
    d->blockAttrsMapProxy->notifyChanged();
}

void MarkoffDocument::applyRemoteFrontmatterMapOp(const QByteArray &payload)
{
    SiblingMapOpHeader hdr;
    if (!SiblingMapOpHeader::decode(payload, &hdr)) {
        qWarning() << "applyRemoteFrontmatterMapOp: decode failed; skipping";
        return;
    }
    // Decode key: quint32 len + bytes
    QDataStream ks(hdr.key);
    ks.setByteOrder(QDataStream::LittleEndian);
    quint32 keyLen = 0; ks >> keyLen;
    if (ks.status() != QDataStream::Ok || hdr.key.size() < int(4 + keyLen)) {
        qWarning() << "applyRemoteFrontmatterMapOp: bad key encoding"; return;
    }
    QByteArray mapKey(int(keyLen), Qt::Uninitialized);
    ks.readRawData(mapKey.data(), int(keyLen));

    QByteArray mapValue;
    if (!hdr.isTombstone) {
        QDataStream vs(hdr.value);
        vs.setByteOrder(QDataStream::LittleEndian);
        quint32 valLen = 0; vs >> valLen;
        if (vs.status() != QDataStream::Ok || hdr.value.size() < int(4 + valLen)) {
            qWarning() << "applyRemoteFrontmatterMapOp: bad value encoding"; return;
        }
        mapValue.resize(int(valLen));
        vs.readRawData(mapValue.data(), int(valLen));
    }

    CausalStamp stamp{hdr.lamportReplicaId, hdr.lamportCounter};
    FrontmatterMap::RemoteOp op{mapKey, mapValue, stamp, hdr.isTombstone};
    d->frontmatterMap.applyRemote(op);
    d->frontmatterMapProxy->notifyChanged();
}

void MarkoffDocument::applyRemoteLinkRefMapOp(const QByteArray &payload)
{
    SiblingMapOpHeader hdr;
    if (!SiblingMapOpHeader::decode(payload, &hdr)) {
        qWarning() << "applyRemoteLinkRefMapOp: decode failed; skipping";
        return;
    }
    // Decode key: quint32 len + bytes
    QDataStream ks(hdr.key);
    ks.setByteOrder(QDataStream::LittleEndian);
    quint32 keyLen = 0; ks >> keyLen;
    if (ks.status() != QDataStream::Ok || hdr.key.size() < int(4 + keyLen)) {
        qWarning() << "applyRemoteLinkRefMapOp: bad key encoding"; return;
    }
    QByteArray mapKey(int(keyLen), Qt::Uninitialized);
    ks.readRawData(mapKey.data(), int(keyLen));

    LinkRefValue refValue;
    if (!hdr.isTombstone) {
        QDataStream vs(hdr.value);
        vs.setByteOrder(QDataStream::LittleEndian);
        quint32 urlLen = 0; vs >> urlLen;
        if (vs.status() != QDataStream::Ok) { qWarning() << "applyRemoteLinkRefMapOp: bad url len"; return; }
        QByteArray urlUtf8(int(urlLen), Qt::Uninitialized);
        vs.readRawData(urlUtf8.data(), int(urlLen));
        quint32 titleLen = 0; vs >> titleLen;
        if (vs.status() != QDataStream::Ok) { qWarning() << "applyRemoteLinkRefMapOp: bad title len"; return; }
        QByteArray titleUtf8(int(titleLen), Qt::Uninitialized);
        vs.readRawData(titleUtf8.data(), int(titleLen));
        refValue.url   = QString::fromUtf8(urlUtf8);
        refValue.title = QString::fromUtf8(titleUtf8);
    }

    CausalStamp stamp{hdr.lamportReplicaId, hdr.lamportCounter};
    LinkRefMap::RemoteOp op{mapKey, refValue, stamp, hdr.isTombstone};
    d->linkRefMap.applyRemote(op);
    d->linkRefMapProxy->notifyChanged();
}

void MarkoffDocument::applyRemoteFootnoteDefMapOp(const QByteArray &payload)
{
    SiblingMapOpHeader hdr;
    if (!SiblingMapOpHeader::decode(payload, &hdr)) {
        qWarning() << "applyRemoteFootnoteDefMapOp: decode failed; skipping";
        return;
    }
    // Decode key: quint32 len + bytes (same as FrontmatterMap)
    QDataStream ks(hdr.key);
    ks.setByteOrder(QDataStream::LittleEndian);
    quint32 keyLen = 0; ks >> keyLen;
    if (ks.status() != QDataStream::Ok || hdr.key.size() < int(4 + keyLen)) {
        qWarning() << "applyRemoteFootnoteDefMapOp: bad key encoding"; return;
    }
    QByteArray mapKey(int(keyLen), Qt::Uninitialized);
    ks.readRawData(mapKey.data(), int(keyLen));

    QByteArray mapValue;
    if (!hdr.isTombstone) {
        QDataStream vs(hdr.value);
        vs.setByteOrder(QDataStream::LittleEndian);
        quint32 valLen = 0; vs >> valLen;
        if (vs.status() != QDataStream::Ok || hdr.value.size() < int(4 + valLen)) {
            qWarning() << "applyRemoteFootnoteDefMapOp: bad value encoding"; return;
        }
        mapValue.resize(int(valLen));
        vs.readRawData(mapValue.data(), int(valLen));
    }

    CausalStamp stamp{hdr.lamportReplicaId, hdr.lamportCounter};
    FootnoteDefMap::RemoteOp op{mapKey, mapValue, stamp, hdr.isTombstone};
    d->footnoteDefMap.applyRemote(op);
    d->footnoteDefMapProxy->notifyChanged();
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
    Q_EMIT documentChanged();
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
    // D2 path: resolve anchor directly against the per-block CRDT buffer.
    auto it = d->blockBuffers.find(b);
    if (it != d->blockBuffers.end()) {
        const CollabText::Crdt::Anchor a = Detail::toCrdtAnchor(t);
        const uint32_t localOff = it->second->resolve_anchor(a);
        return static_cast<int>(localOff);
    }

    // Legacy fallback.
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
    // D2 path: use the per-block buffer directly, bypassing latestBlockRanges.
    auto it = d->blockBuffers.find(b);
    if (it != d->blockBuffers.end()) {
        const auto bias = rightBias ? CollabText::Crdt::Bias::Right
                                    : CollabText::Crdt::Bias::Left;
        const int sz = static_cast<int>(it->second->visible_length());
        const int clamped = std::max(0, std::min(offset, sz));
        const CollabText::Crdt::Anchor a =
            it->second->anchor_at(static_cast<uint32_t>(clamped), bias);
        return Detail::toTextAnchor(b, a);
    }

    // Legacy fallback: use parse-based block byte ranges.
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
    // Emit dirtyChanged synchronously on every edit — callers need the
    // transition signal immediately, not after the debounce delay.
    maybeEmitDirtyChanged();

    if (!d->d2ChangePending) {
        d->d2ChangePending = true;
        QTimer::singleShot(0, this, [this]() {
            // Guard against double-fire: flushPendingD2Changed() may have
            // emitted synchronously before this lambda runs.
            if (!d->d2ChangePending) return;
            d->d2ChangePending = false;
            Q_EMIT d2DocumentChanged();
            Q_EMIT documentChanged();
        });
    }
}

void MarkoffDocument::flushPendingD2Changed()
{
    if (!d->d2ChangePending) return;
    d->d2ChangePending = false;
    Q_EMIT d2DocumentChanged();
    Q_EMIT documentChanged();
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
    t.registerOp(UndoCrdtTarget::buffer(edit.blockId), lamportToOpId(ts));
    d->pendingOpPayloads[{6, lamportToOpId(ts)}] =
        QByteArray::fromStdString(CollabText::Crdt::encode_operation(op));

    ++d->blockEditSequences[edit.blockId];

    // Notify per-block buffer proxy synchronously (before debounced d2DocumentChanged).
    auto proxyIt = d->bufferProxies.find(edit.blockId);
    if (proxyIt != d->bufferProxies.end() && proxyIt.value())
        proxyIt.value()->notifyChanged();

    Q_EMIT blocksChanged({edit.blockId});
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
            t.registerOp(UndoCrdtTarget::idList(), lamportToOpId(idTs));
            d->pendingOpPayloads[{0, lamportToOpId(idTs)}] =
                QByteArray::fromStdString(CollabText::Crdt::encode_idlist_operation(idOp));
            // Create buffer
            d->blockBuffers.emplace(newBlock, std::make_unique<CollabText::Crdt::Buffer>(d->replicaId));
            // Create buffer proxy for this block (parented to this; Qt owns it)
            d->bufferProxies.insert(newBlock, new BufferProxy(newBlock, this));
            // Set kind
            OpId kindOpId = d->kindTagMap.setWithNextStamp(newBlock, payload.kind);
            t.registerOp(UndoCrdtTarget::kindTagMap(), kindOpId);
            d->pendingOpPayloads[{1, kindOpId}] = buildKindTagMapPayload(newBlock, payload.kind, kindOpId, false);
            // Notify structural + kind proxies
            d->idListProxy->notifyChanged();
            d->kindTagMapProxy->notifyChanged();
        } else if constexpr (std::is_same_v<T, StructuralOp::RemoveEntry>) {
            CollabText::Crdt::Anchor anchor = d->idList.anchor_of(payload.blockId.raw(), CollabText::Crdt::Bias::Left);
            CollabText::Crdt::IdListOperation idOp = d->idList.remove_at(anchor);
            auto idTs = CollabText::Crdt::get_idlist_op_timestamp(idOp);
            t.registerOp(UndoCrdtTarget::idList(), lamportToOpId(idTs));
            d->pendingOpPayloads[{0, lamportToOpId(idTs)}] =
                QByteArray::fromStdString(CollabText::Crdt::encode_idlist_operation(idOp));
            OpId kindOpId = d->kindTagMap.removeWithNextStamp(payload.blockId);
            t.registerOp(UndoCrdtTarget::kindTagMap(), kindOpId);
            d->pendingOpPayloads[{1, kindOpId}] = buildKindTagMapPayload(payload.blockId, BlockKind::Paragraph, kindOpId, true);
            // Buffer retained for GC (Phase 9)
            d->idListProxy->notifyChanged();
            d->kindTagMapProxy->notifyChanged();
        } else if constexpr (std::is_same_v<T, StructuralOp::ChangeKind>) {
            OpId kindOpId = d->kindTagMap.setWithNextStamp(payload.blockId, payload.newKind);
            t.registerOp(UndoCrdtTarget::kindTagMap(), kindOpId);
            d->pendingOpPayloads[{1, kindOpId}] = buildKindTagMapPayload(payload.blockId, payload.newKind, kindOpId, false);
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

// ============================================================================
// Save-watermark API (E2.5 Task A1)
// ============================================================================

quint64 MarkoffDocument::savedSequence() const noexcept { return d->savedSeq; }

bool MarkoffDocument::dirty() const noexcept
{
    return d->savedSeq != d2EditSequence();
}

void MarkoffDocument::markSaved(quint64 seq)
{
    d->savedSeq = seq;
    maybeEmitDirtyChanged();
}

void MarkoffDocument::maybeEmitDirtyChanged()
{
    const bool nowDirty = dirty();
    if (nowDirty != d->lastDirtyEmitted) {
        d->lastDirtyEmitted = nowDirty;
        Q_EMIT dirtyChanged(nowDirty);
    }
}

// ============================================================================
// Recent-cuts cache (E2.5 Task A2)
// ============================================================================

void MarkoffDocument::recordRecentCut(quint64 cutSeq, std::vector<BlockId> ids)
{
    d->recentCuts.push_back({cutSeq, std::move(ids)});
    while (d->recentCuts.size() > Private::kRecentCutsMax)
        d->recentCuts.pop_front();
}

std::vector<BlockId> MarkoffDocument::takeRecentCut(quint64 cutSeq)
{
    for (auto it = d->recentCuts.begin(); it != d->recentCuts.end(); ++it) {
        if (it->cutSeq == cutSeq) {
            auto ids = std::move(it->blockIds);
            d->recentCuts.erase(it);
            return ids;
        }
    }
    return {};
}

QByteArray MarkoffDocument::reconstructFlatMarkdown(const QJsonArray &blocks)
{
    // Convention for the per-block "text" field, as produced by
    // LiveClipboardController::serializeSelection (which copies model.text =
    // doc->blockText(id) with the trailing '\n' trimmed):
    //
    //   * Heading buffers, blockquote buffers, code-block buffers, paragraph
    //     buffers all hold the raw source range from the parser — `## ` for
    //     headings, `> ` for blockquotes, ` ``` ` fences for code-blocks. We
    //     emit those verbatim and let the receiving side's kind-transition
    //     re-detect the kind from the prefix (Paragraph → Heading via
    //     `## `, Paragraph → Blockquote via `> `, etc.). Adding another
    //     prefix here was a bug: a copied "## TL;DR" came back as
    //     "## ## TL;DR" because we doubled the hashes.
    //
    //   * ListItem is the one exception: tree-sitter's per-list_item parser
    //     range starts after the marker, and `materializeBlocksFromParsedDoc`
    //     stores buffer = content only (no marker, no leading indent). The
    //     marker therefore must be reconstructed from `attrs.markerStyle` /
    //     `markerNumber` / `checked` to round-trip a list selection.
    //
    // Kind strings are lowercase to match Markoff::Live::BlockKind constants
    // (libs/markoff-live/src/BlockKind.cpp). Capitalised strings like
    // "Heading"/"CodeBlock" don't appear in real clipboard payloads.
    QByteArray flat;
    bool first = true;
    for (const auto &v : blocks) {
        const QJsonObject obj = v.toObject();
        if (!first)
            flat.append("\n\n");
        first = false;

        const QString kind = obj.value(QStringLiteral("kind")).toString();
        const QString text = obj.value(QStringLiteral("text")).toString();
        const QJsonObject attrs = obj.value(QStringLiteral("attrs")).toObject();

        if (kind == QStringLiteral("list-item")) {
            const QString style =
                attrs.value(QStringLiteral("markerStyle")).toString(QStringLiteral("minus"));
            const int markerNumber =
                attrs.value(QStringLiteral("markerNumber")).toInt(1);
            const bool checked =
                attrs.value(QStringLiteral("checked")).toBool();
            QByteArray prefix;
            if (style == QStringLiteral("dot"))
                prefix = QByteArray::number(markerNumber) + ".";
            else if (style == QStringLiteral("paren"))
                prefix = QByteArray::number(markerNumber) + ")";
            else if (style == QStringLiteral("plus"))
                prefix = "+";
            else if (style == QStringLiteral("star"))
                prefix = "*";
            else if (style == QStringLiteral("task"))
                prefix = checked ? "- [x]" : "- [ ]";
            else  // "minus" or unknown
                prefix = "-";
            flat.append(prefix);
            flat.append(' ');
            flat.append(text.toUtf8());
        } else {
            // paragraph / heading / code-block / blockquote / math / image /
            // hr / unknown: text already contains the source-faithful prefix
            // (or nothing, for paragraph). Emit verbatim.
            flat.append(text.toUtf8());
        }
    }
    return flat;
}

void MarkoffDocument::applyStructuredPaste(quint32 startByte, quint32 endByte,
                                           const QJsonArray &blocks,
                                           const Markoff::PasteMeta &meta)
{
    // 1. Serialise the JSON block array to flat markdown text.
    const QByteArray flat = reconstructFlatMarkdown(blocks);

    // 2. Apply as a flat edit (clamp: lo ≤ hi).
    const quint32 lo = std::min(startByte, endByte);
    const quint32 hi = std::max(startByte, endByte);
    applyFlatEdit(lo, hi, flat, Markoff::Origin::UserEdit);

    // 3. BlockId reuse path: consume the cache entry if requested.
    //    Full remap of newly-minted IDs to the cached IDs is a TODO (Phase C4).
    //    The correctness guarantee is unaffected; only the CRDT identity of the
    //    re-pasted blocks differs from the original cut blocks.
    if (meta.reuseBlockIds) {
        (void)takeRecentCut(meta.cutSeq);  // consume the entry even if no remap yet
        // TODO(C4): walk iterateBlocks() post-edit and rename the inserted
        // blocks to the cached IDs so cut→paste-back preserves CRDT identity.
    }
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
    t.registerOp(UndoCrdtTarget::buffer(block), lamportToOpId(ts));
    d->pendingOpPayloads[{6, lamportToOpId(ts)}] =
        QByteArray::fromStdString(CollabText::Crdt::encode_operation(op));
    ++d->blockEditSequences[block];

    // Notify per-block buffer proxy synchronously (same as applyBlockEdit).
    auto proxyIt = d->bufferProxies.find(block);
    if (proxyIt != d->bufferProxies.end() && proxyIt.value())
        proxyIt.value()->notifyChanged();

    Q_EMIT blocksChanged({block});
    scheduleD2Changed();

    // Cursor survival: re-resolve remote TextCaret anchors in this block.
    // Use the per-block D2 buffer directly to get the local byte offset —
    // offsetInBlock() relies on latestBlockRanges (stale after D2 edits).
    {
        auto bufIt = d->blockBuffers.find(block);
        if (bufIt != d->blockBuffers.end()) {
            for (auto it = d->remoteCursors.begin(); it != d->remoteCursors.end(); ++it) {
                const quint16 replicaId = it.key();
                auto &rec = it.value();
                if (auto *tc = std::get_if<Markoff::TextCaret>(&rec.cursor)) {
                    if (tc->block == block) {
                        // Resolve anchor against the per-block CRDT buffer.
                        const CollabText::Crdt::Anchor a =
                            Markoff::Detail::toCrdtAnchor(tc->positionAnchor);
                        const uint32_t localOff = bufIt->second->resolve_anchor(a);
                        tc->cachedQtPos = static_cast<quint32>(localOff);
                        Q_EMIT remoteCursorChanged(replicaId, rec.cursor, rec.color, rec.label);
                    }
                }
            }
        }
    }
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
    t.registerOp(UndoCrdtTarget::idList(), lamportToOpId(idTs));
    d->pendingOpPayloads[{0, lamportToOpId(idTs)}] =
        QByteArray::fromStdString(CollabText::Crdt::encode_idlist_operation(idOp));

    d->blockBuffers.emplace(newId, std::make_unique<CollabText::Crdt::Buffer>(d->replicaId));
    d->bufferProxies.insert(newId, new BufferProxy(newId, this));

    OpId kindOpId = d->kindTagMap.setWithNextStamp(newId, kind);
    t.registerOp(UndoCrdtTarget::kindTagMap(), kindOpId);
    d->pendingOpPayloads[{1, kindOpId}] = buildKindTagMapPayload(newId, kind, kindOpId, false);

    ++d->structuralEditSequence;
    d->idListProxy->notifyChanged();
    d->kindTagMapProxy->notifyChanged();

    // Emit blockInserted with the row the new block landed at.
    {
        const auto current = iterateBlocks();
        int row = 0;
        for (int i = 0; i < static_cast<int>(current.size()); ++i) {
            if (current[i] == newId) { row = i; break; }
        }
        Q_EMIT blockInserted(newId, row);
    }

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

    // Boundary disambiguation for startIdx differs by edit shape:
    //
    //   * Cursor paste (oldStart == oldEnd): cursor sits at a single byte
    //     position. A boundary between block N-1 and block N means the
    //     cursor is at the START of block N — paste-start must land in
    //     block N (otherwise the new content would be appended to N-1).
    //
    //   * Range edit (oldStart != oldEnd, e.g. selection delete): a range
    //     that starts at the same boundary is conventionally read as
    //     "starts at end of block N-1" so that a range like
    //     [start-of-block-1, end-of-block-1] hits the cross-block-edit
    //     branch and removes block 1 entirely. The legacy `<=` keeps that.
    //
    // endIdx always biases to the current block at boundaries — deletes
    // shouldn't extend into the next block.
    const bool isCursorEdit = (oldStart == oldEnd);

    for (size_t i = 0; i < blocks.size(); ++i) {
        const uint32_t sz     = static_cast<uint32_t>(blockText(blocks[i]).size());
        const uint32_t blkEnd = cursor + sz;

        if (startIdx == -1) {
            if (isCursorEdit) {
                // Cursor at start-of-block: bias to this block (paste lands
                // inside it, not appended to the previous block).
                if (cursor == oldStart) {
                    startIdx    = static_cast<int>(i);
                    startWithin = 0;
                } else if (oldStart < blkEnd) {
                    startIdx    = static_cast<int>(i);
                    startWithin = oldStart - cursor;
                }
            } else if (oldStart <= blkEnd) {
                // Range edit: legacy behavior (boundary biases to current
                // block so cross-block deletes work).
                startIdx    = static_cast<int>(i);
                startWithin = oldStart - cursor;
            }
        }
        // Cursor edits: endIdx == startIdx; computed after the loop.
        // Range edits: find the block whose end-or-inside contains oldEnd.
        if (!isCursorEdit && oldEnd <= blkEnd) {
            endIdx    = static_cast<int>(i);
            endWithin = oldEnd - cursor;
            break;
        }
        cursor = blkEnd;
    }

    // Cursor edits collapse endIdx onto startIdx.
    if (isCursorEdit && startIdx >= 0) {
        endIdx    = startIdx;
        endWithin = startWithin;
    }
    // End-of-document append for a cursor edit past the last block.
    if (isCursorEdit && startIdx == -1 && !blocks.empty()) {
        startIdx    = static_cast<int>(blocks.size() - 1);
        startWithin = static_cast<uint32_t>(blockText(blocks.back()).size());
        endIdx      = startIdx;
        endWithin   = startWithin;
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
        // B1: block buffers are content. parts[0] is the new content for the
        // portion of the current block before the split; the serializer
        // reconstructs separators.
        // FALSIFIABILITY PROOF: B1 applyFlatEdit "+ \n" restored
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
            }
            // (No delimiter append: B1 buffers are content-only.)
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
        // B1: block buffers are content; no synthetic delimiter.
        QByteArray firstReplacement = parts.front();
        d2ApplyBufferEdit(blocks[startIdx], startWithin, 0, firstReplacement, t);

        BlockId after = blocks[startIdx];
        for (int i = 1; i < parts.size(); ++i) {
            BlockId newBlk = d2InsertBlock(after, BlockKind::Paragraph, t);
            const bool isLast = (i == parts.size() - 1);
            QByteArray seed = parts[i];
            if (isLast) {
                seed += endTail;
            }
            // (No delimiter append: B1 buffers are content-only.)
            if (!seed.isEmpty()) {
                d2ApplyBufferEdit(newBlk, 0, 0, seed, t);
            }
            after = newBlk;
        }
    }
}

void MarkoffDocument::d2RemoveBlock(BlockId block, UndoLog::Transaction &t)
{
    // Capture the former row before the IdList mutation.
    int formerRow = 0;
    {
        const auto before = iterateBlocks();
        for (int i = 0; i < static_cast<int>(before.size()); ++i) {
            if (before[i] == block) { formerRow = i; break; }
        }
    }

    CollabText::Crdt::Anchor anchor = d->idList.anchor_of(block.raw(), CollabText::Crdt::Bias::Left);
    auto idOp = d->idList.remove_at(anchor);
    auto idTs = CollabText::Crdt::get_idlist_op_timestamp(idOp);
    t.registerOp(UndoCrdtTarget::idList(), lamportToOpId(idTs));
    d->pendingOpPayloads[{0, lamportToOpId(idTs)}] =
        QByteArray::fromStdString(CollabText::Crdt::encode_idlist_operation(idOp));

    OpId kindOpId = d->kindTagMap.removeWithNextStamp(block);
    t.registerOp(UndoCrdtTarget::kindTagMap(), kindOpId);
    d->pendingOpPayloads[{1, kindOpId}] = buildKindTagMapPayload(block, BlockKind::Paragraph, kindOpId, true);

    ++d->structuralEditSequence;
    d->idListProxy->notifyChanged();
    d->kindTagMapProxy->notifyChanged();
    Q_EMIT blockRemoved(block, formerRow);
    scheduleD2Changed();
}

void MarkoffDocument::d2SetBlockKind(BlockId block, BlockKind newKind,
                                      UndoLog::Transaction &t)
{
    OpId opId = d->kindTagMap.setWithNextStamp(block, newKind);
    t.registerOp(UndoCrdtTarget::kindTagMap(), opId);
    d->pendingOpPayloads[{1, opId}] = buildKindTagMapPayload(block, newKind, opId, false);
    d->touchedSinceLoad.insert(block);
    scheduleD2Changed();
}

void MarkoffDocument::d2SetBlockAttr(BlockId block, const QByteArray &attrName,
                                      const AttrValue &value,
                                      UndoLog::Transaction &t)
{
    BlockAttrKey key{block, attrName};
    OpId opId = d->blockAttrsMap.setWithNextStamp(key, value);
    t.registerOp(UndoCrdtTarget::blockAttrsMap(), opId);
    d->pendingOpPayloads[{2, opId}] = buildBlockAttrsMapPayload(key, value, opId, false);
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
        if (kind == BlockKind::Heading
            && tb.kind == TLB::Kind::SetextHeading) {
            d->blockAttrsMap.setWithNextStamp(
                BlockAttrKey{newId, AttrNames::HeadingForm},
                AttrValue{QString("setext")});
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

        // Buffer content: full source range in UTF-8 bytes, then strip the
        // trailing block-terminator '\n' if present. Per B1 (spec
        // 2026-05-18-b1-buffer-convention-design.md §1), block buffers hold
        // content only — the structural '\n' separator belongs to the
        // serializer.
        //
        // For FencedCodeBlock, full source is stored (fences preserved for
        // round-trip). For ListItem, the parser's harvestListItem already
        // strips trailing whitespace from the byte range, so this chop is
        // idempotent for ListItem.
        QByteArray content = bodyUtf8.mid(tb.byteStart, tb.byteEnd - tb.byteStart);
        if (content.endsWith('\n'))
            content.chop(1);

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
    // documentChanged() fires synchronously here so connected views can update
    // their state in the same call stack as loadFromMarkdown(). d2DocumentChanged()
    // from scheduleD2Changed() is deferred one event-loop iteration (QTimer::singleShot(0));
    // consumers of both signals must not assume they arrive in the same call stack on load.
    Q_EMIT documentLoaded();
    Q_EMIT documentChanged();
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
    // Per B1 (spec 2026-05-18-b1-buffer-convention-design.md §1):
    // block buffers are content; the separator carries the full gap
    // between two block bodies — a line break ending the previous
    // block plus the blank line opening the next.
    return "\n\n";
}

QByteArray finalDocumentTerminator()
{
    // CommonMark-conventional: documents end with a single newline.
    // Emitted by serializeForSave after the block loop.
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

            // Emit: <indent><marker> <content>  (separator is added below)
            out += indentBytes + marker + " " + content;

            // Per B1 §3: the serializer owns inter-block separators. Loose runs
            // emit a blank line between consecutive items; tight runs emit just
            // the line break.
            if (i + 1 < blocks.size())
                out += looseRun ? QByteArray("\n\n") : QByteArray("\n");
            continue;
        }

        QByteArray bytes;
        if (!isBlockTouched(id)) {
            // Untouched: use original load-time bytes for byte-identical
            // content round-trip. Strip the load-time terminator so the
            // serializer owns separator placement (B1 §3).
            bytes = d->blockLoadTimeBytes.value(id);
            if (bytes.endsWith('\n'))
                bytes.chop(1);
        } else {
            // Touched: re-serialize from CRDT state. Per-kind serializer is
            // contracted to emit body only — no terminator (B1 §4).
            auto fn = reg.get(kind);
            bytes = fn(kind, blockAttrs(id), blockText(id));
        }
        out += bytes;
        if (i + 1 < blocks.size())
            out += interBlockSeparator();
    }

    // B1: serializer owns the document-final '\n'. CommonMark convention.
    if (!blocks.empty())
        out += finalDocumentTerminator();

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
    onSaveComplete();
    return true;
}

bool MarkoffDocument::triggerGc()
{
    if (d->watermark) return d->watermark->onSaveSucceeded();
    return false;
}

void MarkoffDocument::onSaveComplete()
{
    if (!d->watermark) return;
    if (d->undoLog.isTransactionOpen()) {
        qWarning("MarkoffDocument::onSaveComplete: GC deferred — transaction open");
        return;
    }

    const quint64 W = d->maxProducedLamport;
    d->currentSnapshotWatermark = W;
    Q_EMIT localWatermarkAdvanced(W);

    if (d->ackedWatermark >= W) {
        // Gate open: compact immediately.
        d->watermark->compactNow();
        Q_EMIT watermarkCompacted(W);
    } else if (d->collabConfigured) {
        // Gate closed: wait for peer acks.
        Q_EMIT wantsAcksAtWatermark(W);
    } else {
        // Single-user without explicit notifyAcks: compact immediately.
        d->watermark->compactNow();
        Q_EMIT watermarkCompacted(W);
    }
}

void MarkoffDocument::simulateSaveSucceeded()
{
    onSaveComplete();
}

void MarkoffDocument::notifyAcksAtWatermark(quint64 watermark)
{
    if (watermark <= d->ackedWatermark) return;  // monotonic
    d->ackedWatermark = watermark;
    if (d->currentSnapshotWatermark > 0
        && d->ackedWatermark >= d->currentSnapshotWatermark
        && d->watermark)
    {
        d->watermark->compactNow();
        Q_EMIT watermarkCompacted(d->currentSnapshotWatermark);
    }
}

// ============================================================================
// D5: remote cursor state (Phase 7)
// ============================================================================

void MarkoffDocument::setRemoteCursor(quint16 r, Markoff::Cursor c, QColor color, QString label)
{
    d->remoteCursors[r] = { c, color, label };
    Q_EMIT remoteCursorChanged(r, std::move(c), std::move(color), std::move(label));
}

void MarkoffDocument::clearRemoteCursor(quint16 r)
{
    if (d->remoteCursors.remove(r) > 0)
        Q_EMIT remoteCursorCleared(r);
}

void MarkoffDocument::clearAllRemoteCursors()
{
    const auto keys = d->remoteCursors.keys();
    d->remoteCursors.clear();
    for (quint16 r : keys)
        Q_EMIT remoteCursorCleared(r);
}

Markoff::Cursor MarkoffDocument::remoteCursorOf(quint16 r) const
{
    auto it = d->remoteCursors.constFind(r);
    if (it == d->remoteCursors.constEnd()) return Markoff::NoCursor{};
    return it->cursor;
}

}  // namespace Markoff
