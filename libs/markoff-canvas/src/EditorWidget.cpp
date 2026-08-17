// SPDX-License-Identifier: GPL-3.0-or-later
// (c) 2026 Markoff contributors, GPL-3.0-or-later.
#include <markoff/canvas/EditorWidget.h>

#include <QJsonArray>
#include <QJsonValue>
#include <QScrollBar>
#include <QVBoxLayout>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/TextUnits.h>

#include <markoff/canvas/CanvasActionController.h>
#include <markoff/canvas/View.h>

namespace coords = Markoff::TextUnits;

namespace Markoff::Canvas {

namespace {

// Flat-line model (matches live/source/styled — see contract-v2 spec §3):
// each document block contributes 1 + (internal '\n' count) visual flat
// lines; blocks are separated by exactly one line boundary; `column` is
// the 1-based UTF-16 position within the line. Canvas's own coordinate
// space is per-block UTF-8 bytes (C4), so both directions round-trip
// through `Markoff::TextUnits::byteToQtPos`/`qtPosToByte` against the
// block's own `blockText()` — never a layout string (P1.2 note: the
// layout substitutes U+2028 for '\n', which would throw the QChar count
// off by one per preceding newline).

Markoff::CursorPos toCursorPos(const Markoff::MarkoffDocument *doc,
                                BlockId caretBlock, int caretByteOffset)
{
    int line = 1;
    for (const BlockId id : doc->iterateBlocks()) {
        const QByteArray bytes = doc->blockText(id);
        if (id == caretBlock) {
            const qsizetype byteOff = qBound(qsizetype(0), qsizetype(caretByteOffset), bytes.size());
            const qsizetype qtPos = coords::byteToQtPos(bytes, byteOff);
            const QString text = QString::fromUtf8(bytes);
            const QStringView before = QStringView(text).left(qtPos);
            const int innerLine = int(before.count(QLatin1Char('\n')));
            const qsizetype lastNl = before.lastIndexOf(QLatin1Char('\n'));
            const qsizetype lineStart = (lastNl < 0) ? 0 : lastNl + 1;
            return { line + innerLine, int(qtPos - lineStart) + 1 };
        }
        line += 1 + int(QString::fromUtf8(bytes).count(QLatin1Char('\n')));
    }
    return {1, 1};
}

// Inverse: flat (line, column) -> (BlockId, block-relative byte offset).
// Clamps — never a no-op: a past-the-end line lands at the end of the last
// block; an over-long column lands at the line's end. Empty document
// returns a null BlockId / byte 0 (View::setCaretPosition's empty-cache
// guard handles that).
std::pair<BlockId, int> fromCursorPos(const Markoff::MarkoffDocument *doc,
                                       Markoff::CursorPos p)
{
    const auto ids = doc->iterateBlocks();
    if (ids.empty())
        return { BlockId{}, 0 };

    int line = 1;
    for (const BlockId id : ids) {
        const QByteArray bytes = doc->blockText(id);
        const QString text = QString::fromUtf8(bytes);
        const int span = 1 + int(text.count(QLatin1Char('\n')));
        if (p.line < line + span) {
            qsizetype qtPos = 0;
            for (int i = 0; i < p.line - line; ++i)
                qtPos = text.indexOf(QLatin1Char('\n'), qtPos) + 1;
            const qsizetype nl = text.indexOf(QLatin1Char('\n'), qtPos);
            const qsizetype lineEnd = (nl < 0) ? text.size() : nl;
            qtPos = qMin(qtPos + qMax(0, p.column - 1), lineEnd);
            return { id, int(coords::qtPosToByte(bytes, qtPos)) };
        }
        line += span;
    }
    return { ids.back(), int(doc->blockText(ids.back()).size()) };
}

}  // namespace

EditorWidget::EditorWidget(QWidget *parent)
    : Markoff::MarkdownView(parent)
{
    m_view = new View(this);
    m_actionController = new CanvasActionController(this);
    m_actionController->setView(m_view);
    // Context menu's format section (P4.4) is built from these same
    // QActions — no second format-verb wiring.
    m_view->setActionController(m_actionController);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view);

    // cursorPositionChanged (P3.2): View::caretChanged is the composed
    // view's unconditional caret-moved signal (real events AND
    // document-driven clamps, see View.h); onViewCaretChanged() does the
    // CursorPos-space change-gating the base contract requires.
    QObject::connect(m_view, &View::caretChanged, this, &EditorWidget::onViewCaretChanged);

