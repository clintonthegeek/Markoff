// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/canvas/View.h>

#include <QByteArrayList>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScrollBar>
#include <QtMath>

#include <tuple>
#include <utility>
#include <vector>

#include <markoff/core/AttrNames.h>
#include <markoff/core/KindInference.h>
#include <markoff/core/LinkService.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Session.h>
#include <markoff/core/StructuralKeyHandler.h>
#include <markoff/core/TextUnits.h>
#include <markoff/core/UndoLog.h>

#include <markoff/canvas/CanvasActionController.h>

#include "BlockLayoutCache.h"
#include "Folding.h"
#include "FrontmatterBlock.h"
#include "InlineFormatting.h"
#include "InputPredicate.h"
#include "ProjectionMap.h"
#include "TableGeometry.h"

namespace coords = Markoff::TextUnits;

namespace Markoff::Canvas {

namespace {
/// Page margin, in device-independent pixels, either side of the text
/// column. Widened from 16 to 28 in P5.6 to make room for the fold
/// affordance (below) alongside the marker/checkbox decoration slot that
/// already lived at the margin's inner edge — both fit without colliding
/// now, whereas 16px was already fully claimed by the marker/checkbox
/// alone (findings log, P5.6: a real gutter seam per the F1 audit's #12
/// suggestion is future work; this is the minimal width bump this task
/// needed). Every geometry function in this file derives from
/// `pageMargin()`, never this constant directly, so the bump propagates
/// uniformly.
constexpr qreal kPageMargin = 28.0;
/// Gap between a list marker (or quote bar) and its content.
constexpr qreal kMarkerGap = 6.0;
/// Width of the blockquote bar.
constexpr qreal kQuoteBarWidth = 3.0;

// ---- Task-list checkbox glyph (P4.7) ---------------------------------------
// Painted in the same decoration slot as a list bullet/number (left of
// contentX, gapped by kMarkerGap) but drawn as a box rather than measured
// text, so its hit-test rect doesn't depend on font-metrics text width the
// way the bullet/number marker's would. One function computes the rect;
// paintEvent and taskCheckboxAt both call it, so the clickable area is
// always exactly what got painted.
QRectF taskCheckboxRect(const QFontMetricsF &fm, qreal contentX, qreal contentY)
{
    const qreal side = fm.ascent() * 0.72;
    const qreal right = contentX - kMarkerGap;
    const qreal top = contentY + (fm.ascent() - side) / 2.0;
    return QRectF(right - side, top, side, side);
}

// ---- Fold affordance glyph (P5.6) ------------------------------------------
// Painted at the far-left edge of the page margin — deliberately NOT the
// contentX-relative marker/checkbox slot `taskCheckboxRect` above uses
// (leftIndent-shifted, and already claimed by the bullet/number/checkbox
// for a ListItem head): a foldable ListItem (a long list's first item) can
// ALSO be a task item, and the two glyphs must never collide. Anchored to
// `pageMarginX` (== `View::pageMargin()`'s return, unaffected by a block's
// own `leftIndent`) instead, same one-function-decides-the-rect,
// paint-and-hit-test-share-it shape as `taskCheckboxRect`.
QRectF foldAffordanceRect(const QFontMetricsF &fm, qreal pageMarginX, qreal contentY)
{
    const qreal side = fm.ascent() * 0.55;
    const qreal left = qMax(qreal(2), pageMarginX * 0.28 - side / 2.0);
    const qreal top  = contentY + (fm.ascent() - side) / 2.0;
    return QRectF(left, top, side, side);
}

// ---- Inline title band (P4.9) ---------------------------------------------
/// Multiplies Theme::FontRole::Heading's point size for the title band —
/// Obsidian's own `.inline-title` renders noticeably larger than an H1.
constexpr qreal kTitleFontScale = 1.6;
constexpr qreal kTitleTopPadding = 24.0;
constexpr qreal kTitleBottomPadding = 12.0;

// ---- Frontmatter band (P5.5) -----------------------------------------------
constexpr qreal kFrontmatterTopPadding    = 8.0;
constexpr qreal kFrontmatterBottomPadding = 8.0;

/// Body font at `fontScale`, same pixel-size derivation `fontForSlot`
/// (BlockPresentation.cpp) uses for every block — the frontmatter band is
/// a non-block entry but still shares the document's font scale (P3.5), so
/// its height math and its paint must derive it identically or the two
/// disagree (band clipped or under-filled).
QFont scaledBodyFont(const Theme &theme, qreal fontScale)
{
    QFont f(theme.familyFor(Theme::Slot::TextDefault));
    f.setPixelSize(qMax(1, qRound(theme.pixelSizeFor(Theme::Slot::TextDefault) * fontScale)));
    return f;
}

/// Row-major cell index nearest `byteOffset` (block-relative) in a Table
/// entry's cell-ordered linear sequence (plan P2.3): the sequence itself is
/// just `tableCells`' own order (already row-major, built row-by-row in
/// BlockLayoutCache::realizeTable) — misordering *that* build is the plan's
/// named falsification, not anything computed here. `preferAfter` picks the
/// nearest following cell for a byte that falls in the gap between cells
/// (pipes/padding aren't part of any cell); its complement picks the
/// nearest preceding one.
int cellIndexNear(const BlockLayoutCache::Entry &e, int byteOffset, bool preferAfter)
{
    const int n = int(e.tableCells.size());
    if (n == 0)
        return -1;
    if (preferAfter) {
        for (int i = 0; i < n; ++i) {
            if (e.tableCells[size_t(i)].endByte >= byteOffset)
                return i;
        }
        return n - 1;
    }
    for (int i = n - 1; i >= 0; --i) {
        if (e.tableCells[size_t(i)].startByte <= byteOffset)
            return i;
    }
    return 0;
}

/// [lo, hi] row-major cell indices covered by the raw byte range
/// [fromByte, toByte) of a table block's text, or {-1, -1} if the range is
/// empty. A table participates in block-level selection at cell
/// granularity (plan P2.3) — the whole cell a selection endpoint lands in
/// counts as covered, not just the characters under it.
std::pair<int, int> coveredCellRange(const BlockLayoutCache::Entry &e, int fromByte, int toByte)
{
    if (e.tableCells.empty() || toByte <= fromByte)
        return {-1, -1};
    const int lo = cellIndexNear(e, fromByte, /*preferAfter=*/true);
    const int hi = cellIndexNear(e, toByte, /*preferAfter=*/false);
    if (lo < 0 || hi < 0 || hi < lo)
        return {-1, -1};
    return {lo, hi};
}

/// Serializes the [lo, hi] covered cells (plan P2.3) row-major, pipe-
/// separated within a row, '\n' between rows — the clipboard format for a
/// selection that touches a table, in place of a raw byte-range dump (which
/// would include pipes, alignment-row leftovers, and padding).
QByteArray serializeTableCells(const MarkoffDocument &doc, const BlockLayoutCache::Entry &e,
                               int lo, int hi)
{
    if (lo < 0 || hi < 0 || e.tableCols <= 0)
        return {};
    const QByteArray text = doc.blockText(e.id);
    QByteArrayList rows;
    QByteArrayList row;
    int curRow = lo / e.tableCols;
    for (int i = lo; i <= hi; ++i) {
        const int rowIdx = i / e.tableCols;
        if (rowIdx != curRow) {
            rows << row.join(" | ");
            row.clear();
            curRow = rowIdx;
        }
        const auto &cell = e.tableCells[size_t(i)];
        row << text.mid(cell.startByte, cell.endByte - cell.startByte).trimmed();
    }
    if (!row.isEmpty())
        rows << row.join(" | ");
    return rows.join("\n");
}
}  // namespace

View::View(QWidget *parent)
    : QAbstractScrollArea(parent)
    , m_theme(Theme::defaultLight())
    , m_cache(std::make_unique<BlockLayoutCache>())
{
    viewport()->setFocusPolicy(Qt::NoFocus);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled);
    horizontalScrollBar()->setRange(0, 0);  // text wraps; no horizontal scroll
    // P4.2: hover tracking needs mouseMoveEvent without a button held, and
    // viewport Leave (to close out an in-progress hover) only reaches this
    // widget via the event filter below.
    viewport()->setMouseTracking(true);
    viewport()->installEventFilter(this);
}

View::~View() = default;

void View::setDocument(MarkoffDocument *doc)
{
    if (m_doc == doc)
        return;

    if (m_doc)
        m_doc->disconnect(this);

    m_doc = doc;
    m_cache->clear();
    m_caret = {};
    m_selectionAnchor.reset();
    // Folding (P5.6): fold state is keyed by THIS document's BlockIds — a
    // detach (doc == nullptr) or a swap to a different document leaves
    // them meaningless (same reasoning m_caret/m_selectionAnchor's reset
    // above already follows). EditorWidget::restoreEphemeralState's
    // "reattach, then restore" flow is what repopulates this after a real
    // detach/reattach cycle.
    m_foldedHeads.clear();

    if (m_doc) {
        // d2DocumentChanged is the truth feed. Consuming the document's own
        // debounce is explicitly fine (spec §6, C2 — the forbidden thing is
        // a view-side defer). The targeted block signals are an incomplete
        // surface and are deliberately not subscribed; see the plan's API
        // cheat sheet.
        connect(m_doc, &MarkoffDocument::d2DocumentChanged,
                this, &View::onDocumentChanged);
        connect(m_doc, &MarkoffDocument::documentReloaded,
                this, &View::onDocumentChanged);
    }

    onDocumentChanged();
}

MarkoffDocument *View::document() const
{
    return m_doc;
}

void View::setSession(Session *session)
{
    if (m_session == session)
        return;

    m_session = session;

    // NOTE (P6.0 finding, see plan findings log): a generic "rebuild
    // m_foldedHeads FROM m_session->foldedRegions()" step is deliberately
    // NOT implemented here. FoldRef::start is a raw per-block
    // CollabText::Crdt::Anchor with no block identity attached, and
    // per-block CRDT buffers each run their OWN Lamport clock seeded only
    // by the shared document replica id (MarkoffDocument constructs every
    // block's Buffer as Buffer(d->replicaId), no per-block offset) — so
    // two different foldable blocks' byte-0 anchors routinely collide on
    // (replica_id, char_value) (confirmed: both come out {replicaId, 1}
    // whenever both blocks' first characters were authored by the same
    // replica, the common case for a freshly loaded/typed document).
    // Reverse-resolving an arbitrary FoldRef to "the" BlockId it names is
    // therefore ambiguous whenever more than one foldable block collides
    // this way — this is NOT a rare edge case, it reproduces on any
    // two-heading document. `toggleFold()` below instead keeps
    // m_foldedHeads authoritative for ITS OWN writes (it always knows the
    // exact BlockId at the point it pushes a FoldRef to the Session) and
    // pushes through to the Session so external readers (falsification:
    // reading `session->foldedRegions()` after `toggleFold()`) see real
    // state — Session genuinely receives every fold, it just isn't (yet)
    // resolvable back into a BlockId set from cold session state alone.
    // A session created fresh by EditorWidget::setDocument() always
    // starts with empty foldedRegions(), so this gap does not affect any
    // currently-exercised code path; it would need FoldRef to carry a
    // block identity (a core schema change beyond this task's named
    // seam) to close generally.
}

Session *View::session() const
{
    return m_session;
}

void View::setTheme(const Theme &theme)
{
    m_theme = theme;
    // Styles are baked into cache entries; drop them and re-measure.
    m_cache->clear();
    onDocumentChanged();
}

const Theme &View::theme() const
{
    return m_theme;
}

void View::setFontScale(qreal scale)
{
    if (qFuzzyCompare(m_fontScale, scale))
        return;

    // Capture the block currently at the TOP of the viewport before
    // invalidating anything — every block's height is about to change
    // under the new scale, so "the old pixel/fraction offset" would land
    // on an unrelated block; "which block was at the top" is the anchor
    // that still means something afterward (plan P3.5).
    BlockId anchorBlock;
    if (m_doc && !m_cache->entries().empty()) {
        // Old titleBandHeight() (pre-scale) — matches the still-current
        // font at the moment of this read.
        const int idx = m_cache->indexAtY(verticalScrollBar()->value() - leadingBandHeight());
        if (idx >= 0)
            anchorBlock = m_cache->entries()[size_t(idx)].id;
    }

    m_fontScale = scale;
    // Same shape as setTheme(): every entry's font (hence its style AND
    // its layout's width/height) depends on this, so drop everything
    // rather than trying to patch styles in place — clear() also resets
    // the caret/preedit tracking the cache holds, re-seeded below.
    m_cache->clear();

    if (m_doc) {
        m_cache->setTextWidth(textWidth());
        m_cache->sync(*m_doc, m_theme, m_fontScale);
        m_cache->setCaret(*m_doc, m_theme, m_caret.block, m_caret.byteOffset);
        // Folding (P5.6): sync() above reset every entry's `folded` flag
        // (fresh Entry construction on structural-vs-not doesn't matter
        // here — clear() below/setTheme's own clear() already wiped
        // everything). Fold SHAPE is unaffected by a scale change, so no
        // caret-relocation check is needed here (only toggleFold/document
        // edits can newly hide the caret's own block).
        refreshFoldedBlocks();
    }

    // Re-derive the scrollbar's range for the new (estimated) heights
    // before restoring position — setValue() below would otherwise clamp
    // against the stale (pre-clear) range.
    updateScrollRange();
    if (m_doc && !anchorBlock.isNull()) {
        const int idx = m_cache->indexOf(anchorBlock);
        if (idx >= 0)
            verticalScrollBar()->setValue(
                qRound(m_cache->entries()[size_t(idx)].y + leadingBandHeight()));
    }

    // Realize the (now-restored) visible range — deliberately NOT
    // ensureCaretVisible(): that would re-scroll to the caret's block,
    // which may not be the block we just anchored to.
    ensureLayoutForViewport();

    // Realizing blocks BEFORE the anchor (estimateHeight() only counts
    // newlines, no wrap simulation, so an estimate can be off) corrects
    // their heights, which shifts every later y via the prefix sum —
    // including the anchor's own. Re-read it and correct the scrollbar
    // once more so the anchor's real top, not its pre-realization
    // estimate, ends up at the viewport's top edge; realize again in case
    // that correction exposes a not-yet-realized block at the new
    // position (same "settle" reasoning as ensureLayoutForViewport's own
    // fixed-point loop).
    if (m_doc && !anchorBlock.isNull()) {
        const int idx = m_cache->indexOf(anchorBlock);
        if (idx >= 0)
            verticalScrollBar()->setValue(
                qRound(m_cache->entries()[size_t(idx)].y + leadingBandHeight()));
    }
    ensureLayoutForViewport();

    viewport()->update();
}

void View::setContentWidthPolicy(ContentWidthPolicy policy)
{
    if (m_contentWidthPolicy.kind == policy.kind
        && qFuzzyCompare(m_contentWidthPolicy.fixedColumnWidth + 1.0,
                          policy.fixedColumnWidth + 1.0))
        return;

    m_contentWidthPolicy = policy;
    reflowKeepingScrollAnchor();
    viewport()->update();
}

void View::setImageResourceLookup(ImageResourceLookup lookup)
{
    m_cache->setImageResourceLookup(std::move(lookup));
    ensureLayoutForViewport();
    viewport()->update();
}

void View::setMermaidRenderer(MermaidRenderer *renderer)
{
    m_cache->setMermaidRenderer(renderer);
    ensureLayoutForViewport();
    viewport()->update();
}

void View::setEmbedRegistry(Markoff::EmbedRegistry *registry)
{
    m_cache->setEmbedRegistry(registry);
    ensureLayoutForViewport();
    viewport()->update();
}

void View::reflowKeepingScrollAnchor()
{
    const auto [anchorIdx, anchorFraction] = scrollAnchor();
    BlockId anchorBlock;
    if (m_doc && anchorIdx >= 0)
        anchorBlock = m_cache->entries()[size_t(anchorIdx)].id;

    ensureLayoutForViewport();

    if (m_doc && !anchorBlock.isNull()) {
        const int idx = m_cache->indexOf(anchorBlock);
        if (idx >= 0)
            setScrollAnchor(idx, anchorFraction);
    }
}

void View::setReadOnly(bool ro)
{
    if (m_readOnly == ro)
        return;
    m_readOnly = ro;
    emit readOnlyChanged(ro);
}

bool View::isReadOnly() const
{
    return m_readOnly;
}

QRect View::caretRectInViewport() const
{
    if (!m_doc || m_caret.block.isNull())
        return {};
    const int idx = m_cache->indexOf(m_caret.block);
    if (idx < 0)
        return {};
    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.layout)
        return {};
    const int layoutPos = e.projection.byteToLayoutQChar(m_caret.byteOffset);
    const QTextLine line = e.layout->lineForTextPosition(layoutPos);
    if (!line.isValid())
        return {};
    const qreal scrollY = verticalScrollBar()->value();
    const qreal contentX = pageMargin() + e.style.leftIndent;
    const qreal contentY = (e.y + leadingBandHeight() - scrollY) + e.style.topMargin;
    return QRectF(contentX + line.cursorToX(layoutPos), contentY + line.y(),
                 1, line.height()).toRect();
}

QRect View::caretRect() const
{
    // viewport() is at (0, 0) within View for this QAbstractScrollArea (no
    // frame/scrollbar-corner offset in this leaf's stylesheet), so the
    // viewport-local rect IS View's own local rect. Translate explicitly
    // rather than assume, so a future frame/margin change doesn't silently
    // break the completion-popup anchor.
    const QRect r = caretRectInViewport();
    if (!r.isValid())
        return r;
    return r.translated(viewport()->mapTo(const_cast<View *>(this), QPoint(0, 0)));
}

