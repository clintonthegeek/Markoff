// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.

#include <markoff/live/EditorWidget.h>

#include <QAbstractItemModel>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <utility>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/EditorContext.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>

namespace Markoff::Live {

namespace {

// Flat-line model per contract-v2 spec §3 (normative): each model block
// contributes 1 + (internal '\n' count) visual flat lines; blocks are
// separated by exactly one line boundary; `column` is the 1-based UTF-16
// position within the line. This matches what the Styled/Source leaves
// report over their widgetFlatView-seeded QTextDocument
// (blockNumber()+1, positionInBlock()+1).
//
// Maps (model block row, intra-block UTF-16 qtPos) → CursorPos.
// Unknown block row → {1,1}.
Markoff::CursorPos toCursorPos(const Markoff::MarkoffDocument *doc,
                               int blockRow, int qtPos)
{
    int line = 1;
    const auto ids = doc->iterateBlocks();
    for (int row = 0; row < int(ids.size()); ++row) {
        const QString text =
            QString::fromUtf8(doc->blockText(ids[std::size_t(row)]));
        if (row == blockRow) {
            const int pos = qBound(0, qtPos, int(text.size()));
            const QStringView before = QStringView(text).left(pos);
            const int innerLine = int(before.count(QLatin1Char('\n')));
            const qsizetype lastNl = before.lastIndexOf(QLatin1Char('\n'));
            const int lineStart = (lastNl < 0) ? 0 : int(lastNl) + 1;
            return { line + innerLine, pos - lineStart + 1 };
        }
        line += 1 + int(text.count(QLatin1Char('\n')));
    }
    return {1, 1};
}

// Inverse: flat (line, column) → (model block row, intra-block qtPos).
// Clamps — never a no-op: a past-the-end line lands at the end of the
// last block; an over-long column lands at the line's end. Empty
// document → {0, 0} (the chokepoint rejects row 0 on an empty model).
std::pair<int, int> fromCursorPos(const Markoff::MarkoffDocument *doc,
                                  Markoff::CursorPos p)
{
    const auto ids = doc->iterateBlocks();
    if (ids.empty()) return {0, 0};
    int line = 1;
    for (int row = 0; row < int(ids.size()); ++row) {
        const QString text =
            QString::fromUtf8(doc->blockText(ids[std::size_t(row)]));
        const int span = 1 + int(text.count(QLatin1Char('\n')));
        if (p.line < line + span) {
            // Skip (p.line - line) inner '\n's to the start of the
            // target line, then clamp the column to that line's end.
            int pos = 0;
            for (int i = 0; i < p.line - line; ++i)
                pos = int(text.indexOf(QLatin1Char('\n'), pos)) + 1;
            const qsizetype nl = text.indexOf(QLatin1Char('\n'), pos);
            const int lineEnd = (nl < 0) ? int(text.size()) : int(nl);
            return { row, qMin(pos + qMax(0, p.column - 1), lineEnd) };
        }
        line += span;
    }
    const QString last = QString::fromUtf8(doc->blockText(ids.back()));
    return { int(ids.size()) - 1, int(last.size()) };
}

}  // namespace

struct EditorWidget::Private {
    LiveListModelBinding   *binding     = nullptr;
    QQuickWidget           *quickWidget = nullptr;
    QPointer<Session>       session;        // owned by the document
    Markoff::Theme          themeCopy;      // keeps the pointer alive for setTheme forwarding
    Markoff::EditorContext  lastContext;    // change-gate for contextChanged (spec §7)
    QMetaObject::Connection contextCon;    // cursorChanged → recomputeContext
    QMetaObject::Connection dataCon;       // model dataChanged → recomputeContext (spec §7)
    QMetaObject::Connection scrollCon;     // contentYChanged → scrollPositionChanged (spec §9)
    QMetaObject::Connection heightCon;     // contentHeightChanged → apply pending scroll
    QMetaObject::Connection docDestroyedCon; // document destroyed() → null base m_document