    // scrollPositionChanged (P3.2): the composed View is a
    // QAbstractScrollArea — its vertical scrollbar's valueChanged already
    // fires on both user-driven scrolling (wheel, drag) and the
    // programmatic path in setScrollPositionVisualLine (via setValue), so
    // one connection covers the whole contract (mirrors
    // Source::Editor/Styled::Editor's single-emit-path pattern).
    QObject::connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this,
                      [this](int) { emit scrollPositionChanged(scrollPositionVisualLine()); });

    // Inline title (P4.9): a bare forward, not a second signal-emission
    // path — View::titleEdited already carries the finished (post-edit)
    // string.
    QObject::connect(m_view, &View::titleEdited, this, &EditorWidget::titleEdited);

    // File drop (P7.2): same bare-forward shape as titleEdited above —
    // View::fileDropped already carries the finished payload (urls +
    // viewport position); this wrapper is the MarkdownView contract
    // surface Corbomite actually binds against, so the signal has to live
    // here, not just on the composed View.
    QObject::connect(m_view, &View::fileDropped, this, &EditorWidget::fileDropped);

    // Ctrl+Scroll zoom ([cluster-k] P3): View has no `fontScale` authority
    // of its own (see `View::fontScaleStepRequested`'s doc comment) — it
    // just asks. This applies the exact same `fontScale() * kZoomStep`
    // step Corbomite's MainWindow View-menu Zoom In/Out actions use
    // (kZoomStep = 1.10 there), through `setFontScale()` so the base
    // MarkdownView's clamp-to-[0.25,4.0] + `fontScaleChanged` stay
    // authoritative — Ctrl+Scroll, the menu, and any future keyboard
    // shortcut all land in the same state instead of drifting apart.
    QObject::connect(m_view, &View::fontScaleStepRequested, this,
                      [this](int steps) {
                          constexpr qreal kZoomStep = 1.10;
                          setFontScale(steps > 0 ? fontScale() * kZoomStep
                                                  : fontScale() / kZoomStep);
                      });
}

EditorWidget::~EditorWidget()
{
    // Session owned by the document (live's pattern, spec §4.1): when the
    // document outlives this widget (the typical case), destroy the
    // session we created so it doesn't accumulate a ghost session across
    // leaf swaps. When the document was destroyed first, m_docDestroyedCon
    // already nulled the base's document pointer and m_session — nothing
    // to tear down here.
    if (m_session && document())
        document()->destroySession(m_session);
    // No m_view->setSession(nullptr) needed here: m_view is a child widget
    // of this EditorWidget and is being torn down in the same destructor
    // sequence.
}

void EditorWidget::setDocument(Markoff::MarkoffDocument *doc)
{
    if (document() == doc)
        return;

    if (m_session && document())
        document()->destroySession(m_session);
    m_session = nullptr;
    // P6.0: the View's fold-state cache is resolved from the Session — a
    // dying/absent session must not leave it pointing at one.
    m_view->setSession(nullptr);

    QObject::disconnect(m_docDestroyedCon);
    m_docDestroyedCon = {};
    QObject::disconnect(m_contextD2Con);
    m_contextD2Con = {};
    // Reset the context sentinel BEFORE touching the composed View:
    // View::setDocument's internal reset synchronously fires caretChanged
    // (C2 — no queued step anywhere in this leaf), which reaches
    // onViewCaretChanged()->recomputeContext() before this function
    // returns. The sentinel must already be in place for that first
    // recompute, or it would diff against the OLD document's context
    // instead of unconditionally emitting for the new one (mirrors
    // live/source's identical ordering note).
    m_lastContext = Markoff::EditorContext{};
    m_lastContext.blockKind = QString{};  // sentinel: not a valid kind name

    // Base store + documentChanged, then the composed View: View::setDocument
    // resets its caret to {} and re-realizes from the new document
    // synchronously (no queued/deferred step exists anywhere in this leaf,
    // C2) — by the time this function returns, that reset has already
    // happened and nothing further will touch the caret on its own. A
    // caller's setCursorPosition() issued right after this call therefore
    // sticks; see the attach-window note on the class doc.
    Markoff::MarkdownView::setDocument(doc);
    m_view->setDocument(doc);
    m_actionController->setDocument(doc);

    if (doc) {
        m_docDestroyedCon = QObject::connect(doc, &QObject::destroyed, this, [this] {
            // Retire-on-destroy (INVARIANTS #3): don't dereference a freed
            // document from any base accessor. Qualified call avoids
            // re-entering our own setDocument (which would touch the dying
            // document's session).
            m_session = nullptr;
            m_view->setSession(nullptr);
            Markoff::MarkdownView::setDocument(nullptr);
        });
        m_session = doc->createSession();
        m_view->setSession(m_session);

        // Queue #15 (spec §7): also recompute on a genuine structural
        // change (block Insert/Remove/ChangeKind — e.g. a programmatic
        // Cmd::changeKind that doesn't move the caret) so contextChanged
        // doesn't go stale until the next caret move. Filtered on
        // structuralEditSequence rather than the raw signal so
        // content-only/format-only passes stay silent (same pattern
        // markoff-source/markoff-live use).
        m_lastStructuralSeq = doc->structuralEditSequence();
        m_contextD2Con = QObject::connect(
            doc, &Markoff::MarkoffDocument::d2DocumentChanged, this, [this, doc]() {
                const quint64 seq = doc->structuralEditSequence();
                if (seq == m_lastStructuralSeq) return;
                m_lastStructuralSeq = seq;
                recomputeContext();
            });
    }
}