// ---- Inspection ---------------------------------------------------------

int View::blockCount() const
{
    return int(m_cache->entries().size());
}

int View::realizedBlockCount() const
{
    return m_cache->realizedCount();
}

qreal View::documentHeight() const
{
    return m_cache->totalHeight() + leadingBandHeight();
}

QRectF View::blockRect(BlockId id) const
{
    const int i = m_cache->indexOf(id);
    if (i < 0)
        return {};
    const auto &e = m_cache->entries()[size_t(i)];
    // Folding (P5.6): the reported height is the EFFECTIVE (y-layout)
    // height — 0 for a folded-away entry, matching `recomputePositions()`'s
    // own zero contribution — not the entry's own `height` field (which
    // stays the real/estimated content height internally, unaffected by
    // folding, so realization/measurement keep working). `i >= 0` alone
    // already proves the block is still found/queryable; a zero-height
    // rect at the right y is "occupies no y-space", not "doesn't exist".
    const qreal h = e.folded ? qreal(0) : e.height;
    return QRectF(pageMargin(), e.y + leadingBandHeight(), textWidth(), h);
}

QRectF View::taskCheckboxRectFor(BlockId id) const
{
    const int i = m_cache->indexOf(id);
    if (i < 0)
        return {};
    const auto &e = m_cache->entries()[size_t(i)];
    if (!e.style.isTaskItem)
        return {};
    const qreal contentX = pageMargin() + e.style.leftIndent;
    const qreal contentY = e.y + leadingBandHeight() + e.style.topMargin;
    const QFontMetricsF fm(e.style.font);
    return taskCheckboxRect(fm, contentX, contentY);
}

int View::blockIndexOf(BlockId id) const
{
    return m_cache->indexOf(id);
}

BlockId View::blockIdAt(int index) const
{
    if (index < 0 || index >= int(m_cache->entries().size()))
        return {};
    return m_cache->entries()[size_t(index)].id;
}

quint64 View::paintCount() const
{
    return m_paintCount;
}

QRectF View::tableCellRect(BlockId id, int row, int col) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return {};
    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.style.isTable || e.tableCols <= 0)
        return {};
    const int rows = int(e.tableCells.size()) / e.tableCols;
    if (row < 0 || row >= rows || col < 0 || col >= e.tableCols)
        return {};

    qreal x = pageMargin() + e.style.leftIndent;
    for (int c = 0; c < col; ++c)
        x += e.tableColWidths[size_t(c)];
    qreal y = e.y + leadingBandHeight() + e.style.topMargin;
    for (int r = 0; r < row; ++r)
        y += e.tableRowHeights[size_t(r)];

    return QRectF(x, y, e.tableColWidths[size_t(col)], e.tableRowHeights[size_t(row)]);
}

bool View::isDelimiterHiddenAt(BlockId id, int byteOffset) const
{
    if (!m_doc)
        return false;
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return false;

    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.layout)
        return false;

    const QByteArray text = m_doc->blockText(id);
    const int qchar = int(coords::byteToQtPos(text, byteOffset));

    QList<int> cursorsInBlock;
    if (id == m_caret.block)
        cursorsInBlock.push_back(int(coords::byteToQtPos(text, m_caret.byteOffset)));

    const auto omitted = Detail::omittedDelimiterRanges(m_doc->inlineSpansFor(id), cursorsInBlock);
    for (const auto &[start, length] : omitted) {
        if (qchar >= start && qchar < start + length)
            return true;
    }
    return false;
}

QColor View::codeTokenColorAt(BlockId id, int byteOffset) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return QColor();

    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.layout)
        return QColor();

    const int layoutQChar = e.projection.byteToLayoutQChar(byteOffset);
    for (const QTextLayout::FormatRange &fr : e.layout->formats()) {
        if (layoutQChar >= fr.start && layoutQChar < fr.start + fr.length)
            return fr.format.foreground().color();
    }
    return QColor();
}

qreal View::lineNaturalWidth(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return -1;
    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.layout || e.layout->lineCount() == 0)
        return -1;
    return e.layout->lineAt(0).naturalTextWidth();
}

bool View::isMathPixmapActive(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return false;
    return !m_cache->entries()[size_t(idx)].mathPixmap.isNull();
}

bool View::isImagePixmapActive(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return false;
    const auto &e = m_cache->entries()[size_t(idx)];
    return e.style.isImageBlock && !e.imagePixmap.isNull();
}

bool View::isImagePlaceholderActive(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return false;
    const auto &e = m_cache->entries()[size_t(idx)];
    return e.style.isImageBlock && e.imagePixmap.isNull();
}

bool View::isMermaidPixmapActive(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return false;
    return !m_cache->entries()[size_t(idx)].mermaidPixmap.isNull();
}

bool View::isEmbedPlaceholderActive(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return false;
    return m_cache->entries()[size_t(idx)].style.isEmbedBlock;
}

QString View::mediaLabelFor(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return {};
    return m_cache->entries()[size_t(idx)].mediaLabel;
}

bool View::isCalloutBlock(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return false;
    return m_cache->entries()[size_t(idx)].style.isCallout;
}

bool View::isFootnoteDefBlock(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return false;
    return m_cache->entries()[size_t(idx)].style.isFootnoteDef;
}

// ---- Folding (P5.6) -------------------------------------------------------

QSet<BlockId> View::hiddenBlocksFromFolds() const
{
    QSet<BlockId> hidden;
    if (!m_doc)
        return hidden;
    for (const BlockId head : m_foldedHeads) {
        const Detail::FoldInfo info = Detail::resolveFoldable(*m_doc, head);
        for (const BlockId b : info.body)
            hidden.insert(b);
    }
    return hidden;
}

void View::refreshFoldedBlocks()
{
    // Drop heads the document no longer agrees are foldable (deleted, or
    // edited into a shape resolveFoldable no longer recognizes) — the same
    // "re-derive from authority, don't carry stale view state" rule
    // hiddenBlocksFromFolds() itself follows.
    if (m_doc) {
        QSet<BlockId> stale;
        for (const BlockId head : m_foldedHeads) {
            if (Detail::resolveFoldable(*m_doc, head).kind == Detail::FoldKind::None)
                stale.insert(head);
        }
        m_foldedHeads.subtract(stale);
    }
    m_cache->setFoldedBlocks(hiddenBlocksFromFolds());
}

Markoff::FoldRef View::foldRefFor(BlockId id) const
{
    Markoff::FoldRef ref;
    if (!m_doc)
        return ref;
    const Detail::FoldInfo info = Detail::resolveFoldable(*m_doc, id);
    if (info.kind == Detail::FoldKind::None)
        return ref;

    // LongList/Callout are canvas-local interpretations of core's generic
    // FoldRef::Kind::Block (Folding.h's own doc comment) — core has no
    // concept of either shape.
    ref.kind = (info.kind == Detail::FoldKind::Heading)
                   ? Markoff::FoldRef::Kind::Heading
                   : Markoff::FoldRef::Kind::Block;
    // D2-safe raw anchor at the head block's byte 0 — the P6.0 core seam.
    ref.start = m_doc->blockCrdtAnchorAt(id, 0, CollabText::Crdt::Bias::Left);

    if (info.kind == Detail::FoldKind::Heading) {
        const auto attrs = m_doc->blockAttrs(id);
        const auto it = attrs.constFind(AttrNames::Level);
        if (it != attrs.cend()) {
            if (const int *v = std::get_if<int>(&it.value()))
                ref.headingLevel = *v;
        }
        // headingPath is left empty: resolveFoldable's FoldInfo doesn't
        // carry the ancestor heading chain, and deriving it would mean
        // walking backward through iterateBlocks() re-deriving heading
        // nesting — new plumbing beyond this task's named scope. Logged
        // as a P6.0 finding rather than built here.
    }
    return ref;
}


bool View::isBlockFoldable(BlockId id) const
{
    if (!m_doc)
        return false;
    return Detail::resolveFoldable(*m_doc, id).kind != Detail::FoldKind::None;
}

bool View::isBlockFolded(BlockId id) const
{
    return m_foldedHeads.contains(id);
}

bool View::isBlockHidden(BlockId id) const
{
    const int idx = m_cache->indexOf(id);
    if (idx < 0)
        return false;
    return m_cache->entries()[size_t(idx)].folded;
}

void View::toggleFold(BlockId id)
{
    if (!m_doc || !isBlockFoldable(id))
        return;

    // P6.0: m_foldedHeads stays authoritative for the View's OWN writes —
    // at this exact call site the BlockId <-> FoldRef correspondence is
    // known exactly (see foldRefFor()'s and setSession()'s doc comments
    // for why a *generic* reverse resolution from Session state alone is
    // NOT sound and is therefore not attempted). When a Session is
    // attached, the same toggle is pushed through to it so external
    // readers (falsification: `session->foldedRegions()` after this call)
    // see real state — Session is a genuine second writer target, not
    // just decoration.
    if (m_foldedHeads.contains(id)) {
        m_foldedHeads.remove(id);
    } else {
        m_foldedHeads.insert(id);
    }
    if (m_session)
        m_session->toggleFold(foldRefFor(id));
    refreshFoldedBlocks();

    // If the caret is now inside a body this just hid, it must not be left
    // referencing invisible content — land it on the head instead, same
    // "never strand the caret" rule clampCaret enforces for structural
    // edits. A caret sitting on the head itself, or unaffected by this
    // fold at all, is left untouched.
    const int caretIdx = m_cache->indexOf(m_caret.block);
    if (caretIdx >= 0 && m_cache->entries()[size_t(caretIdx)].folded) {
        setCaret(CanvasCursor{id, 0});
    }

    ensureLayoutForViewport();
    viewport()->update();
}

QRectF View::foldAffordanceRectFor(BlockId id) const
{
    const int i = m_cache->indexOf(id);
    if (i < 0 || !isBlockFoldable(id))
        return {};
    const auto &e = m_cache->entries()[size_t(i)];
    const qreal contentY = e.y + leadingBandHeight() + e.style.topMargin;
    const QFontMetricsF fm(e.style.font);
    return foldAffordanceRect(fm, pageMargin(), contentY);
}

QList<int> View::foldedHeadIndices() const
{
    QList<int> out;
    for (size_t i = 0; i < m_cache->entries().size(); ++i) {
        if (m_foldedHeads.contains(m_cache->entries()[i].id))
            out << int(i);
    }
    return out;
}

void View::setFoldedHeadIndices(const QList<int> &indices)
{
    m_foldedHeads.clear();
    for (const int idx : indices) {
        const BlockId id = blockIdAt(idx);
        if (!id.isNull() && isBlockFoldable(id))
            m_foldedHeads.insert(id);
    }
    refreshFoldedBlocks();
    ensureLayoutForViewport();
    viewport()->update();
}

bool View::isComposing() const
{
    return !m_preeditText.isEmpty();
}

QString View::preeditText() const
{
    return m_preeditText;
}

BlockId View::caretBlock() const
{
    return m_caret.block;
}

int View::caretByteOffset() const
{
    return m_caret.byteOffset;
}

std::optional<std::pair<int, int>> View::caretTableCell() const
{
    if (!m_doc || m_caret.block.isNull())
        return std::nullopt;
    const int idx = m_cache->indexOf(m_caret.block);
    if (idx < 0)
        return std::nullopt;
    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.style.isTable || e.tableCols <= 0 || e.tableCells.empty())
        return std::nullopt;
    // Same row-major cell-index lookup selection/hit-test already use
    // (plan P2.3's cellIndexNear, this file's anonymous namespace) — not a
    // second table-position scheme.
    const int cellIdx = cellIndexNear(e, m_caret.byteOffset, /*preferAfter=*/true);
    if (cellIdx < 0)
        return std::nullopt;
    return std::make_pair(cellIdx / e.tableCols, cellIdx % e.tableCols);
}

std::optional<View::TableCellContext> View::caretTableContext() const
{
    const auto cell = caretTableCell();
    if (!cell)
        return std::nullopt;
    const int idx = m_cache->indexOf(m_caret.block);
    if (idx < 0)
        return std::nullopt;
    const auto &e = m_cache->entries()[size_t(idx)];

    TableCellContext ctx;
    ctx.row = cell->first;
    ctx.col = cell->second;
    ctx.colCount = e.tableCols;
    ctx.rowCount = e.tableCols > 0 ? int(e.tableCells.size()) / e.tableCols : 0;

    // Alignment isn't in the cache's own table grid (BlockLayoutCache never
    // needed it before P5.2) — a fresh TableGeometry parse is the same
    // "read the buffer directly" discipline every other P5.2 op uses.
    const ParsedTable parsed = parseTableBlock(m_doc->blockText(m_caret.block));
    if (parsed.ok && ctx.col >= 0 && ctx.col < int(parsed.alignment.size()))
        ctx.columnAlign = parsed.alignment[size_t(ctx.col)];
    return ctx;
}

bool View::hasSelection() const
{
    return orderedSelection().has_value();
}

BlockId View::selectionAnchorBlock() const
{
    return m_selectionAnchor ? m_selectionAnchor->block : BlockId{};
}

int View::selectionAnchorByteOffset() const
{
    return m_selectionAnchor ? m_selectionAnchor->byteOffset : 0;
}

// ---- Geometry -----------------------------------------------------------

qreal View::layoutWidthFor(qreal viewportWidth) const
{
    const qreal available = qMax(qreal(1), viewportWidth - 2 * kPageMargin);
    if (m_contentWidthPolicy.kind == ContentWidthPolicy::FixedColumn)
        return qMin(available, qMax(qreal(1), m_contentWidthPolicy.fixedColumnWidth));
    return available;
}

qreal View::pageMargin() const
{
    // FixedColumn centers the column: whatever space is left over beyond
    // the minimum page margin (kPageMargin) is split evenly on both
    // sides. FullWidth's layoutWidthFor() already consumes all of it, so
    // this collapses back to plain kPageMargin, same as before P4.5.
    const qreal vw = viewport()->width();
    const qreal contentW = layoutWidthFor(vw);
    return qMax(kPageMargin, (vw - contentW) / 2.0);
}

qreal View::textWidth() const
{
    return layoutWidthFor(viewport()->width());
}

// ---- Inline title (P4.9) --------------------------------------------------

qreal View::titleBandHeight() const
{
    if (!m_inlineTitleVisible)
        return 0.0;
    const QFontMetricsF fm(titleFont());
    return kTitleTopPadding + fm.height() + kTitleBottomPadding;
}

QFont View::titleFont() const
{
    QFont f = m_theme.font(Theme::FontRole::Heading);
    f.setPointSizeF(qMax(1.0, f.pointSizeF() * kTitleFontScale * m_fontScale));
    f.setBold(true);
    return f;
}

void View::layoutTitleLine(QTextLayout &layout, const QString &text) const
{
    layout.setFont(titleFont());
    QTextOption opt;
    opt.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(opt);
    layout.setText(text);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid())
        line.setLineWidth(textWidth());
    layout.endLayout();
}

void View::setInlineTitle(const QString &title)
{
    if (m_inlineTitle == title)
        return;
    m_inlineTitle = title;
    m_titleCaretPos = qBound(0, m_titleCaretPos, m_inlineTitle.size());
    viewport()->update();
}

void View::setInlineTitleVisible(bool visible)
{
    if (m_inlineTitleVisible == visible)
        return;
    m_inlineTitleVisible = visible;
    if (!visible)
        m_titleCaretActive = false;
    // The band's height changes what "document top" means for every block
    // below it — same reflow-and-reanchor shape setContentWidthPolicy()
    // uses for a width change.
    reflowKeepingScrollAnchor();
    viewport()->update();
}

bool View::hitTestTitle(const QPoint &viewportPos, int *outCharPos) const
{
    if (!m_inlineTitleVisible)
        return false;
    const qreal scrollY = verticalScrollBar()->value();
    const qreal docY = viewportPos.y() + scrollY;
    if (docY < 0 || docY >= titleBandHeight())
        return false;
    if (outCharPos) {
        QTextLayout layout;
        layoutTitleLine(layout, m_inlineTitle);
        const QTextLine line = layout.lineAt(0);
        const qreal contentX = pageMargin();
        const qreal localX = viewportPos.x() - contentX;
        *outCharPos = line.isValid() ? line.xToCursor(localX) : 0;
    }
    return true;
}

void View::exitTitleEditingToBlockZero()
{
    m_titleCaretActive = false;
    if (!m_doc || m_cache->entries().empty())
        return;
    // setCaretPosition -> setCaret is the one chokepoint every other
    // programmatic caret placement in this file routes through (P3.1) —
    // no second "move the caret" path for this seam.
    setCaretPosition(m_cache->entries().front().id, 0);
}

