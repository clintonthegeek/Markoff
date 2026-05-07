// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveListModelBinding.h>
#include <markoff/live-render/AstBlockDiff.h>
#include <markoff/live-render/LiveStructuralKeyHandler.h>
#include <markoff/live-render/BlockKind.h>

#include "KindTransition.h"

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockAnchor.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/CrdtProxies.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/Cmd/D2.h>

#include <QList>
#include <QRegularExpression>
#include <QScopeGuard>

namespace Markoff::Live {

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
    d->cursorState   = new LiveCursorState(&d->registry, d->model, this, this);
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
        // Trim trailing newline. Per-item ListItem blocks have content-only
        // buffers (the parser strips all trailing newlines from the per-item
        // content). The single trailing '\n' is the block-delimiter convention.
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

        // Only promote FROM Paragraph. Blocks already set to a structural kind
        // (ListItem, Heading, CodeBlock, …) hold content-only text in their
        // buffers — inferBlockKind on content-only text would always infer
        // "paragraph", wrongly demoting the block. Demotion is always via
        // explicit structural-key actions (Enter-on-empty, Backspace, etc.).
        if (rec.kind != BlockKind::Paragraph) continue;

        // Map inferred string to foundation BlockKind enum.
        Markoff::BlockKind fk = Markoff::BlockKind::Paragraph;
        if      (inferred == BlockKind::Heading)        fk = Markoff::BlockKind::Heading;
        else if (inferred == BlockKind::CodeBlock)      fk = Markoff::BlockKind::CodeBlock;
        else if (inferred == BlockKind::HorizontalRule) fk = Markoff::BlockKind::HorizontalRule;
        else if (inferred == BlockKind::Image)          fk = Markoff::BlockKind::Image;
        else if (inferred == BlockKind::Math)           fk = Markoff::BlockKind::Math;
        else if (inferred == BlockKind::ListItem)       fk = Markoff::BlockKind::ListItem;
        else if (inferred == BlockKind::Blockquote)     fk = Markoff::BlockKind::BlockQuote;

        // ListItem promotion: parse marker, strip buffer, set attrs in one transaction.
        if (fk == Markoff::BlockKind::ListItem) {
            static const QRegularExpression kPromoteMarker(
                QStringLiteral(R"(^([ \t]{0,3})(\d{1,9})([.)]) (.*)$|^([ \t]{0,3})([-*+]) (.*)$)"));
            auto pm = kPromoteMarker.match(rec.text);
            if (!pm.hasMatch()) {
                // inferBlockKind matched but our parse failed — change kind without attrs.
                Markoff::Cmd::changeKind(*doc, Markoff::BlockId(rec.blockAnchor), fk, {}, {});
                return;
            }

            QString style;
            int number = 0;
            QString content;
            int leadingSpaces = 0;
            if (!pm.captured(2).isEmpty()) {
                leadingSpaces = pm.captured(1).size();
                number        = pm.captured(2).toInt();
                style         = (pm.captured(3) == QStringLiteral(".")) ? QStringLiteral("dot")
                                                                         : QStringLiteral("paren");
                content = pm.captured(4);
            } else {
                leadingSpaces = pm.captured(5).size();
                const QString c = pm.captured(6);
                style   = (c == QStringLiteral("-")) ? QStringLiteral("minus")
                        : (c == QStringLiteral("*")) ? QStringLiteral("star")
                                                      : QStringLiteral("plus");
                content = pm.captured(7);
            }
            const int indent = leadingSpaces / 2;

            UndoLog::Transaction t(doc->d2UndoLog());

            const QByteArray oldBuf = doc->blockText(Markoff::BlockId(rec.blockAnchor));
            doc->d2ApplyBufferEdit(Markoff::BlockId(rec.blockAnchor),
                                   0, static_cast<uint32_t>(oldBuf.size()),
                                   content.toUtf8(), t);
            doc->d2SetBlockKind(Markoff::BlockId(rec.blockAnchor), Markoff::BlockKind::ListItem, t);
            doc->d2SetBlockAttr(Markoff::BlockId(rec.blockAnchor),
                                Markoff::AttrNames::IndentLevel, indent, t);
            doc->d2SetBlockAttr(Markoff::BlockId(rec.blockAnchor),
                                Markoff::AttrNames::MarkerStyle, style, t);
            if (style == QStringLiteral("dot") || style == QStringLiteral("paren"))
                doc->d2SetBlockAttr(Markoff::BlockId(rec.blockAnchor),
                                    Markoff::AttrNames::MarkerNumber, number, t);
            doc->d2SetBlockAttr(Markoff::BlockId(rec.blockAnchor),
                                Markoff::AttrNames::LooseRun, false, t);
            Markoff::Cmd::renumberRunStartingAt(*doc, Markoff::BlockId(rec.blockAnchor), t);
            return;
        }

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

}

}  // namespace Markoff::Live