Markoff::CursorPos EditorWidget::cursorPosition() const
{
    auto *doc = document();
    if (!doc || !m_view)
        return {1, 1};
    return toCursorPos(doc, m_view->caretBlock(), m_view->caretByteOffset());
}

void EditorWidget::setCursorPosition(Markoff::CursorPos pos)
{
    auto *doc = document();
    if (!doc || !m_view)
        return;
    const auto [block, byteOffset] = fromCursorPos(doc, pos);
    m_view->setCaretPosition(block, byteOffset);
}

void EditorWidget::onViewCaretChanged()
{
    const Markoff::CursorPos p = cursorPosition();
    if (p.line != m_lastCursorPos.line || p.column != m_lastCursorPos.column) {
        m_lastCursorPos = p;
        Q_EMIT cursorPositionChanged(p.line, p.column);
    }
    // contextChanged is gated independently (on EditorContext, not
    // CursorPos) — View::caretChanged is unconditional (P3.2), so this
    // runs on every caret-changing code path; recomputeContext()'s own
    // change-gate absorbs a caret move that stays within the same block
    // (spec §7: "no re-emit when the caret stays in the same block").
    recomputeContext();
}

void EditorWidget::recomputeContext()
{
    auto *doc = document();
    if (!doc || !m_view) return;
    // setDocument()'s internal caret reset fires View::caretChanged
    // synchronously (the same P3.2 chokepoint every real caret move uses,
    // C2 — no queued step to hide behind), reaching this function via
    // onViewCaretChanged() before setDocument() has finished wiring
    // m_contextD2Con. Skip that call rather than let it consume the
    // fresh-document sentinel early: the contract (mirrored from
    // live/source) is "the FIRST cursor movement AFTER setDocument()
    // always emits", not "attaching a document counts as a movement".
    // m_contextD2Con is only ever valid once setDocument() has finished
    // wiring the current document — using it as the readiness check avoids
    // a second, purpose-built flag for the same fact.
    if (!m_contextD2Con) return;

    const Markoff::BlockId block = m_view->caretBlock();
    if (block.isNull()) return;

    const Markoff::BlockKind kind = doc->blockKind(block);
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
    default:                 ctx.blockKind = BKN::Paragraph;       break;  // Mermaid, HtmlBlock
    }
    ctx.inTable = (kind == BK::Table);

    // Heading level from the "level" attr (int 1-6) — same attr P1.1's
    // promoteCaretBlockKind()/updateCaretHeadingLevel() write.
    if (kind == BK::Heading) {
        const auto attrs = doc->blockAttrs(block);
        const auto it = attrs.constFind(Markoff::AttrNames::Level);
        if (it != attrs.cend()) {
            if (const int *p = std::get_if<int>(&it.value()))
                ctx.headingLevel = *p;
        }
    }

    // Table row/col (P2.3's per-cell row-major sequence, via
    // View::caretTableCell()) — unlike the live leaf (no stable C++ owner
    // for a QML delegate's focused cell), canvas's View owns table layout
    // directly, so row/col are cheaply derivable rather than a contract
    // minimum (-1, -1).
    if (kind == BK::Table) {
        if (const auto rc = m_view->caretTableCell()) {
            ctx.tableRow = rc->first;
            ctx.tableCol = rc->second;
        }
    }

    // Change-gate: only emit if something actually changed.
    if (ctx == m_lastContext) return;
    m_lastContext = ctx;
    Q_EMIT contextChanged(m_lastContext);
}