void View::handleTitleKeyPress(QKeyEvent *event)
{
    const int key = event->key();

    // Caret seam (spec §5.2): Down or Enter from the title lands at block 0
    // byte 0.
    if (key == Qt::Key_Down || key == Qt::Key_Return || key == Qt::Key_Enter) {
        exitTitleEditingToBlockZero();
        viewport()->update();
        event->accept();
        return;
    }
    // No block sits above the title for an "Up" seam to enter (spec names
    // only the Down direction) — Escape/Up simply leave title-edit mode
    // without moving the document caret.
    if (key == Qt::Key_Escape || key == Qt::Key_Up) {
        m_titleCaretActive = false;
        viewport()->update();
        event->accept();
        return;
    }
    if (key == Qt::Key_Left) {
        if (m_titleCaretPos > 0) --m_titleCaretPos;
        viewport()->update();
        event->accept();
        return;
    }
    if (key == Qt::Key_Right) {
        if (m_titleCaretPos < m_inlineTitle.size()) ++m_titleCaretPos;
        viewport()->update();
        event->accept();
        return;
    }
    if (key == Qt::Key_Home) {
        m_titleCaretPos = 0;
        viewport()->update();
        event->accept();
        return;
    }
    if (key == Qt::Key_End) {
        m_titleCaretPos = m_inlineTitle.size();
        viewport()->update();
        event->accept();
        return;
    }
    if (key == Qt::Key_Backspace) {
        // Backspace at title position 0: nothing to consume in this
        // direction (the falsification target, plan P4.9) — there is no
        // document content above the title for it to reach into; it is
        // simply a no-op here, same as a plain line edit at its own start.
        if (m_titleCaretPos > 0) {
            m_inlineTitle.remove(m_titleCaretPos - 1, 1);
            --m_titleCaretPos;
            emit titleEdited(m_inlineTitle);
            viewport()->update();
        }
        event->accept();
        return;
    }
    if (key == Qt::Key_Delete) {
        if (m_titleCaretPos < m_inlineTitle.size()) {
            m_inlineTitle.remove(m_titleCaretPos, 1);
            emit titleEdited(m_inlineTitle);
            viewport()->update();
        }
        event->accept();
        return;
    }
    if (Detail::isAcceptableTextInput(event) && !event->text().isEmpty()) {
        // Titles are a single line (Obsidian's own inline-title behavior);
        // strip any embedded newline defensively rather than let one split
        // the band into a wrapped multi-line control.
        QString clean = event->text();
        clean.remove(QLatin1Char('\n'));
        if (!clean.isEmpty()) {
            m_inlineTitle.insert(m_titleCaretPos, clean);
            m_titleCaretPos += clean.size();
            emit titleEdited(m_inlineTitle);
        }
        viewport()->update();
        event->accept();
        return;
    }
    event->ignore();
}

void View::paintTitle(QPainter &p) const
{
    if (!m_inlineTitleVisible)
        return;

    const qreal scrollY = verticalScrollBar()->value();
    const qreal bandH = titleBandHeight();
    const qreal top = -scrollY;  // the band always starts at document y=0
    if (top + bandH <= 0 || top >= viewport()->height())
        return;  // scrolled fully out of view

    // Placeholder text is shown only when empty AND not currently being
    // edited — matches a normal line edit's placeholder convention, and
    // keeps the caret's character offsets meaningful (drawn against the
    // REAL m_inlineTitle, never the placeholder string).
    const bool showPlaceholder = m_inlineTitle.isEmpty() && !m_titleCaretActive;
    QTextLayout layout;
    layoutTitleLine(layout, showPlaceholder ? tr("Untitled") : m_inlineTitle);

    const qreal contentX = pageMargin();
    const qreal textY = top + kTitleTopPadding;

    QColor color = m_theme.color(Theme::Slot::Heading1);
    if (showPlaceholder)
        color.setAlpha(120);
    p.setPen(color);
    layout.draw(&p, QPointF(contentX, textY));

    if (m_hasFocus && m_titleCaretActive) {
        p.setPen(m_theme.color(Theme::Slot::CursorPrimary));
        layout.drawCursor(&p, QPointF(contentX, textY),
                          qBound(0, m_titleCaretPos, m_inlineTitle.size()));
    }
}

// ---- Frontmatter (P5.5) ----------------------------------------------------

qreal View::frontmatterBandHeight() const
{
    if (!m_doc)
        return 0.0;
    const auto raw = m_doc->frontmatterValue(QByteArrayLiteral("raw"));
    if (!raw.has_value() || raw->isEmpty())
        return 0.0;

    const QFontMetricsF fm(scaledBodyFont(m_theme, m_fontScale));
    const qreal lineHeight = fm.lineSpacing();

    if (m_frontmatterExpanded) {
        const QString rawYaml = QString::fromUtf8(*raw);
        const int lines = qMax(1, rawYaml.count(QLatin1Char('\n')) + 1);
        return kFrontmatterTopPadding + lineHeight * lines + kFrontmatterBottomPadding;
    }

    const auto props = Detail::parseFrontmatterProperties(QString::fromUtf8(*raw));
    const int rows = qMax(1, props.size());  // at least the "Properties" summary row
    return kFrontmatterTopPadding + lineHeight * rows + kFrontmatterBottomPadding;
}

qreal View::leadingBandHeight() const
{
    return titleBandHeight() + frontmatterBandHeight();
}

bool View::hitTestFrontmatter(const QPoint &viewportPos) const
{
    const qreal bandH = frontmatterBandHeight();
    if (bandH <= 0.0)
        return false;
    const qreal scrollY = verticalScrollBar()->value();
    const qreal docY = viewportPos.y() + scrollY;
    const qreal bandTop = titleBandHeight();
    return docY >= bandTop && docY < bandTop + bandH;
}

void View::paintFrontmatter(QPainter &p) const
{
    const qreal bandH = frontmatterBandHeight();
    if (bandH <= 0.0)
        return;

    const qreal scrollY = verticalScrollBar()->value();
    const qreal top = titleBandHeight() - scrollY;  // right after the title band
    if (top + bandH <= 0 || top >= viewport()->height())
        return;  // scrolled fully out of view

    const qreal contentX = pageMargin();
    QFont font = scaledBodyFont(m_theme, m_fontScale);
    const QFontMetricsF fm(font);
    const qreal lineHeight = fm.lineSpacing();

    p.fillRect(QRectF(contentX, top, textWidth(), bandH),
               m_theme.color(Theme::Slot::CodeBlockBackground));

    p.setFont(font);
    qreal y = top + kFrontmatterTopPadding;

    const auto raw = m_doc->frontmatterValue(QByteArrayLiteral("raw"));
    const QString rawYaml = raw.has_value() ? QString::fromUtf8(*raw) : QString();

    if (m_frontmatterExpanded) {
        // Caret/click reveal (same "show raw source" role code-fence/math
        // per-block reveal plays for a real block — the band has no
        // BlockId, so a click toggles it instead of caret entry).
        QFont mono = m_theme.font(Theme::FontRole::Monospace);
        mono.setPixelSize(font.pixelSize());
        p.setFont(mono);
        p.setPen(m_theme.color(Theme::Slot::TextDefault));
        const QFontMetricsF mfm(mono);
        for (const QString &line : rawYaml.split(QLatin1Char('\n'))) {
            p.drawText(QPointF(contentX + kMarkerGap, y + mfm.ascent()), line);
            y += mfm.lineSpacing();
        }
        return;
    }

    const auto props = Detail::parseFrontmatterProperties(rawYaml);
    p.setPen(m_theme.color(Theme::Slot::Heading6));
    if (props.isEmpty()) {
        p.drawText(QPointF(contentX + kMarkerGap, y + fm.ascent()),
                    tr("Properties"));
        return;
    }
    for (const Detail::FrontmatterProperty &prop : props) {
        QFont bold = font;
        bold.setBold(true);
        p.setFont(bold);
        p.setPen(m_theme.color(Theme::Slot::Heading6));
        const QString keyText = prop.key + QStringLiteral(": ");
        p.drawText(QPointF(contentX + kMarkerGap, y + fm.ascent()), keyText);
        p.setFont(font);
        p.setPen(m_theme.color(Theme::Slot::TextDefault));
        p.drawText(QPointF(contentX + kMarkerGap + QFontMetricsF(bold).horizontalAdvance(keyText),
                            y + fm.ascent()),
                    prop.value);
        y += lineHeight;
    }
}

void View::updateScrollRange()
{
    const int viewportH = viewport()->height();
    const int max = qMax(0, qCeil(m_cache->totalHeight() + leadingBandHeight()) - viewportH);

    QScrollBar *vbar = verticalScrollBar();

    // Realizing blocks replaces estimated heights with real ones, so the
    // document can grow or shrink under a viewport that is already parked
    // at the bottom. Re-pin in that case, or Ctrl+End leaves the last line
    // cut off by however much the estimate was wrong. Guarded on the OLD
    // maximum being non-zero so the first range update on load (0 → N)
    // does not read as "at the bottom" and fling a fresh document to its
    // end.
    const bool wasAtBottom = vbar->maximum() > 0 && vbar->value() >= vbar->maximum();

    vbar->setRange(0, max);
    vbar->setPageStep(viewportH);
    vbar->setSingleStep(
        qMax(1, qRound(QFontMetricsF(m_theme.font(Theme::FontRole::Body))
                           .lineSpacing())));

    if (wasAtBottom && vbar->value() != max)
        vbar->setValue(max);
}

void View::ensureLayoutForViewport()
{
    if (!m_doc)
        return;

    m_cache->setTextWidth(textWidth());

    const qreal height = viewport()->height();

    // Fixed-point loop, not a one-shot pass: realizing corrects heights,
    // which moves the scroll range, which can move the scroll position
    // (bottom re-pin above), which brings different blocks into view.
    // Realization is monotonic — a realized block never becomes
    // unrealized — so this converges, and the cap is belt-and-braces.
    //
    // Note what this deliberately is NOT: a QTimer::singleShot(0) that
    // lets the next event-loop spin sort it out (C2), and not a
    // re-entrance guard around the scrollbar write (C1). It settles
    // synchronously, before returning, because it can.
    constexpr int kMaxPasses = 4;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        const int top = verticalScrollBar()->value();
        // The scrollbar's value is in document space (title band + blocks,
        // P4.9); the cache's own y-space starts at 0 for block 0, so the
        // title band's height is subtracted back out before querying it —
        // titleBandHeight()'s doc comment names this file's every such
        // conversion.
        const qreal cacheTop = top - leadingBandHeight();
        // Realize the viewport plus one viewport-height either side, so a
        // scroll of up to a full page never exposes an unrealized block.
        const bool realized = m_cache->realizeRange(*m_doc, m_theme, cacheTop - height,
                                                    cacheTop + 2 * height);
        updateScrollRange();
        if (!realized && verticalScrollBar()->value() == top)
            break;
    }
}

void View::onDocumentChanged()
{
    const int oldCaretIndex = m_cache->indexOf(m_caret.block);

    if (m_doc) {
        m_cache->setTextWidth(textWidth());
        m_cache->sync(*m_doc, m_theme, m_fontScale);
    }
    // Folding (P5.6): sync() rebuilds every entry (Entry::folded resets to
    // false along with everything else), and a structural edit can also
    // change what a fold head's body even IS — refresh before clampCaret so
    // a caret clamp never lands inside a range this pass just re-hid.
    refreshFoldedBlocks();
    clampCaret(oldCaretIndex);
    // No UndoLog selection state (T4/T5, queue #10 item 2): if the anchor's
    // block didn't survive the edit there is nothing sound to clamp it to,
    // so the selection is dropped rather than guessed at. Mirrors the
    // undo/redo path, which does the same before mutating.
    if (m_selectionAnchor && m_cache->indexOf(m_selectionAnchor->block) < 0)
        m_selectionAnchor.reset();
    promoteCaretBlockKind();
    ensureLayoutForViewport();
    // A document-driven clamp (remote edit, undo/redo) can move the caret
    // to a different block without any of the interactive code paths
    // running — route it through the same chokepoint they use (P3.2) so
    // EditorWidget::cursorPositionChanged still fires and the caret's new
    // block scrolls into view, same as an interactive move would.
    ensureCaretVisible();
    viewport()->update();
}

// ---- Kind transitions (T6) ------------------------------------------------

void View::promoteCaretBlockKind()
{
    if (!m_doc || m_caret.block.isNull())
        return;

    const Markoff::BlockKind current = m_doc->blockKind(m_caret.block);

    // A Heading's *level* keeps tracking its buffer after promotion: the
    // first `#` already promoted the block, so `##`…`######` would never
    // be seen by the promote path below. Form-aware, and only upward-
    // defined — a heading that loses its marker demotes through the
    // structural-key path, not here (live's KindTransition does the same).
    if (current == Markoff::BlockKind::Heading) {
        updateCaretHeadingLevel();
        return;
    }

    // Display math is unreachable by the FIRST '$': that keystroke already
    // promotes the block to Math (mathDisplay=false, the "$…$" inline
    // reading) before a second '$' could ever arrive, so the promote path
    // below — guarded on Paragraph-only — never sees "$$". Re-infer within
    // an already-Math block instead, same shape as the Heading branch
    // above (P1.1 finding: "left for whoever needs display math (P5.3)").
    if (current == Markoff::BlockKind::Math) {
        updateCaretMathDisplayMode();
        return;
    }

    // Otherwise only promote FROM Paragraph: a structural kind's buffer is
    // either content-only (ListItem, BlockQuote) or already carries the
    // marker that would re-trigger this exact inference (CodeBlock) —
    // same guard as the live leaf's KindTransition consumer.
    if (current != Markoff::BlockKind::Paragraph)
        return;

    const QByteArray text = m_doc->blockText(m_caret.block);
    const Markoff::KindInference inferred =
        Markoff::inferBlockKind(QString::fromUtf8(text));
    if (inferred.kind == Markoff::BlockKind::Paragraph)
        return;

    // No buffer edit for ATX/fence promotions: T1 established that a loaded
    // Heading/CodeBlock keeps its ATX prefix/fence, so a typed one must
    // match or the two representations of "the same block" diverge (spec
    // §9, T1 finding). Setext is the exception below — its *loaded*
    // representation is content-only, so matching it means trimming.
    //
    // The kind-defining attrs go in the *same* transaction as the kind
    // (P1.1): a Heading whose level lands in a second transaction renders
    // as H1 for one paint and undoes in two steps.
    {
        UndoLog::Transaction t(m_doc->d2UndoLog());
        m_doc->d2SetBlockKind(m_caret.block, inferred.kind, t);
        if (inferred.kind == Markoff::BlockKind::Heading) {
            // Setext is the one promotion that DOES edit the buffer: the
            // load path drops the underline and `serializeHeading` rebuilds
            // it from `level`, so keeping the typed underline would double
            // it on save.
            if (inferred.setextHeading) {
                int end = text.size();
                while (end > 0 && text[end - 1] == '\n') --end;
                const int underlineNl = text.lastIndexOf('\n', end - 1);
                if (underlineNl >= 0)
                    m_doc->d2ApplyBufferEdit(m_caret.block, uint32_t(underlineNl),
                                             uint32_t(text.size() - underlineNl),
                                             QByteArray(), t);
            }
            m_doc->d2SetBlockAttr(m_caret.block, Markoff::AttrNames::Level,
                                  inferred.headingLevel, t);
            m_doc->d2SetBlockAttr(m_caret.block, Markoff::AttrNames::HeadingForm,
                                  inferred.setextHeading ? QStringLiteral("setext")
                                                         : QStringLiteral("atx"),
                                  t);
        } else if (inferred.kind == Markoff::BlockKind::Math) {
            m_doc->d2SetBlockAttr(m_caret.block, Markoff::AttrNames::DisplayMode,
                                  inferred.mathDisplay, t);
        }
    }

    // The setext trim shortens the block under the caret, which sits past
    // the new end (the user just typed the underline). This runs inside the
    // document-changed pass that called us, so no later clamp is coming.
    m_caret.byteOffset = qBound(0, m_caret.byteOffset,
                                m_doc->blockText(m_caret.block).size());
}

void View::updateCaretHeadingLevel()
{
    const QString text = QString::fromUtf8(m_doc->blockText(m_caret.block));
    const auto attrs = m_doc->blockAttrs(m_caret.block);

    QString form = QStringLiteral("atx");
    if (const auto it = attrs.constFind(Markoff::AttrNames::HeadingForm);
        it != attrs.constEnd()) {
        if (const QString *v = std::get_if<QString>(&it.value()); v && !v->isEmpty())
            form = *v;
    }
    int level = 0;
    if (const auto it = attrs.constFind(Markoff::AttrNames::Level);
        it != attrs.constEnd()) {
        if (const int *v = std::get_if<int>(&it.value()))
            level = *v;
    }

    const int newLevel = (form == QStringLiteral("setext"))
        ? Markoff::matchesSetextShape(text)
        : Markoff::countLeadingHashes(text);
    if (newLevel <= 0 || newLevel == level)
        return;

    UndoLog::Transaction t(m_doc->d2UndoLog());
    m_doc->d2SetBlockAttr(m_caret.block, Markoff::AttrNames::Level, newLevel, t);
}

void View::updateCaretMathDisplayMode()
{
    // Raise-only, same rule as updateCaretHeadingLevel: once display mode
    // is set it is never cleared back to inline by this path (deleting a
    // trailing '$' is the structural-key/backspace path's business, not
    // typing-inference's).
    const auto attrs = m_doc->blockAttrs(m_caret.block);
    if (const auto it = attrs.constFind(Markoff::AttrNames::DisplayMode);
        it != attrs.cend()) {
        if (const bool *v = std::get_if<bool>(&it.value()); v && *v)
            return;
    }

    const QString text = QString::fromUtf8(m_doc->blockText(m_caret.block));
    const Markoff::KindInference inferred = Markoff::inferBlockKind(text);
    if (inferred.kind != Markoff::BlockKind::Math || !inferred.mathDisplay)
        return;

    UndoLog::Transaction t(m_doc->d2UndoLog());
    m_doc->d2SetBlockAttr(m_caret.block, Markoff::AttrNames::DisplayMode, true, t);
}

// ---- Caret / editing (T2) ------------------------------------------------

void View::clampCaret(int oldCaretIndexHint)
{
    if (!m_doc || m_cache->entries().empty()) {
        m_caret = {};
        return;
    }

    int index = m_cache->indexOf(m_caret.block);
    if (index < 0) {
        // The caret's block vanished (or there was no caret yet). Land on
        // the block now occupying roughly the same position — the
        // "nearest surviving block" clamp the plan calls out as load-
        // bearing for T4's undo/redo.
        const int count = int(m_cache->entries().size());
        index = qBound(0, oldCaretIndexHint < 0 ? 0 : oldCaretIndexHint, count - 1);
        m_caret.block = m_cache->entries()[size_t(index)].id;
        m_caret.byteOffset = 0;
        return;
    }

    const int size = m_doc->blockText(m_caret.block).size();
    m_caret.byteOffset = qBound(0, m_caret.byteOffset, size);
}

