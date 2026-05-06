// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/AstBlockDiff.h>
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/BlockKind.h>

#include "KindTransition.h"

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/BlockKind.h>
#include <markoff-foundation/CrdtProxies.h>
#include <markoff-foundation/AttrNames.h>
#include <markoff-foundation/Cmd/D2.h>

#include <QList>
#include <QScopeGuard>

namespace Markoff::LiveRender {

namespace {

/// Convert the foundation's BlockKind enum to the LiveRender QString kind
/// constant used by BlockRecord / BlockKindRegistry.
QString blockKindToString(Markoff::BlockKind k)
{
    using BK = Markoff::BlockKind;
    switch (k) {
    case BK::Heading:        return BlockKind::Heading;
    case BK::CodeBlock:      return BlockKind::CodeBlock;
    case BK::HorizontalRule: return BlockKind::HorizontalRule;
    case BK::Image:          return BlockKind::Image;
    case BK::ListItem:       return BlockKind::ListItem;
    case BK::BlockQuote:     return BlockKind::Blockquote;
    case BK::Math:           return BlockKind::Math;
    // Paragraph and all other kinds map to Paragraph in the current LiveRender
    // model — same as BlockWalker's catch-all. Additional kinds gain their own
    // strings in future phases.
    default:                 return BlockKind::Paragraph;
    }
}

}  // namespace

struct LiveListModelBinding::Private {
    Markoff::MarkoffDocument *document      = nullptr;
    LiveBlockModel            *model        = nullptr;
    BlockKindRegistry          registry;
    LiveCursorState           *cursorState   = nullptr;
    BlockHitTester            *hitTester     = nullptr;
    LiveSelectionView         *selectionView = nullptr;
    LiveStructuralKeyHandler  *structuralKeys = nullptr;
    QList<BlockKey>            lastKeys;
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
    }
    // Drop old structural components before reconstructing.
    delete d->structuralKeys; d->structuralKeys = nullptr;

    d->document = doc;
    if (d->document) {
        // D2: drive the model from two signal sources:
        // 1. documentLoaded() fires synchronously inside loadFromMarkdown(),
        //    giving immediate model population without waiting for the event loop.
        // 2. d2DocumentChanged() fires debounced (once per event-loop spin) after
        //    any D2 CRDT change (d2InsertBlock, d2RemoveBlock, d2ApplyBufferEdit,
        //    Cmd::enterAtEnd, backspaceMerge, deleteMerge, etc.).
        // The proxy signals (idListProxy::structureChanged, kindTagMapProxy::mapChanged)
        // are NOT connected here because they only fire from the higher-level
        // applyStructural() API, not from the d2InsertBlock/d2RemoveBlock paths
        // used by Cmd::*.
        QObject::connect(d->document, &Markoff::MarkoffDocument::documentLoaded,
                         this, &LiveListModelBinding::onD2Changed);
        QObject::connect(d->document, &Markoff::MarkoffDocument::d2DocumentChanged,
                         this, &LiveListModelBinding::onD2Changed);

        d->selectionView->setDocument(d->document);
        d->selectionView->setSession(nullptr);

        d->structuralKeys = new LiveStructuralKeyHandler(
            d->document, d->model, d->cursorState, &d->registry, this);
    } else {
        d->selectionView->setDocument(nullptr);
        d->selectionView->setSession(nullptr);
        d->lastKeys.clear();
    }
    Q_EMIT documentChanged();
}

LiveBlockModel           *LiveListModelBinding::model()               const { return d->model; }
LiveCursorState          *LiveListModelBinding::cursorState()         const { return d->cursorState; }
BlockHitTester           *LiveListModelBinding::hitTester()           const { return d->hitTester; }
LiveSelectionView        *LiveListModelBinding::selectionView()       const { return d->selectionView; }
LiveStructuralKeyHandler *LiveListModelBinding::structuralKeyHandler() const { return d->structuralKeys; }
const BlockKindRegistry  *LiveListModelBinding::registry()            const { return &d->registry; }

bool LiveListModelBinding::applyingModelUpdate() const
{
    return d->applyingModelUpdate;
}