    // Attach-window contract (2026-06-10): a scroll fraction written while
    // the QML scene has no scrollable content yet (contentHeight == 0 right
    // after setDocument). Applied — and cleared — on contentHeightChanged.
    // This is a pending WRITE, not a second scroll authority (INVARIANTS
    // #3): once applied, the QML contentY is the only store, exactly as
    // before. Cleared on setDocument.
    std::optional<float>    pendingScrollFrac;
};

EditorWidget::EditorWidget(LiveListModelBinding::Capabilities caps,
                           QWidget *parent)
    : Markoff::MarkdownView(parent), d(std::make_unique<Private>())
{
    d->binding     = new LiveListModelBinding(caps, this);
    d->quickWidget = new QQuickWidget(this);
    d->quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    d->quickWidget->rootContext()->setContextProperty(
        QStringLiteral("modelBinding"), d->binding);

    // Contract §9: emit scrollPositionChanged on native QML scroll (user flick,
    // programmatic contentY set). contentYChanged is a NOTIFY signal on the QML
    // ListView (Flickable). We connect it via the runtime SIGNAL() string form
    // (precedent: tst_live_render_qml_integration.cpp:1260 uses the same pattern
    // to connect cursorChanged from a QObject * without a compile-time type).
    //
    // The statusChanged connection MUST be made BEFORE setSource so that the
    // statusChanged(Ready) event is caught even when QML loads synchronously
    // from a pre-compiled qrc resource. The lambda also checks the current
    // status immediately after the connection as a belt-and-braces fallback
    // for the synchronous-load case.
    //
    // The manual emit has been removed from setScrollPositionVisualLine: calling
    // root->setProperty("contentY", ...) fires contentYChanged, which reaches
    // onContentYChanged, which emits — single emit path. When contentHeight <=
    // viewport height (scrollable==0), the setter falls back to an explicit emit.
    auto wireScrollSignal = [this] {
        if (d->scrollCon) {
            QObject::disconnect(d->scrollCon);
            d->scrollCon = {};
        }
        if (d->heightCon) {
            QObject::disconnect(d->heightCon);
            d->heightCon = {};
        }
        auto *root = d->quickWidget->rootObject();
        if (!root) return;
        d->scrollCon = QObject::connect(
            root, SIGNAL(contentYChanged()),
            this, SLOT(onContentYChanged()));
        if (!d->scrollCon)
            qWarning("Markoff::Live::EditorWidget: failed to connect QML contentYChanged; "
                     "native scroll signal will not fire (QML contract changed?)");
        // Attach-window contract: contentHeightChanged drives the deferred
        // apply of a scroll fraction written before the scene materialized.
        d->heightCon = QObject::connect(
            root, SIGNAL(contentHeightChanged()),
            this, SLOT(onContentHeightChanged()));
        if (!d->heightCon)
            qWarning("Markoff::Live::EditorWidget: failed to connect QML contentHeightChanged; "
                     "attach-window scroll writes will not apply (QML contract changed?)");
    };
    connect(d->quickWidget, &QQuickWidget::statusChanged,
            this, [wireScrollSignal](QQuickWidget::Status status) {
                if (status == QQuickWidget::Ready) wireScrollSignal();
            });

    // setSource is called AFTER the statusChanged connection above so that
    // a synchronous QML load (compiled qrc resource) does not miss the signal.
    d->quickWidget->setSource(QUrl(QStringLiteral(
        "qrc:/qt/qml/org/markoff/live/qml/EditorContent.qml")));

    // Belt-and-braces: if statusChanged already fired synchronously during
    // setSource (possible with pre-compiled resources), wire the connection now.
    if (d->quickWidget->status() == QQuickWidget::Ready)
        wireScrollSignal();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(d->quickWidget);

    // Contract §4.1: surface every canonical cursor move as the base
    // signal, mapped to the flat-line coordinate model. LiveCursorState
    // is created once in the binding's constructor (CONSTANT Q_PROPERTY)
    // and outlives every setDocument, so this single constructor-time
    // connect is inherently duplicate-free across document swaps.
    connect(d->binding->cursorState(), &LiveCursorState::cursorChanged,
            this, [this] {
                const auto p = cursorPosition();
                Q_EMIT cursorPositionChanged(p.line, p.column);
            });

    // Contract §7: EditorContext feed — see setDocument() for the wiring.
    // The contextCon connection is made in setDocument() (not here) so that the
    // initial cursor seed from QML's onCountChanged (which fires requestTextCaretAtRow(0,0)
    // immediately after model population) does NOT pre-warm m_lastContext before the
    // consumer's first explicit cursor move. This mirrors the discipline in source/styled,
    // which also connect their cursor-change handlers inside setDocument() (after the
    // binding settles) rather than in the constructor. LiveCursorState is CONSTANT on
    // the binding, so each setDocument() call reconnects the same signal object cleanly.
}

EditorWidget::~EditorWidget()
{
    // Session owned by document — when EditorWidget outlives its document,
    // session is already null via QPointer. When document outlives the
    // widget (typical), explicitly destroy the session we created so the
    // document doesn't accumulate ghost sessions across leaf swaps.
    if (d->session && document()) {
        document()->destroySession(d->session);
    }
}

void EditorWidget::setDocument(Markoff::MarkoffDocument *doc)
{
    if (document() == doc) return;

    // Tear down old session.
    if (d->session && document()) {
        document()->destroySession(d->session);
    }
    d->session = nullptr;

    // Drop any prior document's destroyed() watch (the new/old documents are
    // distinct here; the old one may still be alive on a swap).
    QObject::disconnect(d->docDestroyedCon);
    d->docDestroyedCon = {};

    // Disconnect the stale context connections from the previous document phase.
    // Unlike source/styled where the cursor object changes with the doc, LiveCursorState
    // is CONSTANT on the binding (never changes across setDocument calls). We still
    // disconnect+reconnect so the sentinel reset below takes effect before any new
    // cursor moves arrive — otherwise a queued cursorChanged from the previous doc
    // could fire recomputeContext before the sentinel is in place.
    if (d->contextCon) {
        QObject::disconnect(d->contextCon);
        d->contextCon = {};
    }
    if (d->dataCon) {
        QObject::disconnect(d->dataCon);
        d->dataCon = {};
    }

    // Reset the context sentinel so the first cursor movement after setDocument()
    // always emits contextChanged (spec §7). Use an empty blockKind string as
    // sentinel (not a valid BlockKindNames value), matching the source/styled pattern.
    // NOTE: we reset BEFORE re-wiring the context connection to ensure the
    // QML onCountChanged → requestTextCaretAtRow(0,0) initial seed (which fires
    // cursorChanged synchronously after model population) does NOT pre-warm
    // m_lastContext before the consumer's first explicit cursor move. The
    // contextCon is (re)wired after flushPendingD2Changed so the sentinel is
    // already in place when the binding's initial model population fires.
    d->lastContext = Markoff::EditorContext{};
    d->lastContext.blockKind = QString{};  // sentinel: not a valid kind name

    // Attach-window contract: a scroll write latched against the previous
    // document must not leak into the new one.
    d->pendingScrollFrac.reset();

    Markoff::MarkdownView::setDocument(doc);
    d->binding->setDocument(doc);

    if (doc) {
        // Retire-on-destroy (INVARIANTS #3): the base MarkdownView holds the
        // document by raw pointer (m_document). If the document is destroyed
        // while still attached, null the base pointer so base accessors
        // (cursorPosition/undo/redo/…) don't dereference freed memory. The
        // qualified base call avoids virtual re-entry into our own
        // setDocument() (which would touch the dying document's session). The
        // binding detaches itself via its own destroyed() connection.
        d->docDestroyedCon =
            QObject::connect(doc, &QObject::destroyed, this, [this] {
                Markoff::MarkdownView::setDocument(nullptr);
            });

        d->session = doc->createSession();
        d->binding->setSession(d->session);
        // Force initial model population: setDocument only connects to
        // documentLoaded/d2DocumentChanged signals, but loadFromMarkdown
        // typically ran BEFORE this widget was constructed (the host
        // populates the document then hands it over). Without this nudge
        // the LiveBlockModel stays empty until the next user edit.
        doc->flushPendingD2Changed();
    }

    // Wire the context feed AFTER flushPendingD2Changed so the initial model
    // population (and the QML onCountChanged → requestTextCaretAtRow(0,0) seed)
    // has already fired. Any cursorChanged that fires from here on is a real
    // consumer-driven cursor move.
    //
    // Spec §7: context is driven off BOTH:
    //   (a) LiveCursorState::cursorChanged — every explicit cursor move.
    //   (b) LiveBlockModel::dataChanged — kind-transition Cmd::changeKind emits
    //       this on the changed block row; a kind change that does NOT move the
    //       caret (e.g. prefix-rule inference during onD2Changed) would otherwise
    //       leave the context stale. The change-gate (m_lastContext comparison in
    //       recomputeContext()) absorbs spurious no-op fires cheaply — that is
    //       precisely its job — so wiring dataChanged is safe.
    //
    // Wire unconditionally (even when doc == nullptr) so that a null document
    // transition doesn't leave connections in an intermediate state — the
    // recomputeContext() null-doc guard handles the no-doc case cheaply.
    d->contextCon = connect(d->binding->cursorState(),
                            &LiveCursorState::cursorChanged,
                            this, &EditorWidget::recomputeContext);
    // Wire dataChanged on the model so kind transitions that do not move the
    // caret still trigger a context refresh (spec §7).
    //
    // Spec §7 calls for recompute on kind-transition dataChanged; we connect
    // ALL dataChanged because the model does not expose a kind-only signal.
    // This is safe and cheap: (a) the recomputeContext() change-gate suppresses
    // no-op fires via the m_lastContext comparison, and (b) live model dataChanged
    // fires per debounced d2DocumentChanged cycle (one per event-loop iteration,
    // not per keystroke).
    d->dataCon = connect(d->binding->model(),
                         &QAbstractItemModel::dataChanged,
                         this, [this](const QModelIndex &, const QModelIndex &,
                                      const QList<int> &) {
                             recomputeContext();
                         });
}

LiveListModelBinding *EditorWidget::binding() const noexcept
{
    return d->binding;
}

QRect EditorWidget::caretRect() const
{
    // Read-only query over the focused QML text item (INVARIANTS #3: no
    // second cursor store). Whenever a TextCaret / cell edit is live, the
    // window's activeFocusItem IS the focused TextEdit, for every
    // text-bearing delegate kind — no per-delegate QML changes needed.
    if (!document() || !d->quickWidget) return {};
    QQuickWindow *win = d->quickWidget->quickWindow();
    if (!win) return {};
    QQuickItem *focus = win->activeFocusItem();
    if (!focus) return {};
    const QVariant cr = focus->property("cursorRectangle");
    if (!cr.isValid() || !cr.canConvert<QRectF>()) return {};   // not a text item
    const QRectF sceneRect = focus->mapRectToScene(cr.toRectF());
    // Scene coords == QQuickWidget-local coords; translate into this widget.
    const QPoint origin = d->quickWidget->mapTo(const_cast<EditorWidget *>(this), QPoint(0, 0));
    return sceneRect.translated(origin.x(), origin.y()).toRect();
}

Markoff::CursorPos EditorWidget::cursorPosition() const
{
    auto *doc = document();
    auto *cs  = d->binding ? d->binding->cursorState() : nullptr;
    if (!doc || !cs) return {1, 1};

    // Read the canonical cursor (L3: LiveCursorState is authoritative;
    // 2026-05-22-cursor-authority-decision.md). Non-TextCaret variants
    // report the block's first line, column 1 (spec §4.1).
    Markoff::BlockAnchor block;
    int qtPos = 0;
    if (const auto caret = cs->currentTextCaret()) {
        block = caret->block;
        qtPos = int(caret->cachedQtPos);
    } else {
        const auto cur = cs->cursor();
        if (const auto *bs = std::get_if<Markoff::BlockSelected>(&cur)) {
            block = bs->block;
        } else if (const auto *bi =
                       std::get_if<Markoff::BlockInternalEdit>(&cur)) {
            block = bi->block;
        } else {
            return {1, 1};
        }
    }

    // Row of the cursor's block via an index scan — same id-equality
    // pattern as SourceFindAdapter. O(blocks); deliberately uncached.
    int row = -1;
    const auto ids = doc->iterateBlocks();
    for (int i = 0; i < int(ids.size()); ++i) {
        if (ids[std::size_t(i)] == block) { row = i; break; }
    }
    if (row < 0) return {1, 1};
    return toCursorPos(doc, row, qtPos);
}

// ---- EditorContext feed (spec §7) ----------------------------------------
//
// Reads the canonical cursor from LiveCursorState (the L3 authority; no new
// cursor store is introduced — m_lastContext is a context cache only, not a
// cursor cache). Maps the caret's block row → BlockId → blockKind, builds an
// EditorContext, and emits contextChanged iff the context changed (change-gate).
//
// Called from TWO sources (spec §7):
//   1. LiveCursorState::cursorChanged — every cursor move.
//   2. LiveBlockModel::dataChanged — kind-transition Cmd::changeKind may change
//      a block's kind without moving the caret (prefix-rule inference during
//      onD2Changed). The change-gate absorbs the resulting no-op fires cheaply.
//
// inTable is set when the block kind is Table. tableRow/tableCol remain -1
// (the EditorContext default sentinel): per-cell coordinates would require
// surfacing TableEditBinding's cell focus up through the EditorWidget boundary,
// but TableEditBinding is a per-QML-delegate object with no stable C++ owner
// at the EditorWidget level — there is no single authoritative C++ object that
// holds the "active cell row/col" without live QML delegate interaction.
// The contract minimum (inTable == true, row/col = -1) is satisfied per the
// plan's Step 2 contract-minimum allowance. The test asserts only the minimum.
void EditorWidget::recomputeContext()
{
    auto *doc = document();
    auto *cs  = d->binding ? d->binding->cursorState() : nullptr;
    if (!doc || !cs) return;

    // Read the canonical cursor (L3: LiveCursorState is authoritative).
    // Non-TextCaret and NoCursor states → no meaningful context update.
    Markoff::BlockAnchor block;
    if (const auto caret = cs->currentTextCaret()) {
        block = caret->block;
    } else {
        const auto cur = cs->cursor();
        if (const auto *bs = std::get_if<Markoff::BlockSelected>(&cur))
            block = bs->block;
        else if (const auto *bi = std::get_if<Markoff::BlockInternalEdit>(&cur))
            block = bi->block;
        else
            return;  // NoCursor — no block to report
    }

    // Resolve BlockAnchor → block id via the document's block list (same
    // id-equality pattern as cursorPosition(); O(blocks), uncached).
    const auto ids = doc->iterateBlocks();
    Markoff::BlockAnchor blockId;
    bool found = false;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == block) { blockId = ids[i]; found = true; break; }
    }
    if (!found) return;

    const Markoff::BlockKind kind = doc->blockKind(blockId);
    Markoff::EditorContext ctx;
    using BK = Markoff::BlockKind;
    namespace BKN = Markoff::BlockKindNames;
    switch (kind) {
    case BK::Paragraph:      ctx.blockKind = BKN::Paragraph;      break;
    case BK::Heading:        ctx.blockKind = BKN::Heading;         break;
    case BK::CodeBlock:      ctx.blockKind = BKN::CodeBlock;       break;
    case BK::ListItem:       ctx.blockKind = BKN::ListItem;        break;
    case BK::BlockQuote:     ctx.blockKind = BKN::Blockquote;      break;
    case BK::HorizontalRule: ctx.blockKind = BKN::HorizontalRule;  break;
    case BK::Image:          ctx.blockKind = BKN::Image;           break;
    case BK::Math:           ctx.blockKind = BKN::Math;            break;
    case BK::Table:          ctx.blockKind = BKN::Table;           break;
    default:                 ctx.blockKind = BKN::Paragraph;       break; // fallback
    }
    ctx.inTable = (kind == BK::Table);
    // tableRow/tableCol remain -1 (EditorContext defaults, contract minimum).
    // GAP 3 investigation: TableEditBinding is a per-QML-delegate object
    // instantiated by TableDelegate.qml at runtime; there is no stable C++
    // owner at the EditorWidget level that holds the currently-focused cell's
    // row/col without live QML delegate interaction. Populating these fields
    // would require either (a) a C++-owned "active table cell" singleton that
    // the delegate updates via an invokable (a second cursor authority —
    // INVARIANTS §3), or (b) reading a QML property on the active delegate
    // object, which is not reachable from EditorWidget without QML coupling.
    // Plan Step 2 documents this as the contract minimum; the live contract
    // test asserts only inTable == true / blockKind == "table".

    // Heading level from the "level" attr (int 1–6).
    if (kind == BK::Heading) {
        const auto attrs = doc->blockAttrs(blockId);
        auto it = attrs.constFind(Markoff::AttrNames::Level);
        if (it != attrs.cend()) {
            if (const int *p = std::get_if<int>(&it.value()))
                ctx.headingLevel = *p;
        }
    }

    // Change-gate: only emit if something actually changed.
    if (ctx == d->lastContext) return;
    d->lastContext = ctx;
    emit contextChanged(d->lastContext);
}

