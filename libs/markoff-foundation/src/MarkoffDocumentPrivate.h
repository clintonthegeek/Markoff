// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/MarkoffDocument.h>

#include <QList>

#include <memory>

#include <crdt/Buffer.h>

#include "BlockAnchorComputation.h"
#include "ParsePool.h"

namespace Markoff {

class Session;

struct MarkoffDocument::Private {
    explicit Private(uint16_t replicaId)
        : buffer(replicaId)
        , replicaId(replicaId)
    {}

    CollabText::Crdt::Buffer                  buffer;
    quint16                                   replicaId;
    quint64                                   editSequence = 0;   ///< Bumps on every state-change op.
    quint64                                   parseSequence = 0;  ///< Bumps each time parseUpdated is emitted.
    QList<Markoff::BlockAnchor>               latestBlockAnchors;
    QList<Markoff::Detail::BlockByteRange>    latestBlockRanges;
    QList<Session *>                          sessions;  // filled by Task 23
    Markoff::Parse::Detail::ParsePool         parsePool;
    std::unique_ptr<const Markoff::Document>  latestParse;
};

}  // namespace Markoff