CanvasCursor View::hitTest(const QPoint &viewportPos) const
{
    if (!m_doc || m_cache->entries().empty())
        return {};

    const qreal scrollY = verticalScrollBar()->value();
    const qreal docY    = viewportPos.y() + scrollY;
    // The title band (P4.9) is not part of hit-testing — it has no BlockId,
    // and this is the ONE hit-test path in this file (mouse press/drag,
    // hover, context-menu link lookup), so gating it here also keeps a
    // drag-selection or a right-click from resolving into the title.
    if (docY < leadingBandHeight())
        return {};
    const int idx = m_cache->indexAtY(docY - leadingBandHeight());
    if (idx < 0)
        return {};

    const auto &e = m_cache->entries()[size_t(idx)];
    if (e.style.isTable)
        return hitTestTable(idx, viewportPos, scrollY);
    if (!e.layout || e.style.isRule)
        return CanvasCursor{e.id, 0};

    const qreal contentX = pageMargin() + e.style.leftIndent;
    const qreal contentY = (e.y + leadingBandHeight() - scrollY) + e.style.topMargin;
    const qreal localX   = viewportPos.x() - contentX;
    const qreal localY   = viewportPos.y() - contentY;

    QTextLine line = e.layout->lineAt(0);
    for (int i = 0; i < e.layout->lineCount(); ++i) {
        QTextLine l = e.layout->lineAt(i);
        line = l;
        if (localY < l.y() + l.height())
            break;
    }

    const int qcharPos = line.isValid() ? line.xToCursor(localX) : 0;
    const int byteOff = e.projection.layoutQCharToByte(qcharPos);
    return CanvasCursor{e.id, byteOff};
}

std::optional<BlockId> View::taskCheckboxAt(const QPoint &viewportPos) const
{
    if (!m_doc || m_cache->entries().empty())
        return std::nullopt;

    const qreal scrollY = verticalScrollBar()->value();
    const qreal docY    = viewportPos.y() + scrollY;
    if (docY < leadingBandHeight())
        return std::nullopt;
    const int idx = m_cache->indexAtY(docY - leadingBandHeight());
    if (idx < 0)
        return std::nullopt;

    const auto &e = m_cache->entries()[size_t(idx)];
    // Realized-only, same as the marker/checkbox paint branch below (no
    // font metrics worth trusting for layout an unrealized entry's
    // estimated height never actually measured).
    if (!e.layout || !e.style.isTaskItem)
        return std::nullopt;

    const qreal contentX = pageMargin() + e.style.leftIndent;
    const qreal contentY = (e.y + leadingBandHeight() - scrollY) + e.style.topMargin;
    const QFontMetricsF fm(e.style.font);
    if (!taskCheckboxRect(fm, contentX, contentY).contains(QPointF(viewportPos)))
        return std::nullopt;
    return e.id;
}

std::optional<BlockId> View::foldAffordanceAt(const QPoint &viewportPos) const
{
    if (!m_doc || m_cache->entries().empty())
        return std::nullopt;

    const qreal scrollY = verticalScrollBar()->value();
    const qreal docY    = viewportPos.y() + scrollY;
    if (docY < leadingBandHeight())
        return std::nullopt;
    const int idx = m_cache->indexAtY(docY - leadingBandHeight());
    if (idx < 0)
        return std::nullopt;

    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.layout || e.folded)
        return std::nullopt;
    if (!isBlockFoldable(e.id))
        return std::nullopt;

    const qreal contentY = (e.y + leadingBandHeight() - scrollY) + e.style.topMargin;
    const QFontMetricsF fm(e.style.font);
    if (!foldAffordanceRect(fm, pageMargin(), contentY).contains(QPointF(viewportPos)))
        return std::nullopt;
    return e.id;
}

CanvasCursor View::hitTestTable(int entryIndex, const QPoint &viewportPos, qreal scrollY) const
{
    const auto &e = m_cache->entries()[size_t(entryIndex)];
    if (e.tableCols <= 0 || e.tableCells.empty())
        return CanvasCursor{e.id, 0};

    const int rows = int(e.tableCells.size()) / e.tableCols;
    const qreal contentX = pageMargin() + e.style.leftIndent;
    const qreal contentY = (e.y + leadingBandHeight() - scrollY) + e.style.topMargin;
    const qreal localX   = viewportPos.x() - contentX;
    const qreal localY   = viewportPos.y() - contentY;

    // Locate the row: walk cumulative row heights, clamp to the last row
    // for a click below the grid (no row/col ops in spike scope — a click
    // anywhere in the block lands in the nearest cell, not a no-op).
    int row = 0;
    qreal rowTop = 0;
    for (; row < rows - 1; ++row) {
        const qreal h = e.tableRowHeights[size_t(row)];
        if (localY < rowTop + h)
            break;
        rowTop += h;
    }

    int col = 0;
    qreal colLeft = 0;
    for (; col < e.tableCols - 1; ++col) {
        const qreal w = e.tableColWidths[size_t(col)];
        if (localX < colLeft + w)
            break;
        colLeft += w;
    }

    const auto &cell = e.tableCells[size_t(row) * size_t(e.tableCols) + size_t(col)];
    if (!cell.layout)
        return CanvasCursor{e.id, cell.startByte};

    const qreal cellLocalX = localX - colLeft - kTableCellPadding;
    const qreal cellLocalY = localY - rowTop - kTableCellPadding;
    // P5.1: a wrapped cell can hold more than one line — pick the line the
    // click's y falls in, same walk hitTest() does for a non-table block's
    // layout, rather than always resolving against lineAt(0).
    QTextLine line = cell.layout->lineAt(0);
    for (int i = 0; i < cell.layout->lineCount(); ++i) {
        QTextLine l = cell.layout->lineAt(i);
        line = l;
        if (cellLocalY < l.y() + l.height())
            break;
    }
    const int qcharPos = line.isValid() ? line.xToCursor(cellLocalX) : 0;

    const int byteOff = cell.startByte + cell.projection.layoutQCharToByte(qcharPos);
    return CanvasCursor{e.id, byteOff};
}

void View::setCaret(const CanvasCursor &caret)
{
    // The one chokepoint every real document caret placement funnels
    // through (P3.1/P3.2 doc comments) — any document-side caret move wins
    // input focus away from the title band (P4.9), so a stale
    // m_titleCaretActive never survives an interactive or programmatic
    // document caret change.
    m_titleCaretActive = false;
    m_caret = caret;
    ensureCaretVisible();
    viewport()->update();
}

void View::setCaretPosition(BlockId block, int byteOffset)
{
    m_selectionAnchor.reset();

    if (m_cache->entries().empty()) {
        m_caret = {};
        viewport()->update();
        return;
    }

    int index = m_cache->indexOf(block);
    if (index < 0) {
        // Unknown block (stale id from a previous document, or a caller
        // that never inspected the current one): clamp to the last block,
        // the same "nearest surviving block" fallback clampCaret uses when
        // there is no better positional hint.
        index = int(m_cache->entries().size()) - 1;
        block = m_cache->entries()[size_t(index)].id;
    }

    const int size = m_doc ? m_doc->blockText(block).size() : 0;
    setCaret(CanvasCursor{block, qBound(0, byteOffset, size)});
}

std::pair<int, float> View::scrollAnchor() const
{
    if (!m_doc || m_cache->entries().empty())
        return {-1, 0.0f};

    const qreal value = qreal(verticalScrollBar()->value()) - leadingBandHeight();
    const int idx = m_cache->indexAtY(value);
    if (idx < 0)
        return {-1, 0.0f};

    const auto &e = m_cache->entries()[size_t(idx)];
    const qreal height = qMax(e.height, qreal(1.0));
    const float fraction = float(qBound(0.0, (value - e.y) / height, 1.0));
    return {idx, fraction};
}

void View::setScrollAnchor(int blockIndex, float fraction)
{
    if (!m_doc || m_cache->entries().empty())
        return;

    blockIndex = qBound(0, blockIndex, int(m_cache->entries().size()) - 1);
    const auto &e = m_cache->entries()[size_t(blockIndex)];
    const qreal target = e.y + leadingBandHeight() + qBound(0.0f, fraction, 1.0f) * e.height;
    // setValue() fires valueChanged -> scrollContentsBy(), which only
    // marks the viewport dirty (deliberately does not realize, per its own
    // comment) — paintEvent is what turns the newly-visible range's
    // estimates into real layouts, same as any other programmatic scroll
    // write in this file (setScrollPositionVisualLine's EditorWidget-side
    // counterpart works the same way).
    verticalScrollBar()->setValue(qRound(target));
}

void View::ensureCaretVisible()
{
    if (!m_doc || m_caret.block.isNull())
        return;
    // Single chokepoint (spec T7): every caret-changing code path in this
    // file already calls ensureCaretVisible() afterward, so this is where
    // the cache learns the caret moved and restyles the (at most two)
    // affected blocks' delimiter visibility — not a full-document restyle.
    m_cache->setCaret(*m_doc, m_theme, m_caret.block, m_caret.byteOffset);
    const int idx = m_cache->indexOf(m_caret.block);
    if (idx < 0)
        return;

    const auto &e = m_cache->entries()[size_t(idx)];
    QScrollBar *vbar = verticalScrollBar();
    const qreal viewportH = viewport()->height();
    const qreal docY = e.y + leadingBandHeight();

    if (docY < vbar->value())
        vbar->setValue(qFloor(docY));
    else if (docY + e.height > vbar->value() + viewportH)
        vbar->setValue(qCeil(docY + e.height - viewportH));

    emit caretChanged();
}

// ---- Selection (T5) -------------------------------------------------------

bool View::caretLessThan(const CanvasCursor &a, const CanvasCursor &b) const
{
    if (a.block == b.block)
        return a.byteOffset < b.byteOffset;
    return m_cache->indexOf(a.block) < m_cache->indexOf(b.block);
}

std::optional<std::pair<CanvasCursor, CanvasCursor>> View::orderedSelection() const
{
    if (!m_selectionAnchor || m_selectionAnchor->block.isNull())
        return std::nullopt;
    if (*m_selectionAnchor == m_caret)
        return std::nullopt;  // anchor caught up with the caret: empty range
    if (caretLessThan(*m_selectionAnchor, m_caret))
        return std::make_pair(*m_selectionAnchor, m_caret);
    return std::make_pair(m_caret, *m_selectionAnchor);
}

std::pair<int, int> View::selectedByteRangeInBlock(
    BlockId id, const CanvasCursor &start, const CanvasCursor &end) const
{
    if (!m_doc)
        return {0, 0};
    const int size = m_doc->blockText(id).size();
    const int startIdx = m_cache->indexOf(id);
    const int firstIdx = m_cache->indexOf(start.block);
    const int lastIdx  = m_cache->indexOf(end.block);
    if (startIdx < firstIdx || startIdx > lastIdx)
        return {0, 0};

    const int from = (id == start.block) ? start.byteOffset : 0;
    const int to   = (id == end.block) ? end.byteOffset : size;
    return {from, to};
}

QByteArray View::selectedText() const
{
    const auto sel = orderedSelection();
    if (!sel || !m_doc)
        return {};
    const auto &[start, end] = *sel;

    QByteArrayList parts;
    for (const auto &e : m_cache->entries()) {
        const int idx = m_cache->indexOf(e.id);
        if (idx < m_cache->indexOf(start.block) || idx > m_cache->indexOf(end.block))
            continue;
        const auto [from, to] = selectedByteRangeInBlock(e.id, start, end);
        if (e.style.isTable) {
            // Cell-ordered serialization (plan P2.3), not a raw byte-range
            // dump — see serializeTableCells's comment.
            const auto [lo, hi] = coveredCellRange(e, from, to);
            const QByteArray cellText = serializeTableCells(*m_doc, e, lo, hi);
            if (!cellText.isEmpty())
                parts << cellText;
            continue;
        }
        parts << m_doc->blockText(e.id).mid(from, to - from);
    }
    return parts.join("\n\n");
}

void View::setFindHighlights(const QList<FindHighlight> &highlights)
{
    m_findHighlightsByBlock.clear();
    for (const FindHighlight &h : highlights) {
        if (h.byteLength <= 0)
            continue;
        m_findHighlightsByBlock[h.block].append(h);
    }
    viewport()->update();
}

QList<FindHighlight> View::findHighlightsForBlock(BlockId id) const
{
    return m_findHighlightsByBlock.value(id);
}

void View::collapseSelection()
{
    const auto sel = orderedSelection();
    if (!sel || !m_doc)
        return;
    const auto &[start, end] = *sel;

    // Blocks strictly between the boundary blocks, document order — read
    // before any mutation touches the id list.
    std::vector<BlockId> middles;
    bool inRange = false;
    for (const auto &e : m_cache->entries()) {
        if (e.id == start.block) { inRange = true; continue; }
        if (e.id == end.block) { inRange = false; continue; }
        if (inRange)
            middles.push_back(e.id);
    }

    UndoLog::Transaction t(m_doc->d2UndoLog());

    if (start.block == end.block) {
        m_doc->d2ApplyBufferEdit(start.block, uint32_t(start.byteOffset),
                                 uint32_t(end.byteOffset - start.byteOffset),
                                 QByteArray(), t);
    } else {
        const int startSize = m_doc->blockText(start.block).size();
        m_doc->d2ApplyBufferEdit(start.block, uint32_t(start.byteOffset),
                                 uint32_t(startSize - start.byteOffset),
                                 QByteArray(), t);
        m_doc->d2ApplyBufferEdit(end.block, 0, uint32_t(end.byteOffset),
                                 QByteArray(), t);
        for (BlockId mid : middles)
            m_doc->d2RemoveBlock(mid, t);

        // Merge the (now content-adjacent) boundary blocks via the same
        // structural path T3 routes keys through — no cross-block byte
        // math (C4): this is Backspace at byte 0 of `end.block`, which
        // StructuralKeyHandler already knows how to merge into its
        // document-order predecessor (start.block, once the middles are
        // gone). The nested Transaction it opens joins this one (UndoLog
        // supports nesting), so the whole collapse is one undo step.
        const StructuralResult r = StructuralKeyHandler::handle(
            *m_doc, end.block, Qt::Key_Backspace, Qt::NoModifier, 0);
        if (r.handled) {
            m_caret.block = r.caretBlock;
            m_caret.byteOffset = int(r.caretByteInBlock);
            m_selectionAnchor.reset();
            return;
        }
    }

    m_caret = start;
    m_selectionAnchor.reset();
}

// ---- Cut/copy/paste/select-all (P4.4) --------------------------------------
// Shared by keyPressEvent's Ctrl+A/C/X/V and the context menu's matching
// QActions — one implementation of each op (class doc comment in the header
// explains why paste routes newlines through tryStructuralKey rather than a
// second block-split implementation).

void View::cut()
{
    // Read-only gate (P3.3, spec §4.2): Cut is disabled in its entirety
    // while read-only — including the copy-to-clipboard half — mirroring
    // Qt's own QAction-disabled convention for Cut (unlike Copy, which stays
    // enabled below).
    if (m_readOnly || !hasSelection())
        return;
    QGuiApplication::clipboard()->setText(QString::fromUtf8(selectedText()));
    collapseSelection();
    ensureCaretVisible();
    viewport()->update();
}

void View::copy()
{
    if (!hasSelection())
        return;
    QGuiApplication::clipboard()->setText(QString::fromUtf8(selectedText()));
}

void View::paste()
{
    if (!m_doc || m_readOnly || m_caret.block.isNull())
        return;

    QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty())
        return;

    if (hasSelection())
        collapseSelection();

    // Normalize line endings before splitting — clipboard text pasted from
    // outside this process may carry CRLF/CR; a bare '\r' left in a line
    // chunk would land as a literal control byte in the block buffer.
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = text.split(QLatin1Char('\n'));

    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            // Between-lines split routes through the SAME StructuralKeyHandler
            // path a real Enter keystroke uses (tryStructuralKey), not a
            // second, paste-only block-split implementation.
            QKeyEvent enterEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            tryStructuralKey(&enterEvent);
        }
        insertPrintable(lines.at(i));
    }

    m_doc->flushPendingD2Changed();
    ensureCaretVisible();
    viewport()->update();
}

void View::selectAll()
{
    if (!m_doc || m_cache->entries().empty())
        return;
    const auto &first = m_cache->entries().front();
    const auto &last = m_cache->entries().back();
    m_selectionAnchor = CanvasCursor{first.id, 0};
    setCaret(CanvasCursor{last.id, int(m_doc->blockText(last.id).size())});
}

// ---- Context menu (P4.4) ---------------------------------------------------

void View::contextMenuEvent(QContextMenuEvent *event)
{
    // Resolve the link (if any) under the triggering point BEFORE building
    // the menu — independent of hover/caret state, since a right-click can
    // land on a link the pointer never hovered and the caret never touched.
    // Qt::NoModifier: this is a menu-population lookup, not a click
    // activation gesture (linkActivationAt's `mods` only matters to
    // callers that branch on Ctrl, which this one doesn't).
    m_contextMenuLink.reset();
    if (m_doc) {
        const CanvasCursor hit = hitTest(event->pos());
        if (!hit.block.isNull())
            m_contextMenuLink = linkActivationAt(hit, Qt::NoModifier);
    }

    QMenu menu(this);
    buildContextMenu(menu);
    if (menu.actions().isEmpty()) {
        event->ignore();
        return;
    }
    menu.exec(event->globalPos());
    event->accept();
}