void EditorWidget::setTheme(const Markoff::Theme &t)
{
    Markoff::MarkdownView::setTheme(t);  // base stores + emits themeChanged
    if (m_view)
        m_view->setTheme(theme());
}

void EditorWidget::setFontScale(qreal s)
{
    // Base clamps [0.25, 4.0], stores, and emits fontScaleChanged (no-op
    // if the clamped value is unchanged). Read fontScale() back rather
    // than forwarding `s` directly so View gets the value actually now in
    // effect. View::setFontScale does the full relayout +
    // top-visible-block scroll re-anchor (its own doc comment) and is
    // itself a no-op if unchanged, so this is safe to call unconditionally.
    Markoff::MarkdownView::setFontScale(s);
    if (m_view)
        m_view->setFontScale(fontScale());
}

float EditorWidget::scrollPositionVisualLine() const
{
    if (!m_view)
        return 0.0f;
    const auto *sb = m_view->verticalScrollBar();
    if (!sb || sb->maximum() == 0)
        return 0.0f;
    return float(sb->value()) / float(sb->maximum());
}

void EditorWidget::setScrollPositionVisualLine(float pos)
{
    if (!m_view)
        return;
    auto *sb = m_view->verticalScrollBar();
    if (!sb)
        return;
    const float clamped = qBound(0.0f, pos, 1.0f);
    if (sb->maximum() != 0) {
        // setValue fires valueChanged, wired in the constructor to emit
        // scrollPositionChanged — single emit path (matches Source/Styled).
        sb->setValue(qRound(clamped * float(sb->maximum())));
        return;
    }
    // The document fits entirely in the viewport (nothing to scroll):
    // setValue would be a silent no-op and valueChanged would not fire.
    // Emit explicitly so the write is always observable (never a no-op).
    Q_EMIT scrollPositionChanged(0.0f);
}

QJsonObject EditorWidget::saveEphemeralState() const
{
    QJsonObject out;
    if (!m_view || !document())
        return out;

    const auto [scrollBlockIndex, scrollFraction] = m_view->scrollAnchor();
    QJsonObject scroll;
    scroll[QStringLiteral("blockIndex")] = scrollBlockIndex;
    scroll[QStringLiteral("fraction")] = double(scrollFraction);
    out[QStringLiteral("scroll")] = scroll;

    // F1a multi-cursor readiness: a list of one, not a scalar (base class
    // doc) — View has exactly one caret today.
    QJsonArray cursors;
    const BlockId caretBlock = m_view->caretBlock();
    if (!caretBlock.isNull()) {
        const int idx = m_view->blockIndexOf(caretBlock);
        if (idx >= 0) {
            QJsonObject cursor;
            cursor[QStringLiteral("blockIndex")] = idx;
            cursor[QStringLiteral("byte")] = m_view->caretByteOffset();
            cursors.append(cursor);
        }
    }
    out[QStringLiteral("cursors")] = cursors;

    // Folding (P5.6): document-order indices of every currently-folded
    // head — same "index survives detach/reattach, a raw BlockId doesn't"
    // reasoning as `cursors` above (`View::foldedHeadIndices`'s own doc
    // comment). One object per fold rather than a bare int array, room for
    // a future per-fold field (e.g. kind) without a schema migration —
    // same F1a-style forward-compat the `cursors` array already follows.
    QJsonArray folds;
    for (const int idx : m_view->foldedHeadIndices()) {
        QJsonObject fold;
        fold[QStringLiteral("blockIndex")] = idx;
        folds.append(fold);
    }
    out[QStringLiteral("folds")] = folds;

    return out;
}