// ---- Scroll position (spec §9) -------------------------------------------
//
// The live view is a QML ListView (a Flickable). Its scroll position is the
// contentY / (contentHeight - height) ratio. We access it via the QQuickWidget
// root object's QML properties — the same pattern the integration test harness
// uses (tst_live_render_qml_integration: lv->property("contentY")).
//
// scrollPositionChanged is emitted via onContentYChanged(), which is connected
// to the QML ListView's contentYChanged NOTIFY signal (set up in the constructor
// via QQuickWidget::statusChanged → QObject::connect(..., SIGNAL(contentYChanged()),
// ..., SLOT(onContentYChanged()))). This covers BOTH user-driven flick gestures
// and programmatic contentY writes (setScrollPositionVisualLine → root->setProperty).
//
// The manual emit was removed from setScrollPositionVisualLine; it only adds a
// fallback emit for the zero-scrollable-range case (when contentHeight ≤ viewport
// height, setProperty is a clamped no-op and contentYChanged will not fire).

void EditorWidget::onContentYChanged()
{
    Q_EMIT scrollPositionChanged(scrollPositionVisualLine());
}

float EditorWidget::scrollPositionVisualLine() const
{
    // Attach-window contract: while a write is latched (scene not yet
    // materialized), read back the latched value so a capture immediately
    // after a restore round-trips. Cleared on apply — afterwards the QML
    // contentY is the only store, as before.
    if (d->pendingScrollFrac) return *d->pendingScrollFrac;
    auto *root = d->quickWidget ? d->quickWidget->rootObject() : nullptr;
    if (!root) return 0.0f;
    bool ok = false;
    const qreal contentY = root->property("contentY").toReal(&ok);
    if (!ok) return 0.0f;
    const qreal contentH = root->property("contentHeight").toReal(&ok);
    if (!ok || contentH <= 0.0) return 0.0f;
    const qreal height = root->property("height").toReal();
    const qreal scrollable = contentH - height;
    if (scrollable <= 0.0) return 0.0f;
    return static_cast<float>(contentY / scrollable);
}