void View::buildContextMenu(QMenu &menu)
{
    const bool hasDoc = m_doc && !m_caret.block.isNull();
    const bool clipboardHasText = !QGuiApplication::clipboard()->text().isEmpty();

    QAction *cutAction = menu.addAction(tr("Cut"), this, &View::cut);
    cutAction->setShortcut(QKeySequence::Cut);
    cutAction->setEnabled(!m_readOnly && hasSelection());

    QAction *copyAction = menu.addAction(tr("Copy"), this, &View::copy);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setEnabled(hasSelection());

    // The falsification target named by the plan (P4.4): Paste must be
    // disabled while read-only. Gated on clipboard content too (mirrors
    // QPlainTextEdit's own canPaste()-driven Paste action) so an empty
    // clipboard doesn't offer a dead menu item.
    QAction *pasteAction = menu.addAction(tr("Paste"), this, &View::paste);
    pasteAction->setShortcut(QKeySequence::Paste);
    pasteAction->setEnabled(!m_readOnly && hasDoc && clipboardHasText);

    QAction *selectAllAction = menu.addAction(tr("Select All"), this, &View::selectAll);
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    selectAllAction->setEnabled(m_doc && !m_cache->entries().empty());

    // Format section (spec §5.2 "Context menu": "format section" — the P4.3
    // CanvasActionController's own QActions, each already carrying its own
    // enabled-state; omitted entirely when no controller is attached rather
    // than adding disabled/dead items).
    if (m_actionController) {
        menu.addSeparator();
        menu.addAction(m_actionController->boldAction());
        menu.addAction(m_actionController->italicAction());
        menu.addAction(m_actionController->strikeAction());
        menu.addAction(m_actionController->inlineCodeAction());
        menu.addAction(m_actionController->linkAction());
        QMenu *headingMenu = menu.addMenu(tr("Heading"));
        headingMenu->addAction(m_actionController->heading0Action());
        headingMenu->addAction(m_actionController->heading1Action());
        headingMenu->addAction(m_actionController->heading2Action());
        headingMenu->addAction(m_actionController->heading3Action());
        headingMenu->addAction(m_actionController->heading4Action());
        headingMenu->addAction(m_actionController->heading5Action());
        headingMenu->addAction(m_actionController->heading6Action());
    }

    // Table section (P5.2): only present when the CARET (not necessarily
    // the right-click point — table ops act on the caret's cell, same as
    // the format section acts on the caret's block) is currently sitting
    // in a table. InsertTable is offered unconditionally alongside it
    // (via the controller's own enabled-state, which doesn't require being
    // in a table) rather than needing its own separate menu section.
    if (m_actionController) {
        const bool inTable = caretTableContext().has_value();
        menu.addSeparator();
        menu.addAction(m_actionController->insertTableAction());
        if (inTable) {
            QMenu *tableMenu = menu.addMenu(tr("Table"));
            tableMenu->addAction(m_actionController->insertRowAboveAction());
            tableMenu->addAction(m_actionController->insertRowBelowAction());
            tableMenu->addAction(m_actionController->deleteRowAction());
            tableMenu->addSeparator();
            tableMenu->addAction(m_actionController->insertColumnLeftAction());
            tableMenu->addAction(m_actionController->insertColumnRightAction());
            tableMenu->addAction(m_actionController->deleteColumnAction());
            tableMenu->addSeparator();
            QMenu *alignMenu = tableMenu->addMenu(tr("Column Alignment"));
            alignMenu->addAction(m_actionController->alignColumnNoneAction());
            alignMenu->addAction(m_actionController->alignColumnLeftAction());
            alignMenu->addAction(m_actionController->alignColumnCenterAction());
            alignMenu->addAction(m_actionController->alignColumnRightAction());
        }
    }

    // "Copy Link Target" (spec §5.2): only present when the right-click
    // landed on a link/wikilink/tag span — resolved once in contextMenuEvent,
    // read here and by the trigger handler below.
    if (m_contextMenuLink) {
        menu.addSeparator();
        const Markoff::LinkActivation link = *m_contextMenuLink;
        QAction *copyLinkAction = menu.addAction(tr("Copy Link Target"), this, [link] {
            const QString target = link.resolvedTarget.isValid()
                ? link.resolvedTarget.toString()
                : link.rawText;
            QGuiApplication::clipboard()->setText(target);
        });
        Q_UNUSED(copyLinkAction);
    }
}

// ---- Format verbs (P4.3) --------------------------------------------------

void View::applyFormatOp(const FormatOpFn &applyOne)
{
    if (!m_doc || m_readOnly || m_caret.block.isNull())
        return;

    using Markoff::FormatOps::ByteRange;

    const auto sel = orderedSelection();

    if (!sel) {
        // Caret only: single call at the caret's own position. Note:
        // applyOne's underlying FormatOps call flushes the document change
        // synchronously (C2 forbids deferral in this leaf), so
        // onDocumentChanged() has already re-synced the cache against the
        // OLD m_caret by the time this returns — setCaret() below is what
        // actually moves the caret to the op's result and re-syncs it a
        // second time against the correct (post-edit) block content.
        const ByteRange r{m_caret.byteOffset, m_caret.byteOffset};
        const auto result = applyOne(m_caret.block, r);
        if (!result)
            return;  // no-op (matches FormatOps' nullopt contract)
        m_selectionAnchor.reset();
        setCaret(CanvasCursor{m_caret.block, result->start});
        return;
    }

    const auto &[start, end] = *sel;

    if (start.block == end.block) {
        // Single-block selection: exact restoration, same as FormatOps'
        // own single-slice case.
        const ByteRange r{start.byteOffset, end.byteOffset};
        const auto result = applyOne(start.block, r);
        if (!result)
            return;
        m_selectionAnchor = CanvasCursor{start.block, result->start};
        setCaret(CanvasCursor{start.block, result->end});
        return;
    }

    // Multi-block selection: apply to each covered block's own local byte
    // range (never a cross-block sum, C4) inside one outer transaction so
    // the whole gesture is one undo step (UndoLog transactions nest — same
    // pattern collapseSelection's structural-merge tail relies on).
    //
    // Read the covered (block, range) list FIRST, before any mutation —
    // same discipline as collapseSelection's `middles` collection. Each
    // applyOne() call below flushes synchronously (C2), which re-syncs
    // m_cache's own entries vector via onDocumentChanged(); iterating that
    // vector live while mutating through it would be a use-after-resync.
    std::vector<std::pair<BlockId, ByteRange>> covered;
    for (const auto &e : m_cache->entries()) {
        const int idx = m_cache->indexOf(e.id);
        if (idx < m_cache->indexOf(start.block) || idx > m_cache->indexOf(end.block))
            continue;
        const auto [from, to] = selectedByteRangeInBlock(e.id, start, end);
        if (to <= from)
            continue;  // nothing selected in this block (shouldn't happen for
                       // a boundary block, but middles can be empty)
        covered.emplace_back(e.id, ByteRange{from, to});
    }

    BlockId lastBlock;
    std::optional<ByteRange> lastResult;
    {
        Markoff::UndoLog::Transaction t(m_doc->d2UndoLog());
        for (const auto &[id, range] : covered) {
            const auto result = applyOne(id, range);
            if (result) {
                lastBlock = id;
                lastResult = result;
            }
        }
    }

    m_selectionAnchor.reset();
    if (lastResult) {
        // Collapse to the trailing edge of the last covered block that
        // actually changed, mirroring FormatOps::wrapToggle's own
        // multi-slice tail.
        const int size = m_doc->blockText(lastBlock).size();
        setCaret(CanvasCursor{lastBlock, qBound(0, lastResult->end, size)});
    } else {
        setCaret(end);
    }
}

void View::toggleBold()
{
    applyFormatOp([this](BlockId b, Markoff::FormatOps::ByteRange r) {
        return Markoff::FormatOps::wrapToggleInBlock(m_doc, b, r, "**");
    });
}

void View::toggleItalic()
{
    applyFormatOp([this](BlockId b, Markoff::FormatOps::ByteRange r) {
        return Markoff::FormatOps::wrapToggleInBlock(m_doc, b, r, "_");
    });
}

void View::toggleStrikethrough()
{
    applyFormatOp([this](BlockId b, Markoff::FormatOps::ByteRange r) {
        return Markoff::FormatOps::wrapToggleInBlock(m_doc, b, r, "~~");
    });
}

void View::toggleInlineCode()
{
    applyFormatOp([this](BlockId b, Markoff::FormatOps::ByteRange r) {
        return Markoff::FormatOps::wrapToggleInBlock(m_doc, b, r, "`");
    });
}

void View::insertLink()
{
    applyFormatOp([this](BlockId b, Markoff::FormatOps::ByteRange r) {
        return Markoff::FormatOps::insertLinkInBlock(m_doc, b, r);
    });
}

void View::setHeadingLevel(int level)
{
    if (!m_doc || m_readOnly || m_caret.block.isNull())
        return;
    if (level < 0 || level > 6)
        return;

    const BlockId block = m_caret.block;
    const auto result = Markoff::FormatOps::setHeadingLevelInBlock(
        m_doc, block, m_caret.byteOffset, level);
    if (!result)
        return;  // no-op: already at the requested level

    m_selectionAnchor.reset();
    setCaret(CanvasCursor{block, *result});
}

void View::setActionController(CanvasActionController *controller)
{
    m_actionController = controller;
}

void View::insertPrintable(const QString &text)
{
    if (!m_doc || m_caret.block.isNull() || text.isEmpty())
        return;

    const QByteArray insert = text.toUtf8();
    UndoLog::Transaction t(m_doc->d2UndoLog());
    m_doc->d2ApplyBufferEdit(m_caret.block, uint32_t(m_caret.byteOffset), 0, insert, t);
    m_caret.byteOffset += insert.size();
}

void View::deleteCluster(bool forward)
{
    if (!m_doc || m_caret.block.isNull())
        return;

    const int idx = m_cache->indexOf(m_caret.block);
    if (idx < 0)
        return;
    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.layout)
        return;  // not yet realized: caret motion no-ops here too (T2)

    // Stepping through the entry's REAL (reveal-aware) layout, not a
    // throwaway copy of the untransformed text, is what makes a hidden
    // delimiter run get skipped in one keystroke (spec §4.2 P2.1 exit
    // criterion) — those bytes simply aren't present in this text.
    const int layoutPos = e.projection.byteToLayoutQChar(m_caret.byteOffset);

    if (forward) {
        if (layoutPos >= int(e.projection.layoutText().size()))
            return;  // at block end: T3's job (merge with next block)
        const int next = e.layout->nextCursorPosition(layoutPos);
        const int nextByte = e.projection.layoutQCharToByte(next);
        const int removed = nextByte - m_caret.byteOffset;
        UndoLog::Transaction t(m_doc->d2UndoLog());
        m_doc->d2ApplyBufferEdit(m_caret.block, uint32_t(m_caret.byteOffset),
                                 uint32_t(removed), QByteArray(), t);
    } else {
        if (layoutPos <= 0)
            return;  // at block start: T3's job (merge with previous block)
        const int prev = e.layout->previousCursorPosition(layoutPos);
        const int prevByte = e.projection.layoutQCharToByte(prev);
        const int removed = m_caret.byteOffset - prevByte;
        UndoLog::Transaction t(m_doc->d2UndoLog());
        m_doc->d2ApplyBufferEdit(m_caret.block, uint32_t(prevByte),
                                 uint32_t(removed), QByteArray(), t);
        m_caret.byteOffset = prevByte;
    }
}

bool View::tryStructuralKey(QKeyEvent *event)
{
    if (!m_doc || m_caret.block.isNull())
        return false;

    const StructuralResult r = StructuralKeyHandler::handle(
        *m_doc, m_caret.block, event->key(), int(event->modifiers()),
        uint32_t(m_caret.byteOffset));
    if (!r.handled)
        return false;

    m_caret.block = r.caretBlock;
    m_caret.byteOffset = int(r.caretByteInBlock);
    return true;
}

void View::moveCaretHorizontally(bool forward)
{
    if (!m_doc || m_caret.block.isNull())
        return;

    const int idx = m_cache->indexOf(m_caret.block);
    if (idx < 0)
        return;
    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.layout)
        return;  // not yet realized: no-op, same as moveCaretVertically (T2)

    // Stepping through the entry's real (reveal-aware) layout — see
    // deleteCluster's comment — is what skips a hidden delimiter run in
    // one press.
    const int layoutPos = e.projection.byteToLayoutQChar(m_caret.byteOffset);
    const int layoutLen = int(e.projection.layoutText().size());

    if (forward) {
        if (layoutPos >= layoutLen) {
            // Folding (P5.6): step to the next VISIBLE entry, not a bare
            // idx+1 — a folded range's blocks are still real cache entries
            // (BlockLayoutCache::Entry, "found/queryable"), just invisible,
            // so a plain +1 would land the caret inside now-hidden content.
            const int nextIdx = nextVisibleEntryIndex(idx, /*forward=*/true);
            if (nextIdx >= 0) {
                m_caret.block = m_cache->entries()[size_t(nextIdx)].id;
                m_caret.byteOffset = 0;
            }
            return;
        }
        const int next = e.layout->nextCursorPosition(layoutPos);
        m_caret.byteOffset = e.projection.layoutQCharToByte(next);
    } else {
        if (layoutPos <= 0) {
            const int prevIdx = nextVisibleEntryIndex(idx, /*forward=*/false);
            if (prevIdx >= 0) {
                const auto &prevEntry = m_cache->entries()[size_t(prevIdx)];
                m_caret.block = prevEntry.id;
                m_caret.byteOffset = m_doc->blockText(prevEntry.id).size();
            }
            return;
        }
        const int prev = e.layout->previousCursorPosition(layoutPos);
        m_caret.byteOffset = e.projection.layoutQCharToByte(prev);
    }
}

int View::nextVisibleEntryIndex(int idx, bool forward) const
{
    const auto &entries = m_cache->entries();
    int i = idx + (forward ? 1 : -1);
    while (i >= 0 && i < int(entries.size())) {
        if (!entries[size_t(i)].folded)
            return i;
        i += forward ? 1 : -1;
    }
    return -1;
}

void View::moveCaretToLineEdge(bool home)
{
    if (!m_doc || m_caret.block.isNull())
        return;
    const int idx = m_cache->indexOf(m_caret.block);
    if (idx < 0)
        return;
    const auto &e = m_cache->entries()[size_t(idx)];
    if (!e.layout)
        return;

    const int layoutPos = e.projection.byteToLayoutQChar(m_caret.byteOffset);
    const QTextLine line = e.layout->lineForTextPosition(layoutPos);
    if (!line.isValid())
        return;

    const int edgeQChar = home ? line.textStart() : line.textStart() + line.textLength();
    m_caret.byteOffset = e.projection.layoutQCharToByte(edgeQChar);
}

void View::moveCaretVertically(bool forward)
{
    if (!m_doc || m_caret.block.isNull())
        return;
    const int idx = m_cache->indexOf(m_caret.block);
    if (idx < 0)
        return;
    const auto &e = m_cache->entries()[size_t(idx)];

    // P5.1: a table has its own per-cell wrapped layouts, not one
    // block-wide `e.layout` (that member is unused/null for tables — see
    // realizeTable's comment) — Up/Down inside one needs its own walk:
    // line-in-cell, then row-in-column, then out of the table entirely.
    if (e.style.isTable) {
        moveCaretVerticallyInTable(idx, forward);
        return;
    }

    if (!e.layout)
        return;

    const int layoutPos = e.projection.byteToLayoutQChar(m_caret.byteOffset);
    const QTextLine curLine = e.layout->lineForTextPosition(layoutPos);
    const int lineNo = curLine.isValid() ? curLine.lineNumber() : 0;
    const qreal x = curLine.isValid() ? curLine.cursorToX(layoutPos) : 0;

    const int targetLineNo = lineNo + (forward ? 1 : -1);
    if (targetLineNo >= 0 && targetLineNo < e.layout->lineCount()) {
        const QTextLine targetLine = e.layout->lineAt(targetLineNo);
        const int newQChar = targetLine.xToCursor(x);
        m_caret.byteOffset = e.projection.layoutQCharToByte(newQChar);
        return;
    }

    // Off the top/bottom of this block's layout: cross into the
    // previous/next block, landing near the same x on its nearest edge
    // line. Exact column affinity is not a criterion (plan T2). Folding
    // (P5.6): the next VISIBLE entry, not a bare idx+-1 — see
    // moveCaretHorizontally's identical reasoning.
    const int nextIdx = nextVisibleEntryIndex(idx, forward);
    if (nextIdx < 0)
        return;

    const auto &target = m_cache->entries()[size_t(nextIdx)];
    m_caret.block = target.id;

    // P2.3: crossing into a table from an adjacent block's edge lands in
    // the nearest cell by x — row 0 (header) when entering from above, the
    // last row when entering from below — not byte 0 of the raw block
    // text, which the generic layout-less fallback below would give.
    if (target.style.isTable) {
        if (target.tableCols <= 0 || target.tableCells.empty()) {
            m_caret.byteOffset = 0;
            return;
        }
        int col = 0;
        qreal colLeft = 0;
        for (; col < target.tableCols - 1; ++col) {
            const qreal w = target.tableColWidths[size_t(col)];
            if (x < colLeft + w)
                break;
            colLeft += w;
        }
        const int rows = int(target.tableCells.size()) / target.tableCols;
        const int row = forward ? 0 : rows - 1;
        const auto &cell = target.tableCells[size_t(row) * size_t(target.tableCols) + size_t(col)];
        m_caret.byteOffset = cell.startByte;
        return;
    }

    if (!target.layout || target.layout->lineCount() == 0) {
        m_caret.byteOffset = 0;
        return;
    }
    const QTextLine edgeLine = target.layout->lineAt(forward ? 0 : target.layout->lineCount() - 1);
    const int newQChar = edgeLine.xToCursor(x);
    m_caret.byteOffset = target.projection.layoutQCharToByte(newQChar);
}

