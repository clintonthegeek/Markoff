// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffCoreExport.h>

#include <QByteArray>
#include <QtGlobal>

namespace Markoff {

/// Wire-format header for a sibling-map op. Encoded into MarkoffOp::payload.
/// See D5 spec §3.1.
struct MARKOFF_CORE_EXPORT SiblingMapOpHeader {
    QByteArray  key;
    QByteArray  value;             // empty == tombstone
    quint64     lamportCounter   = 0;
    quint16     lamportReplicaId = 0;
    bool        isTombstone      = false;

    /// Serialize. Format (little-endian):
    ///   [u32 keyLen][u32 valLen][u64 lamportCounter][u16 lamportReplicaId][u8 isTombstone]
    ///   [keyLen bytes of key][valLen bytes of value]
    static QByteArray encode(const SiblingMapOpHeader &h);

    /// Decode; returns false on truncation/malformation.
    static bool decode(const QByteArray &bytes, SiblingMapOpHeader *out);
};

}  // namespace Markoff
