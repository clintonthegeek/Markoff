// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveListModelBinding.h>
#include <markoff/live/AstBlockDiff.h>
#include <markoff/live/LiveStructuralKeyHandler.h>
#include <markoff/live/LiveNavigationController.h>
#include <markoff/live/BlockKind.h>

#include "KindTransition.h"

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/BlockAnchor.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/CrdtProxies.h>
#include <markoff/core/Session.h>
#include <markoff/core/AttrNames.h>
#include <markoff/core/Cmd/D2.h>
#include <markoff/core/Cursor.h>
#include <markoff/core/Theme.h>

#include <QAbstractListModel>
#include <QColor>
#include <QGuiApplication>
#include <QList>
#include <QRegularExpression>
#include <QScopeGuard>

namespace Markoff::Live {

namespace {

// ============================================================================
// RemoteCursorsListModel — QAbstractListModel for remote cursor overlays (D5)
// ============================================================================

class RemoteCursorsListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ReplicaIdRole = Qt::UserRole + 1,
        ColorRole,
        LabelRole,
    };

    struct RemoteEntry {
        quint16 replicaId;
        QColor  color;
        QString label;
    };

    explicit RemoteCursorsListModel(QObject *parent = nullptr)
        : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
            return {};
        const auto &e = m_entries[index.row()];
        switch (role) {
        case ReplicaIdRole: return QVariant::fromValue(e.replicaId);
        case ColorRole:     return QVariant::fromValue(e.color);
        case LabelRole:     return e.label;
        default:            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override {
        return {
            { ReplicaIdRole, "replicaId" },
            { ColorRole,     "color" },
            { LabelRole,     "label" },
        };
    }

    void onRemoteCursorChanged(quint16 replicaId, Markoff::Cursor /*cursor*/,
                               QColor color, QString label) {
        for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
            if (m_entries[i].replicaId == replicaId) {
                m_entries[i].color = color;
                m_entries[i].label = label;
                const QModelIndex idx = index(i);
                Q_EMIT dataChanged(idx, idx, {ColorRole, LabelRole});
                return;
            }
        }
        // New entry
        const int row = static_cast<int>(m_entries.size());
        beginInsertRows({}, row, row);
        m_entries.push_back({ replicaId, color, label });
        endInsertRows();
    }

    void onRemoteCursorCleared(quint16 replicaId) {
        for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
            if (m_entries[i].replicaId == replicaId) {
                beginRemoveRows({}, i, i);
                m_entries.erase(m_entries.begin() + i);
                endRemoveRows();
                return;
            }
        }
    }

private:
    std::vector<RemoteEntry> m_entries;
};

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
    Markoff::MarkoffDocument *document          = nullptr;
    LiveBlockModel            *model            = nullptr;
    BlockKindRegistry          registry;
    LiveCursorState           *cursorState      = nullptr;
    BlockHitTester            *hitTester        = nullptr;
    LiveSelectionView         *selectionView    = nullptr;
    LiveStructuralKeyHandler  *structuralKeys   = nullptr;
    LiveNavigationController  *navigationCtrl   = nullptr;
    RemoteCursorsListModel    *remoteCursors    = nullptr;
    LiveClipboardController   *clipboard        = nullptr;
    LiveActionController      *actions          = nullptr;
    LiveFormatController      *format           = nullptr;
    Capabilities               caps            = AllCapabilities;
    QList<BlockKey>            lastKeys;
    bool                       applyingModelUpdate = false;
    Markoff::Theme             theme            = Markoff::Theme::defaultLight();
};

LiveListModelBinding::LiveListModelBinding(QObject *parent)
    : LiveListModelBinding(AllCapabilities, parent) {}