void View::moveCaretVerticallyInTable(int idx, bool forward)
{
    const auto &e = m_cache->entries()[size_t(idx)];
    if (e.tableCols <= 0 || e.tableCells.empty())
        return;

    const int cols = e.tableCols;
    const int rows = int(e.tableCells.size()) / cols;
    // Same row-major cell lookup Tab navigation and caretTableCell() use
    // (plan P2.3's cellIndexNear) — not a second table-position scheme.
    const int cellIdx = cellIndexNear(e, m_caret.byteOffset, /*preferAfter=*/true);
    if (cellIdx < 0)
        return;
    const int row = cellIdx / cols;
    const int col = cellIdx % cols;
    const auto &cell = e.tableCells[size_t(cellIdx)];

    qreal x = 0;
    int lineNo = 0;
    const int lineCount = cell.layout ? cell.layout->lineCount() : 0;
    if (cell.layout && lineCount > 0) {
        const int layoutPos = cell.projection.byteToLayoutQChar(m_caret.byteOffset - cell.startByte);
        const QTextLine curLine = cell.layout->lineForTextPosition(layoutPos);
        lineNo = curLine.isValid() ? curLine.lineNumber() : 0;
        x = curLine.isValid() ? curLine.cursorToX(layoutPos) : 0;
    }

    // Step 1 (P5.1 done-when "Up/Down at cell edge moves rows"): still
    // inside this cell's own wrap — move a line within it, same as the
    // non-table case above.
    const int targetLineNo = lineNo + (forward ? 1 : -1);
    if (targetLineNo >= 0 && targetLineNo < lineCount) {
        const QTextLine targetLine = cell.layout->lineAt(targetLineNo);
        const int newQChar = targetLine.xToCursor(x);
        m_caret.byteOffset = cell.startByte + cell.projection.layoutQCharToByte(newQChar);
        return;
    }

    // Step 2: at the top/bottom line of this cell's own wrap — move to the
    // same column, next/previous row. The column doesn't change, so `x`
    // (cell-local) is directly comparable against the target cell's own
    // layout with no coordinate conversion.
    const int targetRow = row + (forward ? 1 : -1);
    if (targetRow >= 0 && targetRow < rows) {
        const auto &targetCell = e.tableCells[size_t(targetRow) * size_t(cols) + size_t(col)];
        if (targetCell.layout && targetCell.layout->lineCount() > 0) {
            const QTextLine targetLine = targetCell.layout->lineAt(
                forward ? 0 : targetCell.layout->lineCount() - 1);
            const int newQChar = targetLine.xToCursor(x);
            m_caret.byteOffset =
                targetCell.startByte + targetCell.projection.layoutQCharToByte(newQChar);
        } else {
            m_caret.byteOffset = targetCell.startByte;
        }
        return;
    }

    // Step 3 (done-when "...then exits the table"): off the top/bottom row
    // — leave the table for the adjacent block, same x-based landing
    // moveCaretVertically's own cross-block path uses for entries with a
    // real `e.layout`. `x` has to move from cell-local to table-content-
    // local coordinates first (the same space that path's `x` lives in).
    // Folding (P5.6): next VISIBLE entry — a table can itself sit inside a
    // folded heading section's body, same as any other block kind.
    const int nextIdx = nextVisibleEntryIndex(idx, forward);
    if (nextIdx < 0)
        return;

    qreal colLeft = 0;
    for (int c = 0; c < col; ++c)
        colLeft += e.tableColWidths[size_t(c)];
    const qreal tableX = colLeft + kTableCellPadding + x;

    const auto &target = m_cache->entries()[size_t(nextIdx)];
    m_caret.block = target.id;

    if (target.style.isTable) {
        if (target.tableCols <= 0 || target.tableCells.empty()) {
            m_caret.byteOffset = 0;
            return;
        }
        int tcol = 0;
        qreal tcolLeft = 0;
        for (; tcol < target.tableCols - 1; ++tcol) {
            const qreal w = target.tableColWidths[size_t(tcol)];
            if (tableX < tcolLeft + w)
                break;
            tcolLeft += w;
        }
        const int trows = int(target.tableCells.size()) / target.tableCols;
        const int trow = forward ? 0 : trows - 1;
        const auto &tcell =
            target.tableCells[size_t(trow) * size_t(target.tableCols) + size_t(tcol)];
        m_caret.byteOffset = tcell.startByte;
        return;
    }

    if (!target.layout || target.layout->lineCount() == 0) {
        m_caret.byteOffset = 0;
        return;
    }
    const QTextLine edgeLine = target.layout->lineAt(forward ? 0 : target.layout->lineCount() - 1);
    const int newQChar = edgeLine.xToCursor(tableX);
    m_caret.byteOffset = target.projection.layoutQCharToByte(newQChar);
}

// ---- Tables: Tab / Shift+Tab cell navigation (P5.1) ------------------------

void View::appendTableRow(BlockId block, int cols)
{
    if (!m_doc)
        return;
    const QByteArray text = m_doc->blockText(block);
    // A minimal syntactically-valid empty row for `cols` columns: `cols+1`
    // pipes bounding `cols` one-space cell ranges (TableGeometry's
    // tokenizeLine just needs >= 2 pipes per line; it doesn't require the
    // padding spaces real markdown tables usually carry).
    QByteArray row = "\n|";
    for (int c = 0; c < cols; ++c)
        row += "  |";
    UndoLog::Transaction t(m_doc->d2UndoLog());
    m_doc->d2ApplyBufferEdit(block, uint32_t(text.size()), 0, row, t);
}

bool View::tryTableTab(bool shift)
{
    if (!m_doc || m_caret.block.isNull())
        return false;
    const int idx = m_cache->indexOf(m_caret.block);
    if (idx < 0)
        return false;

    const BlockId id = m_caret.block;
    int row = -1, col = -1, cols = 0, rows = 0;
    {
        const auto &e = m_cache->entries()[size_t(idx)];
        if (!e.style.isTable || e.tableCols <= 0 || e.tableCells.empty())
            return false;
        cols = e.tableCols;
        rows = int(e.tableCells.size()) / cols;
        const int cellIdx = cellIndexNear(e, m_caret.byteOffset, /*preferAfter=*/true);
        if (cellIdx < 0)
            return false;
        row = cellIdx / cols;
        col = cellIdx % cols;
    }

    // Tab/Shift+Tab always collapses any selection to the destination
    // cell's start — there is no "extend selection with Tab" semantic.
    m_selectionAnchor.reset();

    if (shift) {
        if (row == 0 && col == 0)
            return true;  // first cell: still ours to swallow, just a no-op
        --col;
        if (col < 0) { col = cols - 1; --row; }
        const auto &e = m_cache->entries()[size_t(m_cache->indexOf(id))];
        const auto &cell = e.tableCells[size_t(row) * size_t(cols) + size_t(col)];
        setCaret(CanvasCursor{id, cell.startByte});
        return true;
    }

    // Obsidian behavior (plan P5.1 done-when): Tab in the table's last cell
    // appends a new (empty) row and lands the caret in its first cell,
    // rather than leaving the table or no-op'ing.
    if (row == rows - 1 && col == cols - 1) {
        appendTableRow(id, cols);
        // The buffer edit above is queued, not yet reflected in the cache
        // (d2DocumentChanged is coalesced — see the flush comments at the
        // other d2ApplyBufferEdit call sites in this file); flush so the
        // new row exists before this function reads it back, and read it
        // straight off the fresh buffer (TableGeometry) rather than trust
        // the cache's own (lazily-realized) table grid to already reflect
        // it.
        m_doc->flushPendingD2Changed();
        const ParsedTable parsed = parseTableBlock(m_doc->blockText(id));
        if (parsed.ok && !parsed.rows.empty() && !parsed.rows.back().empty())
            setCaret(CanvasCursor{id, parsed.rows.back().front().start});
        return true;
    }

    ++col;
    if (col >= cols) { col = 0; ++row; }
    const auto &e = m_cache->entries()[size_t(m_cache->indexOf(id))];
    const auto &cell = e.tableCells[size_t(row) * size_t(cols) + size_t(col)];
    setCaret(CanvasCursor{id, cell.startByte});
    return true;
}

// ---- Tables: row/col ops + alignment (P5.2) --------------------------------

void View::repositionCaretInTable(BlockId block, int row, int col)
{
    if (!m_doc)
        return;
    m_doc->flushPendingD2Changed();
    const ParsedTable parsed = parseTableBlock(m_doc->blockText(block));
    if (!parsed.ok || parsed.rows.empty() || parsed.cols <= 0)
        return;
    row = qBound(0, row, int(parsed.rows.size()) - 1);
    col = qBound(0, col, parsed.cols - 1);
    setCaret(CanvasCursor{block, parsed.rows[size_t(row)][size_t(col)].start});
}

void View::insertTableRowAbove()
{
    if (!m_doc || m_readOnly)
        return;
    const auto ctx = caretTableContext();
    if (!ctx || ctx->row <= 0)  // nothing sensible above the header
        return;
    const BlockId block = m_caret.block;
    const ParsedTable parsed = parseTableBlock(m_doc->blockText(block));
    if (!parsed.ok)
        return;

    // rows[r] (r >= 1) is lines[r + 1]; "above row r" lands the new row
    // right after the PRECEDING line, i.e. right before row r's own line.
    const int rowLineIdx = ctx->row + 1;
    if (rowLineIdx <= 0 || rowLineIdx > int(parsed.lines.size()))
        return;
    const int pos = parsed.lines[size_t(rowLineIdx - 1)].lineEnd;

    QByteArray rowBytes = "|";
    for (int c = 0; c < parsed.cols; ++c) rowBytes += "  |";

    UndoLog::Transaction t(m_doc->d2UndoLog());
    m_doc->d2ApplyBufferEdit(block, uint32_t(pos), 0, "\n" + rowBytes, t);

    repositionCaretInTable(block, ctx->row + 1, ctx->col);
}

void View::insertTableRowBelow()
{
    if (!m_doc || m_readOnly)
        return;
    const auto ctx = caretTableContext();
    if (!ctx)
        return;
    const BlockId block = m_caret.block;
    const ParsedTable parsed = parseTableBlock(m_doc->blockText(block));
    if (!parsed.ok)
        return;

    // "Below the header" (row 0) can't land between the header and its
    // delimiter row — it lands after the delimiter instead, as the new
    // first body row. rows[r] (r >= 1) is lines[r + 1]: "below row r"
    // inserts right after that row's own line.
    const int anchorLineIdx = (ctx->row == 0) ? 1 : ctx->row + 1;
    if (anchorLineIdx < 0 || anchorLineIdx >= int(parsed.lines.size()))
        return;
    const int pos = parsed.lines[size_t(anchorLineIdx)].lineEnd;

    QByteArray rowBytes = "|";
    for (int c = 0; c < parsed.cols; ++c) rowBytes += "  |";

    UndoLog::Transaction t(m_doc->d2UndoLog());
    m_doc->d2ApplyBufferEdit(block, uint32_t(pos), 0, "\n" + rowBytes, t);

    repositionCaretInTable(block, ctx->row, ctx->col);
}

void View::deleteTableRow()
{
    if (!m_doc || m_readOnly)
        return;
    const auto ctx = caretTableContext();
    if (!ctx || ctx->row <= 0)  // the header isn't deletable
        return;
    const BlockId block = m_caret.block;
    const ParsedTable parsed = parseTableBlock(m_doc->blockText(block));
    if (!parsed.ok)
        return;

    const int rowLineIdx = ctx->row + 1;  // rows[r] (r >= 1) is lines[r + 1]
    if (rowLineIdx <= 0 || rowLineIdx >= int(parsed.lines.size()))
        return;
    const int from = parsed.lines[size_t(rowLineIdx - 1)].lineEnd;
    const int to   = parsed.lines[size_t(rowLineIdx)].lineEnd;

    UndoLog::Transaction t(m_doc->d2UndoLog());
    m_doc->d2ApplyBufferEdit(block, uint32_t(from), uint32_t(to - from), QByteArray(), t);

    // The deleted row's slot is now occupied by what used to be the next
    // row (or the previous one, if it was the last row) — land there.
    const int newRow = (ctx->row < int(parsed.rows.size()) - 1) ? ctx->row : ctx->row - 1;
    repositionCaretInTable(block, newRow, ctx->col);
}

void View::insertTableColumnLeft()
{
    if (!m_doc || m_readOnly)
        return;
    const auto ctx = caretTableContext();
    if (!ctx)
        return;
    const BlockId block = m_caret.block;
    const ParsedTable parsed = parseTableBlock(m_doc->blockText(block));
    if (!parsed.ok || ctx->col < 0 || ctx->col >= parsed.cols)
        return;

    UndoLog::Transaction t(m_doc->d2UndoLog());
    // Reverse line order (last physical line first): each insert position
    // below is taken from the frozen `parsed` snapshot, and d2ApplyBufferEdit
    // applies synchronously, so an edit on a LATER line must land first —
    // it never shifts an EARLIER line's still-to-be-used byte positions.
    for (int li = int(parsed.lines.size()) - 1; li >= 0; --li) {
        const TableLine &line = parsed.lines[size_t(li)];
        if (ctx->col >= int(line.cells.size()))
            continue;
        const int pos = line.cells[size_t(ctx->col)].start;  // right after the preceding '|'
        const QByteArray newCell =
            (li == 1) ? (alignmentCellText(TableAlign::None) + "|") : QByteArray("  |");
        m_doc->d2ApplyBufferEdit(block, uint32_t(pos), 0, newCell, t);
    }

    repositionCaretInTable(block, ctx->row, ctx->col + 1);
}

void View::insertTableColumnRight()
{
    if (!m_doc || m_readOnly)
        return;
    const auto ctx = caretTableContext();
    if (!ctx)
        return;
    const BlockId block = m_caret.block;
    const ParsedTable parsed = parseTableBlock(m_doc->blockText(block));
    if (!parsed.ok || ctx->col < 0 || ctx->col >= parsed.cols)
        return;

    UndoLog::Transaction t(m_doc->d2UndoLog());
    for (int li = int(parsed.lines.size()) - 1; li >= 0; --li) {
        const TableLine &line = parsed.lines[size_t(li)];
        if (ctx->col >= int(line.cells.size()))
            continue;
        const int pos = line.cells[size_t(ctx->col)].end + 1;  // right after the following '|'
        const QByteArray newCell =
            (li == 1) ? (alignmentCellText(TableAlign::None) + "|") : QByteArray("  |");
        m_doc->d2ApplyBufferEdit(block, uint32_t(pos), 0, newCell, t);
    }

    repositionCaretInTable(block, ctx->row, ctx->col);
}

void View::deleteTableColumn()
{
    if (!m_doc || m_readOnly)
        return;
    const auto ctx = caretTableContext();
    if (!ctx || ctx->colCount <= 1)  // the table's last column isn't deletable
        return;
    const BlockId block = m_caret.block;
    const ParsedTable parsed = parseTableBlock(m_doc->blockText(block));
    if (!parsed.ok || parsed.cols <= 1 || ctx->col < 0 || ctx->col >= parsed.cols)
        return;

    UndoLog::Transaction t(m_doc->d2UndoLog());
    for (int li = int(parsed.lines.size()) - 1; li >= 0; --li) {
        const TableLine &line = parsed.lines[size_t(li)];
        if (ctx->col >= int(line.cells.size()))
            continue;
        const TableCellRange &cell = line.cells[size_t(ctx->col)];
        // Removes the pipe PRECEDING this column plus its content, leaving
        // the following pipe as the new boundary — works uniformly for the
        // first, a middle, or the last column, no special-casing needed.
        const int from = cell.start - 1;
        const int to   = cell.end;
        if (from < line.lineStart)
            continue;  // defensive; shouldn't happen for a well-formed line
        m_doc->d2ApplyBufferEdit(block, uint32_t(from), uint32_t(to - from), QByteArray(), t);
    }

    const int newCol = (ctx->col < parsed.cols - 1) ? ctx->col : ctx->col - 1;
    repositionCaretInTable(block, ctx->row, newCol);
}

void View::setTableColumnAlignment(TableAlign align)
{
    if (!m_doc || m_readOnly)
        return;
    const auto ctx = caretTableContext();
    if (!ctx)
        return;
    const BlockId block = m_caret.block;
    const ParsedTable parsed = parseTableBlock(m_doc->blockText(block));
    if (!parsed.ok || ctx->col < 0 || ctx->col >= parsed.cols || parsed.lines.size() < 2)
        return;
    const TableLine &alignLine = parsed.lines[1];
    if (ctx->col >= int(alignLine.cells.size()))
        return;
    const TableCellRange &cell = alignLine.cells[size_t(ctx->col)];

    UndoLog::Transaction t(m_doc->d2UndoLog());
    m_doc->d2ApplyBufferEdit(block, uint32_t(cell.start), uint32_t(cell.end - cell.start),
                             alignmentCellText(align), t);

    // The caret's own row/col don't move, but the delimiter row (always
    // physically before every body row) just changed size, which shifts
    // every byte offset after it — reposition even though the logical
    // cell is unchanged.
    repositionCaretInTable(block, ctx->row, ctx->col);
}