void EditorWidget::setScrollPositionVisualLine(float pos)
{
    auto *root = d->quickWidget ? d->quickWidget->rootObject() : nullptr;
    // Clamp pos to [0, 1].
    const float clamped = qBound(0.0f, pos, 1.0f);
    if (root) {
        bool ok = false;
        const qreal contentH = root->property("contentHeight").toReal(&ok);
        const qreal height   = root->property("height").toReal();
        const qreal scrollable = (ok && contentH > height) ? (contentH - height) : 0.0;
        if (scrollable > 0.0) {
            d->pendingScrollFrac.reset();
            // setProperty fires contentYChanged → onContentYChanged → emit
            // scrollPositionChanged — single emit path (spec §9).
            root->setProperty("contentY",
                              QVariant::fromValue(static_cast<qreal>(clamped) * scrollable));
            return;
        }
        if (ok && contentH > 0.0) {
            // The scene is materialized and the content genuinely fits the
            // viewport — the fraction is moot. setProperty would be a
            // clamped no-op and contentYChanged would not fire; emit
            // explicitly so the caller always observes the set (spec §9
            // contract minimum).
            d->pendingScrollFrac.reset();
            Q_EMIT scrollPositionChanged(0.0f);
            return;
        }
    }
    // Attach-window contract: no root yet, or the QML scene has not
    // materialized the content (contentHeight still 0 — e.g. this write
    // arrived in the same call stack as setDocument, the adoption brief's
    // restore recipe). Latch and apply on contentHeightChanged — a one-shot
    // write here must not be silently lost. See
    // tst_view_contract_live_attach_window.
    d->pendingScrollFrac = clamped;
    Q_EMIT scrollPositionChanged(clamped);
}