void LiveListModelBinding::onD2Changed()
{
    auto *doc = d->document;
    if (!doc) return;

    // Build the new record list from D2 CRDT state.
    const auto blockIds = doc->iterateBlocks();
    QList<BlockRecord> records;
    records.reserve(static_cast<int>(blockIds.size()));
    for (const auto &id : blockIds) {
        BlockRecord r;
        r.blockAnchor = id;   // BlockAnchor == BlockId
        r.kind        = blockKindToString(doc->blockKind(id));

        QByteArray raw = doc->blockText(id);
        // Trim the trailing newline that the D2 block buffer stores as a
        // block delimiter (analogous to BlockWalker's trailing-\n trim).
        // This keeps qtPos == text.length() at the logical end of the block
        // content, not inside the inter-block separator.
        if (raw.endsWith('\n'))
            raw.chop(1);
        r.text = QString::fromUtf8(raw);

        // Populate inline spans and block attrs from the foundation CRDT.
        r.inlineSpans = doc->inlineSpansFor(id);
        r.attrs       = doc->blockAttrs(id);

        // Populate kind-specific extras from attrs map.
        if (r.kind == BlockKind::Heading) {
            auto it = r.attrs.find(Markoff::AttrNames::Level);
            if (it != r.attrs.end()) {
                if (const int *v = std::get_if<int>(&it.value()))
                    r.headingLevel = *v;
            }
        } else if (r.kind == BlockKind::CodeBlock) {
            auto it = r.attrs.find(Markoff::AttrNames::InfoString);
            if (it != r.attrs.end()) {
                if (const QString *v = std::get_if<QString>(&it.value()))
                    r.codeLanguage = *v;
            }
        }

        records.append(r);
    }

    QList<BlockKey> nextKeys;
    nextKeys.reserve(records.size());
    for (const auto &r : records)
        nextKeys.append(BlockKey{ r.kind, r.blockAnchor });

    const QList<AstBlockDiff::Op> ops = AstBlockDiff::diff(d->lastKeys, nextKeys);

    // Kind-transition detection: for Equal-op blocks, check if the text now
    // implies a different kind than what the CRDT stores. If so, issue a
    // changeKind command and return — the resulting d2DocumentChanged signal
    // will re-fire onD2Changed with the corrected kind.
    for (const auto &op : ops) {
        if (op.kind != AstBlockDiff::OpKind::Equal) continue;
        const int idx = op.nextIndex;
        if (idx < 0 || idx >= records.size()) continue;
        const auto &rec = records[idx];

        bool displayMode = false;
        const QString inferred = inferBlockKind(rec.text, &displayMode);

        if (inferred == rec.kind) {
            // Same kind — check if heading level changed.
            if (rec.kind == BlockKind::Heading) {
                const int newLevel = countLeadingHashes(rec.text);
                if (newLevel > 0 && newLevel != rec.headingLevel) {
                    Markoff::Cmd::changeKind(*doc,
                                             Markoff::BlockId(rec.blockAnchor),
                                             Markoff::BlockKind::Heading,
                                             {Markoff::AttrNames::Level},
                                             {newLevel});
                    return;
                }
            }
            continue;
        }

        // Map inferred string to foundation BlockKind enum.
        Markoff::BlockKind fk = Markoff::BlockKind::Paragraph;
        if      (inferred == BlockKind::Heading)        fk = Markoff::BlockKind::Heading;
        else if (inferred == BlockKind::CodeBlock)      fk = Markoff::BlockKind::CodeBlock;
        else if (inferred == BlockKind::HorizontalRule) fk = Markoff::BlockKind::HorizontalRule;
        else if (inferred == BlockKind::Image)          fk = Markoff::BlockKind::Image;
        else if (inferred == BlockKind::Math)           fk = Markoff::BlockKind::Math;
        else if (inferred == BlockKind::ListItem)       fk = Markoff::BlockKind::ListItem;
        else if (inferred == BlockKind::Blockquote)     fk = Markoff::BlockKind::BlockQuote;

        QList<Markoff::AttrName> attrNames;
        QList<Markoff::AttrValue> attrVals;
        if (fk == Markoff::BlockKind::Heading) {
            attrNames << Markoff::AttrNames::Level;
            attrVals  << int(countLeadingHashes(rec.text));
        } else if (fk == Markoff::BlockKind::Math) {
            attrNames << Markoff::AttrNames::DisplayMode;
            attrVals  << displayMode;
        }

        Markoff::Cmd::changeKind(*doc,
                                  Markoff::BlockId(rec.blockAnchor),
                                  fk, attrNames, attrVals);
        // changeKind will schedule another d2DocumentChanged; return to let
        // the next spin re-run onD2Changed with the corrected kind.
        return;
    }

    d->applyingModelUpdate = true;
    auto _ = qScopeGuard([this]{ d->applyingModelUpdate = false; });
    d->model->applyOps(ops, records);
    d->lastKeys = std::move(nextKeys);

    // Emit structural signals so LiveCursorState can resolve pending cursors
    // without going through the parse-cycle path.
    for (const auto &op : ops) {
        if (op.kind == AstBlockDiff::OpKind::Insert) {
            Q_EMIT structuralRowsInserted(op.nextIndex, op.nextIndex);
        } else if (op.kind == AstBlockDiff::OpKind::Delete) {
            Q_EMIT structuralRowRemoved(op.prevIndex);
        }
    }

    // Advance the pending-cursor drop counter.
    if (d->cursorState)
        d->cursorState->noteParseArrived(doc->d2EditSequence());
}

}  // namespace Markoff::LiveRender
