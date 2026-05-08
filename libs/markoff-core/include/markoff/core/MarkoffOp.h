// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/ActionId.h>
#include <markoff/core/MarkoffCoreExport.h>

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QtGlobal>

namespace Markoff {

/// What CRDT a MarkoffOp targets. The consumer routes by this tag.
/// See D5 spec §2.3.
enum class CrdtTarget : quint8 {
    IdList         = 0,
    Buffer         = 1,
    KindTagMap     = 2,
    BlockAttrsMap  = 3,
    FrontmatterMap = 4,
    LinkRefMap     = 5,
    FootnoteDefMap = 6,
};

/// A single op crossing the boundary. Opaque payload.
struct MARKOFF_CORE_EXPORT MarkoffOp {
    CrdtTarget  target            = CrdtTarget::IdList;
    quint64     blockId           = 0;     // valid iff target==Buffer
    QByteArray  payload;
    quint16     producerReplicaId = 0;
};

/// Metadata identifying the user-action this set of ops belongs to.
/// One bundle = one transaction = one user-action.
struct MARKOFF_CORE_EXPORT MarkoffBundleMeta {
    quint16    producerReplicaId = 0;
    quint64    bundleId          = 0;     // monotonic per producer
    quint16    opCountInBundle   = 0;
    ActionId   actionId          = ActionId::None;
    quint64    producerLamport   = 0;
};

}  // namespace Markoff

Q_DECLARE_METATYPE(Markoff::MarkoffOp)
Q_DECLARE_METATYPE(Markoff::MarkoffBundleMeta)
Q_DECLARE_METATYPE(QList<Markoff::MarkoffOp>)