void EditorWidget::restoreEphemeralState(const QJsonObject &state)
{
    if (!m_view || !document())
        return;

    // Cursor restore BEFORE scroll restore, deliberately: View::setCaretPosition
    // routes through ensureCaretVisible(), which auto-scrolls the caret's
    // block into view if it isn't already — if that ran AFTER the scroll
    // restore below, a caret whose saved block sits outside the saved
    // scroll viewport (a perfectly legal independent combination — this
    // schema saves the two separately) would silently override the scroll
    // restore. Doing scroll last makes the explicitly-saved scroll always
    // win, matching what a consumer restoring from a persisted blob
    // expects: the exact scroll position it asked for, not wherever the
    // cursor's own auto-scroll side effect happened to leave it.
    const QJsonValue cursorsVal = state.value(QStringLiteral("cursors"));
    if (cursorsVal.isArray()) {
        const QJsonArray cursors = cursorsVal.toArray();
        // Single-caret leaf today (F1a note, class doc): restore only the
        // array's first element, per the base contract's "cursorPosition()
        // is the primary caret" rule.
        if (!cursors.isEmpty() && cursors.first().isObject()) {
            const QJsonObject cursor = cursors.first().toObject();
            const QJsonValue idxVal = cursor.value(QStringLiteral("blockIndex"));
            const QJsonValue byteVal = cursor.value(QStringLiteral("byte"));
            if (idxVal.isDouble() && byteVal.isDouble()) {
                const BlockId block = m_view->blockIdAt(idxVal.toInt());
                // Unknown index (foreign/stale blob): skip rather than
                // guess — setCaretPosition's own "unknown block" clamp is
                // for a stale BlockId, not an out-of-range index.
                if (!block.isNull())
                    m_view->setCaretPosition(block, byteVal.toInt());
            }
        }
    }

    const QJsonValue scrollVal = state.value(QStringLiteral("scroll"));
    if (scrollVal.isObject()) {
        const QJsonObject scroll = scrollVal.toObject();
        const QJsonValue idxVal = scroll.value(QStringLiteral("blockIndex"));
        // Malformed/missing sub-key: skip just this piece, not the whole
        // restore (class doc — never fatal on a partial or foreign blob).
        if (idxVal.isDouble()) {
            const float fraction =
                float(scroll.value(QStringLiteral("fraction")).toDouble(0.0));
            m_view->setScrollAnchor(idxVal.toInt(), fraction);
        }
    }

    // Folding (P5.6): restore whatever of the saved indices still resolve
    // to a foldable block in THIS document — View::setFoldedHeadIndices
    // does the per-index validity check (class doc: never fatal on a
    // stale/foreign blob).
    const QJsonValue foldsVal = state.value(QStringLiteral("folds"));
    if (foldsVal.isArray()) {
        QList<int> indices;
        for (const QJsonValue &v : foldsVal.toArray()) {
            if (!v.isObject())
                continue;
            const QJsonValue idxVal = v.toObject().value(QStringLiteral("blockIndex"));
            if (idxVal.isDouble())
                indices << idxVal.toInt();
        }
        m_view->setFoldedHeadIndices(indices);
    }
}

View *EditorWidget::view() const noexcept
{
    return m_view;
}

CanvasActionController *EditorWidget::actionController() const noexcept
{
    return m_actionController;
}

// --- Inline title (contract-v2 P4.9) ---

void EditorWidget::setInlineTitle(const QString &title)
{
    if (m_view) m_view->setInlineTitle(title);
}

QString EditorWidget::inlineTitle() const
{
    return m_view ? m_view->inlineTitle() : QString();
}

void EditorWidget::setInlineTitleVisible(bool visible)
{
    if (m_view) m_view->setInlineTitleVisible(visible);
}

bool EditorWidget::inlineTitleVisible() const
{
    return m_view && m_view->inlineTitleVisible();
}

// --- Image / Mermaid / Embed seams (P5.4) ---

void EditorWidget::setImageResourceLookup(Markoff::Canvas::ImageResourceLookup lookup)
{
    if (m_view) m_view->setImageResourceLookup(std::move(lookup));
}

void EditorWidget::setMermaidRenderer(Markoff::Canvas::MermaidRenderer *renderer)
{
    if (m_view) m_view->setMermaidRenderer(renderer);
}

