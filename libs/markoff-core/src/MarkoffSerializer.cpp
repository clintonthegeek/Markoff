// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/MarkoffSerializer.h>

#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <array>

namespace Markoff::MarkoffSerializer {

namespace {
constexpr quint32 kMagic = 0x4D4B4F46u;   // 'MKOF'
constexpr quint16 kSchemaVersion = 2;
}

QByteArray encode(const QList<MarkoffOp> &ops, const MarkoffBundleMeta &meta)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_8);

    if (ops.size() != meta.opCountInBundle) return {};
    for (const MarkoffOp& op : ops) {
        if (quint8(op.target) > quint8(CrdtTarget::FootnoteDefMap)) return {};
        const bool isBuffer = op.target == CrdtTarget::Buffer;
        if ((isBuffer && op.blockId.isNull())
            || (!isBuffer && !op.blockId.isNull()))
            return {};
    }

    ds << kMagic
       << kSchemaVersion
       << meta.producerReplicaId
       << meta.bundleId
       << meta.opCountInBundle
       << quint32(static_cast<int>(meta.actionId))
       << meta.producerLamport;

    for (const MarkoffOp &op : ops) {
        ds << quint8(op.target);
        const auto& actor = op.blockId.actorBytes();
        ds.writeRawData(reinterpret_cast<const char*>(actor.data()), int(actor.size()));
        ds << quint64(op.blockId.counter());
        ds << op.producerReplicaId << quint32(op.payload.size());
        ds.writeRawData(op.payload.constData(), op.payload.size());
    }
    return ds.status() == QDataStream::Ok ? bytes : QByteArray{};
}

bool decode(const QByteArray &bytes, QList<MarkoffOp> *outOps, MarkoffBundleMeta *outMeta)
{
    if (!outOps || !outMeta) return false;
    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_8);

    quint32 magic = 0;
    quint16 schema = 0;
    ds >> magic >> schema;
    if (magic != kMagic || schema != kSchemaVersion) return false;

    quint32 actionIdRaw = 0;
    ds >> outMeta->producerReplicaId
       >> outMeta->bundleId
       >> outMeta->opCountInBundle
       >> actionIdRaw
       >> outMeta->producerLamport;
    outMeta->actionId = static_cast<ActionId>(actionIdRaw);
    if (ds.status() != QDataStream::Ok) return false;

    outOps->clear();
    for (quint16 i = 0; i < outMeta->opCountInBundle; ++i) {
        quint8 targetRaw = 0;
        quint32 payloadLen = 0;
        MarkoffOp op;
        ds >> targetRaw;
        std::array<uint8_t, 16> actor{};
        if (ds.readRawData(reinterpret_cast<char*>(actor.data()), int(actor.size()))
            != int(actor.size())) return false;
        quint64 counter = 0;
        ds >> counter;
        if (ds.status() != QDataStream::Ok) return false;
        if (counter == 0 && std::all_of(actor.cbegin(), actor.cend(), [](uint8_t b) { return b == 0; })) {
            op.blockId = BlockId{};
        } else {
            const auto identity = BlockId::fromStructuralIdentity(actor, counter);
            if (!identity) return false;
            op.blockId = *identity;
        }
        ds >> op.producerReplicaId >> payloadLen;
        if (ds.status() != QDataStream::Ok) return false;
        op.target = static_cast<CrdtTarget>(targetRaw);
        const bool isBuffer = op.target == CrdtTarget::Buffer;
        if (targetRaw > quint8(CrdtTarget::FootnoteDefMap)
            || (isBuffer && op.blockId.isNull())
            || (!isBuffer && !op.blockId.isNull())) return false;
        QByteArray payload(int(payloadLen), Qt::Uninitialized);
        if (payloadLen > 0) {
            const int read = ds.readRawData(payload.data(), int(payloadLen));
            if (read != int(payloadLen)) return false;
        }
        op.payload = payload;
        outOps->append(op);
    }
    return ds.status() == QDataStream::Ok && ds.atEnd();
}

}  // namespace Markoff::MarkoffSerializer
