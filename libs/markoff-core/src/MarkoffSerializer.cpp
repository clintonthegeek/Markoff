// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/MarkoffSerializer.h>

#include <QDataStream>
#include <QIODevice>

namespace Markoff::MarkoffSerializer {

namespace {
constexpr quint32 kMagic = 0x4D4B4F46u;   // 'MKOF'
constexpr quint16 kSchemaVersion = 1;
}

QByteArray encode(const QList<MarkoffOp> &ops, const MarkoffBundleMeta &meta)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_8);

    ds << kMagic
       << kSchemaVersion
       << meta.producerReplicaId
       << meta.bundleId
       << meta.opCountInBundle
       << quint32(static_cast<int>(meta.actionId))
       << meta.producerLamport;

    for (const MarkoffOp &op : ops) {
        ds << quint8(op.target)
           << op.blockId
           << op.producerReplicaId
           << quint32(op.payload.size());
        ds.writeRawData(op.payload.constData(), op.payload.size());
    }
    return bytes;
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
        ds >> targetRaw >> op.blockId >> op.producerReplicaId >> payloadLen;
        if (ds.status() != QDataStream::Ok) return false;
        op.target = static_cast<CrdtTarget>(targetRaw);
        QByteArray payload(int(payloadLen), Qt::Uninitialized);
        if (payloadLen > 0) {
            const int read = ds.readRawData(payload.data(), int(payloadLen));
            if (read != int(payloadLen)) return false;
        }
        op.payload = payload;
        outOps->append(op);
    }
    return ds.status() == QDataStream::Ok;
}

}  // namespace Markoff::MarkoffSerializer