void EditorWidget::setEmbedRegistry(Markoff::EmbedRegistry *registry)
{
    if (m_view) m_view->setEmbedRegistry(registry);
}

// --- Remote presence (P6.2) ---

void EditorWidget::setRemotePresences(const QList<Markoff::Canvas::RemotePresence> &presences)
{
    if (m_view) m_view->setRemotePresences(presences);
}

// --- Format verbs (contract-v2 P4.3) ---
// Each verb delegates straight to the composed View's own method — no
// second implementation, and no indirection through actionController()'s
// QActions (QAction::trigger() would work too, since its slots call the
// same View methods, but going direct here avoids a disabled-action
// early-return silently swallowing a call made through the base contract
// API rather than through a QAction a consumer forgot to enable).

void EditorWidget::toggleBold()
{
    if (m_view) m_view->toggleBold();
}

void EditorWidget::toggleItalic()
{
    if (m_view) m_view->toggleItalic();
}

void EditorWidget::toggleStrikethrough()
{
    if (m_view) m_view->toggleStrikethrough();
}

void EditorWidget::toggleInlineCode()
{
    if (m_view) m_view->toggleInlineCode();
}

void EditorWidget::insertLink()
{
    if (m_view) m_view->insertLink();
}

void EditorWidget::setHeadingLevel(int level)
{
    if (level < 0 || level > 6) return;
    if (m_view) m_view->setHeadingLevel(level);
}

void EditorWidget::setReadOnly(bool ro)
{
    Markoff::MarkdownView::setReadOnly(ro);
    if (m_view)
        m_view->setReadOnly(ro);
}

QRect EditorWidget::caretRect() const
{
    if (!m_view)
        return {};
    const QRect r = m_view->caretRect();
    if (!r.isValid())
        return r;
    // View::caretRect() is already in View's own local coordinates; map
    // into this widget's frame explicitly rather than assume the
    // zero-margin QVBoxLayout keeps them identical (mapTo is correct even
    // if that layout ever grows a margin).
    return r.translated(m_view->mapTo(const_cast<EditorWidget *>(this), QPoint(0, 0)));
}

void EditorWidget::attachFindController(Markoff::FindController *fc)
{
    if (m_findController == fc)
        return;
    if (m_findController)
        detachFindController();
    m_findController = fc;
    if (!fc)
        return;

    QObject::connect(fc, &Markoff::FindController::matchesChanged,
                      this, &EditorWidget::rebuildFindHighlights);
    QObject::connect(fc, &Markoff::FindController::currentMatchChanged,
                      this, &EditorWidget::rebuildFindHighlights);
    QObject::connect(fc, &Markoff::FindController::navigationRequested,
                      this, &EditorWidget::onFindNavigationRequested);

    // The controller may already carry matches (needle set before attach)
    // — push them through immediately rather than waiting for the next
    // matchesChanged.
    rebuildFindHighlights();
}

void EditorWidget::detachFindController()
{
    if (!m_findController)
        return;
    QObject::disconnect(m_findController, nullptr, this, nullptr);
    m_findController = nullptr;
    if (m_view)
        m_view->setFindHighlights({});
}

void EditorWidget::rebuildFindHighlights()
{
    if (!m_view)
        return;
    QList<FindHighlight> highlights;
    if (m_findController) {
        const QList<Markoff::FindController::Match> &matches = m_findController->matches();
        const int currentIdx = m_findController->currentMatchIndex();
        highlights.reserve(matches.size());
        for (int i = 0; i < matches.size(); ++i) {
            const auto &m = matches[i];
            highlights.push_back(FindHighlight{
                m.block, int(m.byteOffset), int(m.byteLength), i == currentIdx});
        }
    }
    m_view->setFindHighlights(highlights);
}

void EditorWidget::onFindNavigationRequested(Markoff::FindController::Match match)
{
    if (!m_view)
        return;
    // Non-focusing caret placement (FindController's contract: the adapter
    // MAY scroll + place the caret but MUST NOT take focus) — setCaretPosition
    // never calls QWidget::setFocus, and its ensureCaretVisible() chokepoint
    // already scrolls the target block into view (P3.2), so this single call
    // covers both halves of "scrolls the match visible + places the caret"
    // without inventing a second caret-move path.
    m_view->setCaretPosition(match.block, int(match.byteOffset));
}

}  // namespace Markoff::Canvas
