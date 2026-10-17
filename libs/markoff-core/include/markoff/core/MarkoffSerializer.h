// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/MarkoffOp.h>
#include <markoff/core/MarkoffCoreExport.h>

#include <QByteArray>
#include <QList>

namespace Markoff::MarkoffSerializer {

/// Encode a bundle of ops + metadata into a single canonical byte blob.
/// Format (little-endian):
///   [u32 magic = 0x4D4B4F46 'MKOF']
///   [u16 schemaVersion = 2]
///   [u16 producerReplicaId][u64 bundleId][u16 opCountInBundle]
///   [u32 actionId][u64 producerLamport]
///   for each op:
///     [u8 target][16 actor bytes][u64 actor-local counter]
///     [u16 producerReplicaId][u32 payloadLen][payload]. Non-buffer targets
///     carry an all-zero identity; buffer targets require a valid identity.
/// Schema 1 numeric block IDs are not accepted.
MARKOFF_CORE_EXPORT QByteArray encode(const QList<MarkoffOp> &ops,
                                      const MarkoffBundleMeta &meta);

/// Decode a blob produced by encode(). Returns false on truncation,
/// magic mismatch, unknown schemaVersion, or any other malformation.
MARKOFF_CORE_EXPORT bool decode(const QByteArray &bytes,
                                QList<MarkoffOp> *outOps,
                                MarkoffBundleMeta *outMeta);

}  // namespace Markoff::MarkoffSerializer