void EditorWidget::onContentHeightChanged()
{
    if (!d->pendingScrollFrac) return;
    // Re-dispatch through the setter: with scrollable content it applies
    // and clears the latch; if the materialized content turns out to fit
    // the viewport it clears the latch via the fits-viewport branch; if
    // the scene is still not ready it re-latches the same value (no-op).
    setScrollPositionVisualLine(*d->pendingScrollFrac);
}

void EditorWidget::setReadOnly(bool ro)
{
    // Base stores (isReadOnly()/hasEditing()/undo()/redo() read it); the
    // binding's flag is the single authority the live leaf's mutation-
    // ingress gates consult (spec §4.2). The flag is binding state, not
    // per-document state, so it survives setDocument unchanged.
    Markoff::MarkdownView::setReadOnly(ro);
    d->binding->setReadOnly(ro);
}

void EditorWidget::setTheme(const Markoff::Theme &t)
{
    // Base stores + emits themeChanged() (spec §4.3).
    Markoff::MarkdownView::setTheme(t);
    // Keep a widget-owned copy; the binding takes a const pointer, so the
    // pointed-to object must outlive the next setTheme call. The binding
    // rotates two internal copy-buffers — no pointer-equality short-circuit
    // — so every call reaches QML and emits themeChanged on the binding.
    d->themeCopy = t;
    if (d->binding)
        d->binding->setTheme(&d->themeCopy);
}