LiveListModelBinding::LiveListModelBinding(Capabilities caps, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->caps            = caps;
    d->model           = new LiveBlockModel(this);
    d->cursorState     = new LiveCursorState(&d->registry, d->model, this, this);
    d->hitTester       = new BlockHitTester(this);
    d->selectionView   = new LiveSelectionView(this);
    d->selectionView->setModel(d->model);
    d->navigationCtrl  = new LiveNavigationController(&d->registry, d->model, d->cursorState, d->selectionView, this);
    d->remoteCursors   = new RemoteCursorsListModel(this);

    if (caps & Clipboard) {
        d->clipboard = new LiveClipboardController(this);
        d->clipboard->setSelectionView(d->selectionView);
        d->clipboard->setModel(d->model);
    }
    if (caps & Format) {
        d->format = new LiveFormatController(this);
        d->format->setSelectionView(d->selectionView);
        d->format->setModel(d->model);
    }
    // LiveActionController creates QActions, which require QGuiApplication.
    // Headless tests (QCoreApplication only) skip this sub-controller safely.
    if ((caps & Actions) && qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        d->actions = new LiveActionController(this);
        d->actions->setSelectionView(d->selectionView);
        if (d->clipboard) d->actions->setClipboardController(d->clipboard);
        if (d->format)    d->actions->setFormatController(d->format);
    }
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

        // D5: wire remote cursor signals to the overlay model.
        QObject::connect(d->document, &Markoff::MarkoffDocument::remoteCursorChanged,
                         d->remoteCursors, &RemoteCursorsListModel::onRemoteCursorChanged);
        QObject::connect(d->document, &Markoff::MarkoffDocument::remoteCursorCleared,
                         d->remoteCursors, &RemoteCursorsListModel::onRemoteCursorCleared);

        d->selectionView->setDocument(d->document);
        d->selectionView->setSession(nullptr);

        if (d->clipboard) d->clipboard->setDocument(d->document);
        if (d->format)    d->format->setDocument(d->document);
        if (d->actions)   d->actions->setDocument(d->document);

        d->structuralKeys = new LiveStructuralKeyHandler(
            d->document, d->model, d->cursorState, &d->registry, this);
    } else {
        d->selectionView->setDocument(nullptr);
        d->selectionView->setSession(nullptr);

        if (d->clipboard) d->clipboard->setDocument(nullptr);
        if (d->format)    d->format->setDocument(nullptr);
        if (d->actions)   d->actions->setDocument(nullptr);

        d->lastKeys.clear();
    }
    Q_EMIT documentChanged();
}

void LiveListModelBinding::setSession(Markoff::Session *session)
{
    d->selectionView->setSession(session);
}

LiveBlockModel           *LiveListModelBinding::model()               const { return d->model; }
LiveCursorState          *LiveListModelBinding::cursorState()         const { return d->cursorState; }
BlockHitTester           *LiveListModelBinding::hitTester()           const { return d->hitTester; }
LiveSelectionView        *LiveListModelBinding::selectionView()       const { return d->selectionView; }
LiveStructuralKeyHandler *LiveListModelBinding::structuralKeyHandler() const { return d->structuralKeys; }
LiveNavigationController *LiveListModelBinding::navigationController() const { return d->navigationCtrl; }
const BlockKindRegistry  *LiveListModelBinding::registry()            const { return &d->registry; }
QAbstractListModel       *LiveListModelBinding::remoteCursorsModel()  const { return d->remoteCursors; }

LiveClipboardController *LiveListModelBinding::clipboardController() const { return d->clipboard; }
LiveActionController    *LiveListModelBinding::actionController()    const { return d->actions; }
LiveFormatController    *LiveListModelBinding::formatController()    const { return d->format; }

bool LiveListModelBinding::applyingModelUpdate() const
{
    return d->applyingModelUpdate;
}

const Markoff::Theme *LiveListModelBinding::theme() const noexcept
{
    return &d->theme;
}

void LiveListModelBinding::setTheme(const Markoff::Theme *theme)
{
    if (!theme || theme == &d->theme) return;
    d->theme = *theme;
    Q_EMIT themeChanged();
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
        //
        // Carve-out: HR and Image have NO content-only mode — their buffers
        // always hold the source-literal pattern (e.g. "---", "![alt](url)").
        // When text deviates from that pattern (e.g. backspace-merge appends
        // following block's content into an HR block) we MUST demote, or the
        // block ends up rendered as a structural kind that doesn't match its
        // content and rejects cursor placement (HR has no TextCaret variant).
        if (rec.kind != BlockKind::Paragraph
            && rec.kind != BlockKind::HorizontalRule
            && rec.kind != BlockKind::Image) continue;

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

#include "LiveListModelBinding.moc"
