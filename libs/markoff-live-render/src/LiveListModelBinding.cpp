// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/AstBlockDiff.h>
#include "BlockWalker.h"

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockAnchor.h>
#include <markoff-parser/Document.h>

#include <QList>

namespace Markoff::LiveRender {

struct LiveListModelBinding::Private {
    Markoff::MarkoffDocument *document = nullptr;
    LiveBlockModel            *model   = nullptr;
    BlockKindRegistry          registry;  // built-ins registered in ctor
    QList<BlockKey>            lastKeys;
    quint64 lastParseInputEditSeq = 0;   // stored for R4 freshness rule
};

LiveListModelBinding::LiveListModelBinding(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->model = new LiveBlockModel(this);
}

LiveListModelBinding::~LiveListModelBinding() = default;

Markoff::MarkoffDocument *LiveListModelBinding::document() const
{
    return d->document;
}

void LiveListModelBinding::setDocument(Markoff::MarkoffDocument *doc)
{
    if (d->document == doc) return;
    if (d->document)
        QObject::disconnect(d->document, nullptr, this, nullptr);
    d->document = doc;
    if (d->document) {
        QObject::connect(d->document, &Markoff::MarkoffDocument::parseUpdated,
                         this, &LiveListModelBinding::onParseUpdated);
    }
    Q_EMIT documentChanged();
}

LiveBlockModel *LiveListModelBinding::model() const
{
    return d->model;
}

const BlockKindRegistry *LiveListModelBinding::registry() const
{
    return &d->registry;
}

void LiveListModelBinding::onParseUpdated(const Markoff::Document *parsed,
                                          quint64 /*parseSequence*/,
                                          const QList<Markoff::BlockAnchor> &blockAnchors,
                                          quint64 parseInputEditSequence)
{
    if (!parsed) return;

    d->lastParseInputEditSeq = parseInputEditSequence;

    QList<BlockRecord> records = BlockWalker::walk(parsed);

    // Align BlockAnchors 1:1 with topLevelBlocks() (and therefore records).
    QList<BlockKey> nextKeys;
    nextKeys.reserve(records.size());
    for (qsizetype i = 0; i < records.size(); ++i) {
        const Markoff::BlockAnchor anchor =
            (i < blockAnchors.size()) ? blockAnchors[i] : Markoff::BlockAnchor{};
        records[i].blockAnchor = anchor;
        nextKeys.append(BlockKey{ records[i].kind, anchor });
    }

    const QList<AstBlockDiff::Op> ops =
        AstBlockDiff::diff(d->lastKeys, nextKeys);

    d->model->applyOps(ops, records);
    d->lastKeys = std::move(nextKeys);
}

}  // namespace Markoff::LiveRender