void EditorWidget::setFontScale(qreal s)
{
    // Base clamps, stores, and emits fontScaleChanged (spec §4.3).
    // Forward the base's clamped canonical value, not the raw s.
    Markoff::MarkdownView::setFontScale(s);
    if (d->binding)
        d->binding->setFontScale(fontScale());
}

// --- Format verbs (spec §4.4) ---
// Each verb delegates to the binding's actionController QAction.
// QAction::trigger() is a no-op when the action is disabled, so
// read-only gating and selection guards ride along automatically.

void EditorWidget::toggleBold()
{
    if (auto *ac = d->binding ? d->binding->actionController() : nullptr)
        ac->boldAction()->trigger();
}

void EditorWidget::toggleItalic()
{
    if (auto *ac = d->binding ? d->binding->actionController() : nullptr)
        ac->italicAction()->trigger();
}

void EditorWidget::toggleStrikethrough()
{
    if (auto *ac = d->binding ? d->binding->actionController() : nullptr)
        ac->strikeAction()->trigger();
}

void EditorWidget::toggleInlineCode()
{
    if (auto *ac = d->binding ? d->binding->actionController() : nullptr)
        ac->inlineCodeAction()->trigger();
}

void EditorWidget::insertLink()
{
    if (auto *ac = d->binding ? d->binding->actionController() : nullptr)
        ac->linkAction()->trigger();
}

