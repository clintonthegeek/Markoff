// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff-foundation/MarkoffDocument.h>

#include <QList>

#include <memory>

#include <crdt/Buffer.h>

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
    int                                       coalescingIdleMs = 250;
    QList<Session *>                          sessions;  // filled by Task 23
    Markoff::Foundation::ParsePool            parsePool;
    std::unique_ptr<const Markoff::Document>  latestParse;
};

}  // namespace Markoff
