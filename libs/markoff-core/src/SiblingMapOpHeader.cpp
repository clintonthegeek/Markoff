// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SiblingMapOpHeader.h>

#include <QDataStream>
#include <QIODevice>

namespace Markoff {

QByteArray SiblingMapOpHeader::encode(const SiblingMapOpHeader &h)
{
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_8);

    ds << quint32(h.key.size())
       << quint32(h.value.size())
       << h.lamportCounter
       << h.lamportReplicaId
       << quint8(h.isTombstone ? 1 : 0);
    ds.writeRawData(h.key.constData(), h.key.size());
    ds.writeRawData(h.value.constData(), h.value.size());
    return bytes;
}

bool SiblingMapOpHeader::decode(const QByteArray &bytes, SiblingMapOpHeader *out)
{
    if (!out) return false;
    if (bytes.size() < int(sizeof(quint32) * 2 + sizeof(quint64) + sizeof(quint16) + sizeof(quint8))) {
        return false;
    }

    QDataStream ds(bytes);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_8);

    quint32 keyLen = 0, valLen = 0;
    quint8 tomb = 0;
    ds >> keyLen >> valLen >> out->lamportCounter >> out->lamportReplicaId >> tomb;
    if (ds.status() != QDataStream::Ok) return false;
    out->isTombstone = (tomb != 0);

    QByteArray key(int(keyLen), Qt::Uninitialized);
    if (keyLen > 0) {
        const int read = ds.readRawData(key.data(), int(keyLen));
        if (read != int(keyLen)) return false;
    }
    out->key = key;

    QByteArray value(int(valLen), Qt::Uninitialized);
    if (valLen > 0) {
        const int read = ds.readRawData(value.data(), int(valLen));
        if (read != int(valLen)) return false;
    }
    out->value = value;

    return ds.status() == QDataStream::Ok;
}

}  // namespace Markoff