void EditorWidget::setHeadingLevel(int level)
{
    // Guard: 0..6 are the only valid levels (0 = paragraph, 1..6 = ATX heading).
    if (level < 0 || level > 6) return;
    if (auto *ac = d->binding ? d->binding->actionController() : nullptr) {
        switch (level) {
        case 0: ac->heading0Action()->trigger(); break;
        case 1: ac->heading1Action()->trigger(); break;
        case 2: ac->heading2Action()->trigger(); break;
        case 3: ac->heading3Action()->trigger(); break;
        case 4: ac->heading4Action()->trigger(); break;
        case 5: ac->heading5Action()->trigger(); break;
        case 6: ac->heading6Action()->trigger(); break;
        default: break;
        }
    }
}

void EditorWidget::setCursorPosition(Markoff::CursorPos pos)
{
    auto *doc = document();
    auto *cs  = d->binding ? d->binding->cursorState() : nullptr;
    if (!doc || !cs) return;
    // Attach-window contract: an explicit consumer caret placement IS the
    // initial caret — LiveView.qml's initial-focus seed (which fires one
    // frame after model population and would otherwise clobber this
    // request's pending focus with row 0) checks this flag and yields.
    d->binding->markInitialCaretRequested();
    const auto [row, qtPos] = fromCursorPos(doc, pos);
    // Chokepoint write (L3) — no widget-side cursor mutation.
    cs->requestTextCaretAtRow(row, qtPos);
}

void EditorWidget::attachFindController(Markoff::FindController *fc)
{
    d->binding->attachFindController(fc);
}

void EditorWidget::detachFindController()
{
    d->binding->detachFindController();
}

}  // namespace Markoff::Live
