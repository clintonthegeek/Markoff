// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/AstBlockDiff.h>
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/UndoCoalescer.h>
#include "BlockWalker.h"

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/Session.h>
#include <markoff-parser/Document.h>

#include <QList>
#include <QScopeGuard>

namespace Markoff::LiveRender {

struct LiveListModelBinding::Private {
    Markoff::MarkoffDocument *document     = nullptr;
    Markoff::Session         *session      = nullptr;
    LiveBlockModel            *model       = nullptr;
    BlockKindRegistry          registry;
    LiveCursorState           *cursorState   = nullptr;
    BlockHitTester            *hitTester     = nullptr;
    LiveSelectionView         *selectionView = nullptr;
    UndoCoalescer             *undoCoalescer = nullptr;
    LiveStructuralKeyHandler  *structuralKeys = nullptr;
    LiveHoleLayer             *holeLayer   = nullptr;
    LiveProxyBlockModel       *proxyModel  = nullptr;
    QList<BlockKey>            lastKeys;
    quint64                    lastParseInputEditSeq = 0;
    bool                       applyingModelUpdate = false;
};

LiveListModelBinding::LiveListModelBinding(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->model         = new LiveBlockModel(this);
    d->cursorState   = new LiveCursorState(&d->registry, d->model, this);
    d->hitTester     = new BlockHitTester(this);
    d->selectionView = new LiveSelectionView(this);
    d->selectionView->setModel(d->model);
}

LiveListModelBinding::~LiveListModelBinding() = default;

Markoff::MarkoffDocument *LiveListModelBinding::document() const
{
    return d->document;
}

void LiveListModelBinding::setDocument(Markoff::MarkoffDocument *doc)
{
    if (d->document == doc) return;
    if (d->document) {
        QObject::disconnect(d->document, nullptr, this, nullptr);
        if (d->session) {
            d->document->destroySession(d->session);
            d->session = nullptr;
        }
    }
    // Drop old structural components before reconstructing (reverse construction order).
    delete d->proxyModel;     d->proxyModel     = nullptr;
    delete d->holeLayer;      d->holeLayer      = nullptr;
    delete d->structuralKeys; d->structuralKeys = nullptr;
    delete d->undoCoalescer;  d->undoCoalescer  = nullptr;

    d->document = doc;
    if (d->document) {
        QObject::connect(d->document, &Markoff::MarkoffDocument::parseUpdated,
                         this, &LiveListModelBinding::onParseUpdated);
        d->session = d->document->createSession({});
        d->selectionView->setDocument(d->document);
        d->selectionView->setSession(d->session);
        d->undoCoalescer  = new UndoCoalescer(d->document, this);
        d->holeLayer      = new LiveHoleLayer(d->document, d->model, d->undoCoalescer, this);
        d->proxyModel     = new LiveProxyBlockModel(d->document, d->model, d->holeLayer, this);
        // End-to-end 250 ms quiet-commit path: idle timer → reification.
        QObject::connect(d->holeLayer, &LiveHoleLayer::idleCommitDue,
                         d->holeLayer, &LiveHoleLayer::commitBlockHole);
        Q_EMIT holeLayerChanged();
        Q_EMIT proxyModelChanged();
        d->structuralKeys = new LiveStructuralKeyHandler(
            d->document, d->model, d->cursorState, &d->registry,
            d->undoCoalescer, d->holeLayer, d->proxyModel, this);
    } else {
        d->selectionView->setDocument(nullptr);
        d->selectionView->setSession(nullptr);
        Q_EMIT holeLayerChanged();
        Q_EMIT proxyModelChanged();
    }
    Q_EMIT documentChanged();
}

LiveBlockModel           *LiveListModelBinding::model()               const { return d->model; }
LiveCursorState          *LiveListModelBinding::cursorState()         const { return d->cursorState; }
BlockHitTester           *LiveListModelBinding::hitTester()           const { return d->hitTester; }
LiveSelectionView        *LiveListModelBinding::selectionView()       const { return d->selectionView; }
LiveStructuralKeyHandler *LiveListModelBinding::structuralKeyHandler() const { return d->structuralKeys; }
UndoCoalescer            *LiveListModelBinding::undoCoalescer()       const { return d->undoCoalescer; }
const BlockKindRegistry  *LiveListModelBinding::registry()            const { return &d->registry; }
LiveHoleLayer            *LiveListModelBinding::holeLayer()           const { return d->holeLayer; }
LiveProxyBlockModel      *LiveListModelBinding::proxyModel()          const { return d->proxyModel; }

bool LiveListModelBinding::applyingModelUpdate() const
{
    return d->applyingModelUpdate;
}

void LiveListModelBinding::onParseUpdated(const Markoff::Document *parsed,
                                          quint64 parseSequence,
                                          const QList<Markoff::BlockAnchor> &blockAnchors,
                                          quint64 parseInputEditSequence)
{
    if (!parsed) return;
    d->lastParseInputEditSeq = parseInputEditSequence;

    QList<BlockRecord> records = BlockWalker::walk(parsed);
    QList<BlockKey> nextKeys;
    nextKeys.reserve(records.size());
    for (qsizetype i = 0; i < records.size(); ++i) {
        const Markoff::BlockAnchor anchor =
            (i < blockAnchors.size()) ? blockAnchors[i] : Markoff::BlockAnchor{};
        records[i].blockAnchor = anchor;
        nextKeys.append(BlockKey{ records[i].kind, anchor });
    }

    const QList<AstBlockDiff::Op> ops = AstBlockDiff::diff(d->lastKeys, nextKeys);

    d->applyingModelUpdate = true;
    auto _ = qScopeGuard([this]{ d->applyingModelUpdate = false; });
    d->model->applyOps(ops, records, parseInputEditSequence);

    d->lastKeys = std::move(nextKeys);

    // Re-resolve the cached byte offset of the active TextCaret cursor.
    // Local edits between parse arrivals shift the resolved byte position
    // of the cursor's TextAnchor; the cached offset is consulted by
    // selection rendering and structural-key dispatch (R5). Spec §3.3.
    if (d->cursorState) {
        const Cursor cur = d->cursorState->cursor();
        if (auto *tc = std::get_if<TextCaret>(&cur)) {
            // Hole-side carets don't have a foundation block range; skip refresh.
            if (isHoleBlockId(tc->block)) return;
            const auto blockRangeOpt = d->document->blockByteRange(anchorOf(tc->block));
            if (blockRangeOpt) {
                const quint32 blockStart = blockRangeOpt->first;
                const quint32 resolvedAbs = d->document->resolveTextAnchor(tc->positionAnchor);
                TextCaret refreshed = *tc;
                refreshed.cachedByteOffset = (resolvedAbs >= blockStart)
                    ? resolvedAbs - blockStart : 0;
                if (refreshed.cachedByteOffset != tc->cachedByteOffset) {
                    d->cursorState->request(refreshed);
                }
            }
        }
    }

    // R5: advance the pending-cursor drop counter (spec §8.4).
    if (d->cursorState)
        d->cursorState->noteParseArrived(parseSequence);
}

}  // namespace Markoff::LiveRender