void View::insertTable()
{
    if (!m_doc || m_readOnly || m_caret.block.isNull())
        return;

    static const QByteArray kTemplate =
        "|  |  |\n"
        "| --- | --- |\n"
        "|  |  |";

    UndoLog::Transaction t(m_doc->d2UndoLog());
    const BlockId newId = m_doc->d2InsertBlock(m_caret.block, Markoff::BlockKind::Table, t);
    m_doc->d2ApplyBufferEdit(newId, 0, 0, kTemplate, t);

    repositionCaretInTable(newId, 0, 0);
}

// ---- Events -------------------------------------------------------------

void View::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    // A resize changes textWidth() (and, under FixedColumn, pageMargin()
    // too), which reflows every visible block's height — keep the block
    // that was on top pinned there rather than leaving the scrollbar at
    // its old raw pixel value (P4.5 done-when: "scroll stays anchored to
    // top visible block").
    reflowKeepingScrollAnchor();
    viewport()->update();
}

void View::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    // Full repaint rather than a blit: realization below the fold can
    // change heights, so the scrolled-in region is not merely the old
    // content shifted.
    //
    // Deliberately does NOT realize here. paintEvent is the single place
    // that turns estimates into layouts, and it can move the scroll
    // position while doing so; if this slot realized too, that write would
    // re-enter here and the obvious fix would be a re-entrance guard —
    // exactly what C1 forbids. Structuring it so the recursion cannot
    // happen beats guarding against it. update() only marks the viewport
    // dirty, so paint is where the work lands.
    viewport()->update();
}

void View::keyPressEvent(QKeyEvent *event)
{
    QScrollBar *vbar = verticalScrollBar();
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);

    // Page/document-jump keys stay scroll actions even with a caret in
    // play; T1's un-modified Up/Down/Home/End become caret motion below.
    switch (event->key()) {
    case Qt::Key_PageDown:
        vbar->triggerAction(QAbstractSlider::SliderPageStepAdd);
        event->accept();
        return;
    case Qt::Key_PageUp:
        vbar->triggerAction(QAbstractSlider::SliderPageStepSub);
        event->accept();
        return;
    case Qt::Key_Home:
        if (ctrl) {
            vbar->setValue(vbar->minimum());
            event->accept();
            return;
        }
        break;
    case Qt::Key_End:
        if (ctrl) {
            vbar->setValue(vbar->maximum());
            event->accept();
            return;
        }
        break;
    default:
        break;
    }

    // Title-band input (P4.9): while editing the title, keys are routed
    // there instead of the document — a completely separate handler, not a
    // reuse of the document's StructuralKeyHandler/insertPrintable/
    // deleteCluster paths (the title has no BlockId/byte buffer for those
    // to operate on).
    if (m_titleCaretActive) {
        handleTitleKeyPress(event);
        return;
    }

    // Undo/redo are document-level actions, not caret motion — they run
    // even before the caret-null bail below. There is no UndoLog
    // selection/caret state (plan T4, queue #10 item 2): undoD2()/redoD2()
    // mutate the document and nothing else, so the caret is left where it
    // was and clampCaret (already wired generically through
    // onDocumentChanged) is the entire "never strand it" mechanism. Flush
    // rather than wait for the debounced d2DocumentChanged so the caret is
    // already valid by the time this handler returns — the document's own
    // synchronous flush, not a view-side defer (spec C2).
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) {
        // Read-only gate (P3.3, spec §4.2): this shortcut applies undoD2/
        // redoD2 directly and bypasses MarkdownView::undo()/redo() (which
        // already no-ops while read-only over the base store) — it needs
        // its own check, or Ctrl+Z would mutate a read-only document.
        if (m_doc && !m_readOnly) {
            m_selectionAnchor.reset();  // no UndoLog selection state (T4)
            if (event->matches(QKeySequence::Undo))
                m_doc->undoD2();
            else
                m_doc->redoD2();
            m_doc->flushPendingD2Changed();
            ensureCaretVisible();
            viewport()->update();
        }
        event->accept();
        return;
    }

    if (!m_doc || m_caret.block.isNull()) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    if (ctrl && event->key() == Qt::Key_A) {
        selectAll();
        event->accept();
        return;
    }

    if (ctrl && event->key() == Qt::Key_C) {
        copy();
        event->accept();
        return;
    }

    if (ctrl && event->key() == Qt::Key_X) {
        cut();
        event->accept();
        return;
    }

    if (ctrl && event->key() == Qt::Key_V) {
        // paste() carries its own read-only/empty-clipboard/no-caret checks
        // (P4.4) — same "the op checks itself, the shortcut just calls it"
        // shape as cut()/copy()/selectAll() above.
        paste();
        event->accept();
        return;
    }

    // A mutating key on a non-empty selection collapses it first (plan T5).
    // Backspace/Delete stop there — deleting the selection IS the whole
    // operation. Enter/printable fall through so the structural/insert
    // handling below runs at the post-collapse caret, same as typing at
    // any other empty-selection caret position.
    const bool isPrintable = Detail::isAcceptableTextInput(event);
    const bool isMutatingKey = event->key() == Qt::Key_Backspace
                             || event->key() == Qt::Key_Delete
                             || event->key() == Qt::Key_Return
                             || event->key() == Qt::Key_Enter
                             || isPrintable;
    // Read-only gate (P3.3, spec §4.2): every remaining mutation ingress —
    // selection-collapse-on-type, StructuralKeyHandler (Enter split,
    // boundary Backspace/Delete merge, Tab/Shift+Tab list indent),
    // in-block Backspace/Delete, and printable insertion — is swallowed
    // here in one place, before any of it runs. A superset of
    // `isMutatingKey` (adds Tab/Backtab, StructuralKeyHandler's list-indent
    // keys, which the selection-collapse block below deliberately excludes
    // — Tab-with-a-selection is not "collapse then type" today).
    // Navigation (arrow keys, Home/End, PageUp/Down, handled above/below
    // this point) and selection-extension (Shift+move, Ctrl+A, handled
    // above) are untouched: this block only ever matches keys that mutate.
    if (m_readOnly && (isMutatingKey || event->key() == Qt::Key_Tab
                       || event->key() == Qt::Key_Backtab)) {
        event->accept();
        return;
    }

    if (hasSelection() && isMutatingKey) {
        collapseSelection();
        if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
            ensureCaretVisible();
            viewport()->update();
            event->accept();
            return;
        }
    }

    // Structural keys (Enter split, boundary Backspace/Delete merge,
    // Tab/Shift+Tab list indent) are StructuralKeyHandler's call, not
    // this leaf's — it is the authority and has already applied the
    // mutation and placed the caret when this returns true (plan T3).
    if (tryStructuralKey(event)) {
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    }

    // Table cell navigation (P5.1): Tab/Shift+Tab next/previous cell, last-
    // cell Tab appends a row. Only "ours" when the caret is actually inside
    // a table — tryTableTab() returns false otherwise and this falls
    // through to the generic Tab handling below (none today; matches the
    // pre-P5.1 behavior of any other block kind). Key_Backtab is how Qt
    // usually delivers Shift+Tab; the modifier check covers platforms that
    // instead deliver Key_Tab with ShiftModifier set.
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
        const bool shift = event->key() == Qt::Key_Backtab
                         || event->modifiers().testFlag(Qt::ShiftModifier);
        if (tryTableTab(shift)) {
            event->accept();
            return;
        }
    }

    // Shift+move extends the selection (setting an anchor if there wasn't
    // one); plain move collapses any existing selection to a bare caret.
    switch (event->key()) {
    case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down:
    case Qt::Key_Home: case Qt::Key_End:
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            if (!m_selectionAnchor)
                m_selectionAnchor = m_caret;
        } else {
            m_selectionAnchor.reset();
        }
        break;
    default:
        break;
    }

    switch (event->key()) {
    case Qt::Key_Left:
        moveCaretHorizontally(false);
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    case Qt::Key_Right:
        moveCaretHorizontally(true);
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    case Qt::Key_Up:
        moveCaretVertically(false);
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    case Qt::Key_Down:
        moveCaretVertically(true);
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    case Qt::Key_Home:
        moveCaretToLineEdge(true);
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    case Qt::Key_End:
        moveCaretToLineEdge(false);
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    case Qt::Key_Backspace:
        deleteCluster(false);
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    case Qt::Key_Delete:
        deleteCluster(true);
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    default:
        break;
    }

    if (isPrintable) {
        insertPrintable(event->text());
        // Flush rather than wait for the debounced d2DocumentChanged (same
        // reasoning as the undo/redo path above): T6's kind-promotion check
        // runs from onDocumentChanged, and a typed "# " must read back as
        // Heading before this handler returns, not one event-loop spin
        // later.
        if (m_doc)
            m_doc->flushPendingD2Changed();
        ensureCaretVisible();
        viewport()->update();
        event->accept();
        return;
    }

    QAbstractScrollArea::keyPressEvent(event);
}

// ---- Links (P4.2) ---------------------------------------------------------

std::optional<Markoff::LinkActivation> View::linkActivationAt(
    const CanvasCursor &cursor, Qt::KeyboardModifiers mods) const
{
    if (!m_doc || cursor.block.isNull())
        return std::nullopt;

    const QByteArray text = m_doc->blockText(cursor.block);
    const int qcharPos = int(coords::byteToQtPos(text, cursor.byteOffset));

    for (const Markoff::SourceSpan &span : m_doc->inlineSpansFor(cursor.block)) {
        if (!(span.isLink || span.isWikilink || span.isTag))
            continue;
        // Half-open [charOffset, charOffset+charLength) — the same
        // convention every other span-covers-position check in this file
        // uses (isDelimiterHiddenAt above, InlineFormatting's format-range
        // build). A position exactly at charOffset+charLength (one past the
        // span's last QChar) belongs to whatever comes next, not this span
        // — this is the off-by-one the plan's falsification target lives
        // in; get it wrong either direction and the boundary QChar
        // mis-resolves.
        if (qcharPos < span.charOffset || qcharPos >= span.charOffset + span.charLength)
            continue;

        Markoff::LinkActivation a;
        a.modifiers   = mods;
        a.fromContext = m_linkFromContext;

        if (span.isWikilink) {
            a.kind       = Markoff::LinkKind::WikiLink;
            a.page       = span.linkTarget.page;
            a.section    = span.linkTarget.section;
            a.blockRef   = span.linkTarget.blockRef;
            a.alias      = span.linkTarget.alias;
            a.anchorHint = a.section;
            QString inner = a.page;
            if (!a.section.isEmpty())  inner += QLatin1Char('#') + a.section;
            if (!a.blockRef.isEmpty()) inner += QStringLiteral("#^") + a.blockRef;
            if (!a.alias.isEmpty())    inner += QLatin1Char('|') + a.alias;
            a.rawText = QStringLiteral("[[%1]]").arg(inner);
        } else if (span.isLink) {
            a.rawText = span.linkTarget.url;
            a.kind = m_linkService ? m_linkService->classify(a.rawText)
                                    : Markoff::LinkKind::Unknown;
        } else {  // isTag — no LinkTarget payload (parser sets isTag alone,
                  // see markoff-parser/src/TreeSitterParser.cpp); the span's
                  // own char range IS the tag text, "#" included.
            a.kind = Markoff::LinkKind::Tag;
            a.rawText = QString::fromUtf8(text).mid(span.charOffset, span.charLength);
        }

        a.resolvedTarget = m_linkService
            ? m_linkService->resolve(a.rawText, m_linkFromContext)
            : QUrl(a.rawText);
        return a;
    }
    return std::nullopt;
}

void View::setLinkService(Markoff::LinkService *service)
{
    m_linkService = service;
}

void View::setFromContext(const QString &context)
{
    m_linkFromContext = context;
}

void View::updateHover(const QPoint &viewportPos, const QPoint &globalPos)
{
    const CanvasCursor hit = m_doc ? hitTest(viewportPos) : CanvasCursor{};
    const auto act = hit.block.isNull()
        ? std::nullopt
        : linkActivationAt(hit, QGuiApplication::keyboardModifiers());
    const QString newRaw = act ? act->rawText : QString();

    if (newRaw == m_hoveredLinkRawText)
        return;  // no state transition — see doc comment (cursor-shape cache)

    if (!m_hoveredLinkRawText.isEmpty() && m_linkService)
        m_linkService->notifyHoverLeft(m_hoveredLinkRawText);
    m_hoveredLinkRawText = newRaw;

    if (act) {
        if (m_linkService)
            m_linkService->notifyHover(*act, globalPos);
        if (!m_cursorIsPointingHand) {
            viewport()->setCursor(Qt::PointingHandCursor);
            m_cursorIsPointingHand = true;
        }
    } else if (m_cursorIsPointingHand) {
        viewport()->unsetCursor();
        m_cursorIsPointingHand = false;
    }
}

void View::clearHover()
{
    if (!m_hoveredLinkRawText.isEmpty() && m_linkService)
        m_linkService->notifyHoverLeft(m_hoveredLinkRawText);
    m_hoveredLinkRawText.clear();
    if (m_cursorIsPointingHand) {
        viewport()->unsetCursor();
        m_cursorIsPointingHand = false;
    }
}

bool View::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == viewport() && event->type() == QEvent::Leave)
        clearHover();
    return QAbstractScrollArea::eventFilter(obj, event);
}

void View::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        int titleCharPos = 0;
        if (hitTestTitle(event->pos(), &titleCharPos)) {
            setFocus(Qt::MouseFocusReason);
            m_selectionAnchor.reset();
            m_titleCaretActive = true;
            m_titleCaretPos = qBound(0, titleCharPos, m_inlineTitle.size());
            viewport()->update();
            event->accept();
            return;
        }
        if (m_titleCaretActive) {
            // A click into the document area exits title-edit mode, same as
            // any other blur-by-click-elsewhere text field.
            m_titleCaretActive = false;
            viewport()->update();
        }

        // Frontmatter band (P5.5): read-only, so a click's only job is to
        // toggle collapsed/expanded — same "click reveals source" role
        // hitTestTitle's caret-entry plays for the title, adapted since
        // this band has no BlockId/text for a real caret to enter.
        if (hitTestFrontmatter(event->pos())) {
            m_frontmatterExpanded = !m_frontmatterExpanded;
            reflowKeepingScrollAnchor();
            viewport()->update();
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton && m_doc) {
        // Fold-affordance toggle (P5.6): checked ahead of the task-checkbox/
        // hitTest/caret paths, same "gutter click never reaches text
        // hit-testing" reasoning taskCheckboxAt's own comment gives —
        // foldAffordanceRect lives at a DIFFERENT x than taskCheckboxRect
        // (see its own doc comment), so there is no ambiguity between the
        // two either. Unlike the checkbox, folding is a pure VIEW-side
        // toggle (spec §2/§3: the document is untouched), so it is not
        // read-only-gated — collapsing/expanding a section is navigation,
        // not an edit, same as scroll/selection staying live in read-only
        // mode elsewhere in this file.
        if (const auto foldHead = foldAffordanceAt(event->pos())) {
            toggleFold(*foldHead);
            event->accept();
            return;
        }

        // Task-checkbox toggle (P4.7): checked ahead of hitTest/caret
        // placement — the glyph sits in the gutter left of contentX, which
        // hitTest's own text hit-testing never resolves to (negative
        // localX clamps into the text at byte 0), so there is no gesture
        // ambiguity to arbitrate the way link-activation-vs-caret has.
        // Gated read-only like every other mutation in this file (cut,
        // paste, format verbs): a plain click on a read-only document's
        // checkbox is a no-op, not a caret jump into the gutter.
        if (const auto taskId = taskCheckboxAt(event->pos())) {
            if (!m_readOnly) {
                m_doc->toggleListItemChecked(*taskId);
                viewport()->update();
            }
            event->accept();
            return;
        }

        const CanvasCursor hit = hitTest(event->pos());
        if (!hit.block.isNull()) {
            // Link activation (P4.2, spec §5.2): plain click while
            // read-only, Ctrl+click while editing — editing mode needs
            // plain click free for caret placement, read-only mode has no
            // conflicting use for it (navigation/selection still work via
            // drag, matching live/styled's read-only-keeps-working rule).
            // A miss (no link under the point) falls through to normal
            // caret placement either way.
            const bool activationGesture =
                m_readOnly || event->modifiers().testFlag(Qt::ControlModifier);
            if (activationGesture) {
                if (const auto act = linkActivationAt(hit, event->modifiers())) {
                    if (m_linkService)
                        m_linkService->activate(*act);
                    event->accept();
                    return;
                }
            }

            setFocus(Qt::MouseFocusReason);
            // Shift+click extends the existing (or a fresh) selection. A
            // plain click deliberately does NOT set an anchor here — only
            // mouseMoveEvent does, lazily, on the first move past this
            // press. A plain click with no drag must leave hasSelection()
            // false, or the next keystroke (typing, arrow-without-shift)
            // would see a stale one-point "selection" and misbehave.
            if (event->modifiers().testFlag(Qt::ShiftModifier) && !m_selectionAnchor)
                m_selectionAnchor = m_caret;
            else if (!event->modifiers().testFlag(Qt::ShiftModifier))
                m_selectionAnchor.reset();
            setCaret(hit);
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void View::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons().testFlag(Qt::LeftButton) && m_doc && !m_caret.block.isNull()) {
        const CanvasCursor hit = hitTest(event->pos());
        if (!hit.block.isNull()) {
            // First move past the press establishes the anchor at the
            // press point (already the caret, set in mousePressEvent).
            if (!m_selectionAnchor)
                m_selectionAnchor = m_caret;
            setCaret(hit);
            event->accept();
            return;
        }
    }

    if (event->buttons() == Qt::NoButton)
        updateHover(event->pos(), event->globalPosition().toPoint());

    QAbstractScrollArea::mouseMoveEvent(event);
}

void View::mouseReleaseEvent(QMouseEvent *event)
{
    // Caret/selection placement happens on press/move; nothing left to do
    // on release. Accept so it doesn't propagate as unhandled.
    event->accept();
}

void View::focusInEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusInEvent(event);
    m_hasFocus = true;
    viewport()->update();
}

void View::focusOutEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusOutEvent(event);
    m_hasFocus = false;
    viewport()->update();
}

void View::inputMethodEvent(QInputMethodEvent *event)
{
    if (!m_doc || m_caret.block.isNull()) {
        event->accept();
        return;
    }

    // Read-only gate (P3.3, spec §4.2) — the task's named falsification
    // target: IME composition never mutates the document while read-only.
    // Disabled outright (no preedit splice either) rather than letting
    // composition run and only blocking the eventual commit — a live
    // composition that can never commit is a worse UX than none at all,
    // and matches "cut disabled in its entirety" above.
    if (m_readOnly) {
        event->accept();
        return;
    }

    // Mirror QWidgetTextControlPrivate::inputMethodEvent's ordering (plan
    // T8): replacement + commit are a real document edit at the caret,
    // issued as one d2ApplyBufferEdit (replacementStart/Length relative to
    // the caret, converted to bytes); the preedit string that may follow is
    // never written to the document (see below).
    if (event->replacementLength() > 0 || !event->commitString().isEmpty()) {
        const QByteArray text = m_doc->blockText(m_caret.block);
        const qsizetype qlen = QString::fromUtf8(text).size();
        const qsizetype qcharPos = coords::byteToQtPos(text, m_caret.byteOffset);
        const qsizetype replStartQ = qBound(qsizetype(0), qcharPos + event->replacementStart(), qlen);
        const qsizetype replEndQ = qBound(replStartQ, replStartQ + event->replacementLength(), qlen);
        const int fromByte = int(coords::qtPosToByte(text, replStartQ));
        const int toByte = int(coords::qtPosToByte(text, replEndQ));
        const QByteArray commit = event->commitString().toUtf8();

        UndoLog::Transaction t(m_doc->d2UndoLog());
        m_doc->d2ApplyBufferEdit(m_caret.block, uint32_t(fromByte),
                                 uint32_t(toByte - fromByte), commit, t);
        m_caret.byteOffset = fromByte + commit.size();
        // Kind promotion (T6) must see the committed text before this
        // handler returns, same reasoning as the printable-key path.
        m_doc->flushPendingD2Changed();
    }

    // Empty commit + empty preedit = cancelled composition: clear the
    // preedit area, no document change (already skipped above).
    m_preeditText = event->preeditString();
    if (m_preeditText.isEmpty()) {
        m_cache->clearPreedit(*m_doc, m_theme);
    } else {
        // Byte offset (this block's coordinate space, C4) — the cache
        // resolves it to a layout position itself, against the projection
        // it rebuilds for this caret position (spec §4.2).
        m_cache->setPreedit(*m_doc, m_theme, m_caret.block, m_caret.byteOffset, m_preeditText);
    }

    ensureCaretVisible();
    viewport()->update();
    event->accept();
}

QVariant View::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (!m_doc || m_caret.block.isNull())
        return {};

    const QByteArray text = m_doc->blockText(m_caret.block);

    switch (query) {
    case Qt::ImCursorRectangle:
        return caretRectInViewport();
    case Qt::ImSurroundingText:
        return QString::fromUtf8(text);
    case Qt::ImCursorPosition:
        return int(coords::byteToQtPos(text, m_caret.byteOffset));
    case Qt::ImHints:
        return int(Qt::ImhNone);
    default:
        return {};
    }
}

void View::paintTable(QPainter &p, int entryIndex, qreal blockTop, qreal contentX) const
{
    const auto &e = m_cache->entries()[size_t(entryIndex)];
    if (e.tableCols <= 0 || e.tableCells.empty())
        return;  // unrealized (outside the realized margin) or malformed

    const int rows = int(e.tableCells.size()) / e.tableCols;
    const QColor gridColor = m_theme.color(Theme::Slot::Quote);
    QFont headerFont = e.style.font;
    headerFont.setBold(true);

    // Cell-granularity selection tint (plan P2.3): a selection touching
    // this table block covers whole cells via the row-major cell-ordered
    // sequence, not individual characters — see coveredCellRange's comment.
    int selLo = -1, selHi = -1;
    if (const auto sel = orderedSelection()) {
        const auto &[start, end] = *sel;
        const auto [fromByte, toByte] = selectedByteRangeInBlock(e.id, start, end);
        std::tie(selLo, selHi) = coveredCellRange(e, fromByte, toByte);
    }

    qreal rowY = blockTop;
    for (int r = 0; r < rows; ++r) {
        qreal colX = contentX;
        const qreal rowH = e.tableRowHeights[size_t(r)];
        for (int c = 0; c < e.tableCols; ++c) {
            const qreal colW = e.tableColWidths[size_t(c)];
            const QRectF cellRect(colX, rowY, colW, rowH);

            const int cellIdx = r * e.tableCols + c;
            if (selLo >= 0 && cellIdx >= selLo && cellIdx <= selHi)
                p.fillRect(cellRect, m_theme.color(Theme::Slot::SelectionBackground));

            p.setPen(QPen(gridColor, 1));
            p.drawRect(cellRect);

            const auto &cell = e.tableCells[size_t(r) * size_t(e.tableCols) + size_t(c)];
            if (cell.layout) {
                const QPointF textPos(cellRect.x() + kTableCellPadding,
                                      cellRect.y() + kTableCellPadding);
                p.setPen(e.style.foreground);
                p.setFont(r == 0 ? headerFont : e.style.font);
                cell.layout->draw(&p, textPos);

                if (m_hasFocus && e.id == m_caret.block
                    && m_caret.byteOffset >= cell.startByte
                    && m_caret.byteOffset <= cell.endByte) {
                    const int qcharPos = cell.projection.byteToLayoutQChar(
                        m_caret.byteOffset - cell.startByte);
                    p.setPen(m_theme.color(Theme::Slot::CursorPrimary));
                    cell.layout->drawCursor(&p, textPos, qcharPos);
                }
            }
            colX += colW;
        }
        rowY += rowH;
    }
}

void View::paintEvent(QPaintEvent *event)
{
    ++m_paintCount;

    QPainter p(viewport());
    p.fillRect(event->rect(), m_theme.color(Theme::Slot::EditorBackground));

    if (!m_doc)
        return;

    // Realizing here keeps "everything visible is realized" true in one
    // place, whatever caused the paint. It cannot recurse: realization only
    // ever turns unrealized into realized, and paints are posted, not
    // re-entered.
    ensureLayoutForViewport();

    // Leading non-block entries (P4.9 title, P5.5 frontmatter), painted
    // before the block loop below — share the content column but are never
    // among BlockLayoutCache's entries.
    paintTitle(p);
    paintFrontmatter(p);

    const qreal scrollY    = verticalScrollBar()->value();
    const qreal viewBottom = scrollY + viewport()->height();
    const qreal margin     = pageMargin();
    const auto  sel        = orderedSelection();
    const qreal titleH     = leadingBandHeight();

    for (size_t entryIndex = 0; entryIndex < m_cache->entries().size(); ++entryIndex) {
        const auto &e = m_cache->entries()[entryIndex];
        // Folding (P5.6): a folded entry contributes 0 to the y-layout
        // (BlockLayoutCache::recomputePositions) but its OWN `height`
        // field stays the real content height (Entry::folded's own doc
        // comment) — so it must never be painted, or it would draw on top
        // of whatever now occupies its old y. This is the one place that
        // distinction matters: every check below it still uses the
        // unmodified `e.height`, which is correct for entries that DO
        // reach this point (they're never folded).
        if (e.folded)
            continue;
        if (e.y + titleH >= viewBottom)
            break;
        if (e.y + titleH + e.height <= scrollY)
            continue;

        const qreal blockTop = e.y + titleH - scrollY;
        const qreal contentX = margin + e.style.leftIndent;
        const qreal contentY = blockTop + e.style.topMargin;

        // Fold affordance (P5.6): a small chevron at the page margin's
        // left edge for any block CURRENTLY heading a foldable unit —
        // independent of table/rule/image/etc. branches below (drawn
        // before all of them, same "gutter decoration first" order the
        // quote bar/callout header already follow), so every foldable
        // kind gets it without duplicating this block per branch.
        if (isBlockFoldable(e.id)) {
            const QFontMetricsF gfm(e.style.font);
            const QRectF glyph = foldAffordanceRect(gfm, margin, contentY);
            QPainterPath tri;
            if (m_foldedHeads.contains(e.id)) {
                // Collapsed: right-pointing chevron ">".
                tri.moveTo(glyph.left(), glyph.top());
                tri.lineTo(glyph.right(), glyph.center().y());
                tri.lineTo(glyph.left(), glyph.bottom());
            } else {
                // Expanded: down-pointing chevron "v".
                tri.moveTo(glyph.left(), glyph.top());
                tri.lineTo(glyph.right(), glyph.top());
                tri.lineTo(glyph.center().x(), glyph.bottom());
            }
            tri.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(e.style.foreground);
            p.drawPath(tri);
        }

        if (e.style.background.isValid()) {
            const qreal bgX = e.style.fullWidthBackground ? margin : contentX;
            p.fillRect(QRectF(bgX, blockTop, margin + textWidth() - bgX, e.height),
                       e.style.background);
        }

        if (e.style.hasQuoteBar) {
            p.fillRect(QRectF(contentX - kMarkerGap - kQuoteBarWidth, blockTop,
                              kQuoteBarWidth, e.height),
                       e.style.foreground);
        }

        // Callout typed header (P5.5): painted into the band presentationFor
        // reserved by inflating style.topMargin — sits directly above
        // contentY (== blockTop + style.topMargin), so it never overlaps
        // the block's own text layout.
        if (e.style.isCallout) {
            QFont headerFont = e.style.font;
            headerFont.setBold(true);
            const QFontMetricsF hfm(headerFont);
            p.setFont(headerFont);
            p.setPen(e.style.foreground);
            const QString header = e.style.calloutIcon + QStringLiteral(" ")
                                  + e.style.calloutLabel;
            p.drawText(QPointF(contentX, blockTop + hfm.ascent() + hfm.height() * 0.2),
                       header);
        }

        if (e.style.isRule) {
            const qreal midY = blockTop + e.height / 2;
            p.setPen(QPen(e.style.foreground, 1));
            p.drawLine(QPointF(margin, midY), QPointF(margin + textWidth(), midY));
            continue;
        }

        if (e.style.isTable) {
            paintTable(p, int(entryIndex), blockTop, contentX);
            continue;
        }

        if (!e.layout)
            continue;  // unrealized: outside the realized margin

        // Image / Embed (P5.4): both are BlockKind::Image forms
        // (MediaBlocks::parseImageBlock's isEmbed split; see
        // BlockStyle::isImageBlock/isEmbedBlock's own doc comments for why
        // the two are mutually exclusive). A resolved image pixmap paints
        // centered in the content column, same as Math's own centering
        // below; anything else (no lookup, a lookup miss, or any embed —
        // embeds are always placeholder this task) paints a dashed
        // placeholder box with the parsed target/label text.
        if (e.style.isImageBlock || e.style.isEmbedBlock) {
            if (!e.imagePixmap.isNull()) {
                const qreal dpr = qMax(1.0, e.imagePixmap.devicePixelRatio());
                const qreal pw  = e.imagePixmap.width() / dpr;
                const qreal avail = margin + textWidth() - contentX;
                const qreal px = contentX + qMax(qreal(0), (avail - pw) / 2.0);
                p.drawPixmap(QPointF(px, contentY), e.imagePixmap);
            } else {
                const QRectF box(contentX, contentY, textWidth(), kMediaPlaceholderHeight);
                p.setPen(QPen(e.style.foreground, 1.0, Qt::DashLine));
                p.setBrush(m_theme.color(Theme::Slot::CodeBlockBackground));
                p.drawRoundedRect(box, 4.0, 4.0);
                p.setPen(e.style.foreground);
                p.drawText(box, Qt::AlignCenter | Qt::TextWordWrap,
                           e.mediaLabel.isEmpty() ? QStringLiteral("[no target]")
                                                   : e.mediaLabel);
            }
            continue;
        }

        // Mermaid (P5.4): a non-null mermaidPixmap means the caret is not
        // in this block AND the injected MermaidRenderer produced a
        // pixmap for it — same centering as the Image/Math pixmap paths.
        // Caret-in-block, no renderer, or a render failure all fall
        // through to the normal code-block text layout below (no
        // placeholder box for mermaid — see mermaidPixmap's own doc
        // comment).
        if (!e.mermaidPixmap.isNull()) {
            const qreal dpr = qMax(1.0, e.mermaidPixmap.devicePixelRatio());
            const qreal pw  = e.mermaidPixmap.width() / dpr;
            const qreal avail = margin + textWidth() - contentX;
            const qreal px = contentX + qMax(qreal(0), (avail - pw) / 2.0);
            p.drawPixmap(QPointF(px, contentY), e.mermaidPixmap);
            continue;
        }

        // Display Math (P5.3): a non-null mathPixmap means the caret is
        // NOT in this block (BlockLayoutCache::rebuildInline only builds
        // one under that exact condition) — paint the rendered LaTeX
        // instead of the raw-source text layout, centered in the content
        // column. Caret-in-block paints nothing here (mathPixmap is null
        // then) and falls through to the normal text-layout path below,
        // which is the "reveal source" state — same per-block switch shape
        // as the code-fence reveal, just swapping which surface is drawn
        // rather than which text is shown.
        if (!e.mathPixmap.isNull()) {
            const qreal dpr = qMax(1.0, e.mathPixmap.devicePixelRatio());
            const qreal pw = e.mathPixmap.width() / dpr;
            const qreal avail = margin + textWidth() - contentX;
            const qreal px = contentX + qMax(qreal(0), (avail - pw) / 2.0);
            p.drawPixmap(QPointF(px, contentY), e.mathPixmap);
            continue;
        }

        if (e.style.isTaskItem) {
            // Checkbox glyph (P4.7): same decoration slot list bullets use
            // (left of contentX, kMarkerGap-gapped), but a drawn box+check
            // rather than measured bracket text — taskCheckboxRect is the
            // one function that decides where it goes, shared with
            // taskCheckboxAt's hit-test so the clickable area always
            // matches what's on screen.
            const QFontMetricsF fm(e.style.font);
            const QRectF box = taskCheckboxRect(fm, contentX, contentY);
            p.setPen(QPen(e.style.foreground, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(box, 2.0, 2.0);
            if (e.style.taskChecked) {
                QPainterPath check;
                check.moveTo(box.left() + box.width() * 0.18, box.top() + box.height() * 0.55);
                check.lineTo(box.left() + box.width() * 0.42, box.top() + box.height() * 0.78);
                check.lineTo(box.left() + box.width() * 0.84, box.top() + box.height() * 0.22);
                p.strokePath(check, QPen(e.style.foreground, 1.6));
            }
        } else if (!e.style.marker.isEmpty()) {
            const QFontMetricsF fm(e.style.font);
            p.setPen(e.style.foreground);
            p.setFont(e.style.font);
            p.drawText(QPointF(contentX - kMarkerGap - fm.horizontalAdvance(e.style.marker),
                               contentY + fm.ascent()),
                       e.style.marker);
        }

        QList<QTextLayout::FormatRange> selections;
        if (sel) {
            const auto &[start, end] = *sel;
            const auto [fromByte, toByte] = selectedByteRangeInBlock(e.id, start, end);
            if (toByte > fromByte) {
                // Snap rule (spec §4.2): a selection start landing inside a
                // hidden run snaps left (shrink toward the visible content),
                // its end snaps right (grow to cover it) — the range never
                // shrinks to nothing just because its edge sat inside an
                // omitted run.
                const int qFrom = e.projection.byteToLayoutQChar(
                    fromByte, ProjectionMap::SnapDirection::Left);
                const int qTo = e.projection.byteToLayoutQChar(
                    toByte, ProjectionMap::SnapDirection::Right);
                if (qTo > qFrom) {
                    QTextCharFormat fmt;
                    fmt.setBackground(m_theme.color(Theme::Slot::SelectionBackground));
                    selections.push_back({qFrom, qTo - qFrom, fmt});
                }
            }
        }

        // Find-match highlights (P3.4): same draw-time FormatRange
        // mechanism as selection above, never a layout format mutation.
        // Current match gets its own slot (distinct visual weight) from
        // the other matches.
        if (const auto it = m_findHighlightsByBlock.constFind(e.id);
            it != m_findHighlightsByBlock.constEnd()) {
            for (const FindHighlight &h : it.value()) {
                const int qFrom = e.projection.byteToLayoutQChar(
                    h.byteOffset, ProjectionMap::SnapDirection::Left);
                const int qTo = e.projection.byteToLayoutQChar(
                    h.byteOffset + h.byteLength, ProjectionMap::SnapDirection::Right);
                if (qTo > qFrom) {
                    QTextCharFormat fmt;
                    fmt.setBackground(m_theme.color(h.isCurrent
                        ? Theme::Slot::SearchActiveMatchBackground
                        : Theme::Slot::SearchMatchBackground));
                    selections.push_back({qFrom, qTo - qFrom, fmt});
                }
            }
        }

        p.setPen(e.style.foreground);
        e.layout->draw(&p, QPointF(contentX, contentY), selections);

        if (m_hasFocus && e.id == m_caret.block) {
            const int layoutPos = e.projection.byteToLayoutQChar(m_caret.byteOffset);
            p.setPen(m_theme.color(Theme::Slot::CursorPrimary));
            e.layout->drawCursor(&p, QPointF(contentX, contentY), layoutPos);
        }
    }
}

}  // namespace Markoff::Canvas
