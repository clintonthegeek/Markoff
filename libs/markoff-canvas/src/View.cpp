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
#include <markoff/core/StructuralKeyHandler.h>
#include <markoff/core/TextUnits.h>
#include <markoff/core/UndoLog.h>

#include <markoff/canvas/CanvasActionController.h>

#include "BlockLayoutCache.h"
#include "InlineFormatting.h"
#include "InputPredicate.h"
#include "ProjectionMap.h"

namespace coords = Markoff::TextUnits;

namespace Markoff::Canvas {

namespace {
/// Page margin, in device-independent pixels, either side of the text column.
constexpr qreal kPageMargin = 16.0;
/// Gap between a list marker (or quote bar) and its content.
constexpr qreal kMarkerGap = 6.0;
/// Width of the blockquote bar.
constexpr qreal kQuoteBarWidth = 3.0;

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
        const int idx = m_cache->indexAtY(verticalScrollBar()->value());
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
    }

    // Re-derive the scrollbar's range for the new (estimated) heights
    // before restoring position — setValue() below would otherwise clamp
    // against the stale (pre-clear) range.
    updateScrollRange();
    if (m_doc && !anchorBlock.isNull()) {
        const int idx = m_cache->indexOf(anchorBlock);
        if (idx >= 0)
            verticalScrollBar()->setValue(qRound(m_cache->entries()[size_t(idx)].y));
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
            verticalScrollBar()->setValue(qRound(m_cache->entries()[size_t(idx)].y));
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
    const qreal contentY = (e.y - scrollY) + e.style.topMargin;
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
    return m_cache->totalHeight();
}

QRectF View::blockRect(BlockId id) const
{
    const int i = m_cache->indexOf(id);
    if (i < 0)
        return {};
    const auto &e = m_cache->entries()[size_t(i)];
    return QRectF(pageMargin(), e.y, textWidth(), e.height);
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
    qreal y = e.y + e.style.topMargin;
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

void View::updateScrollRange()
{
    const int viewportH = viewport()->height();
    const int max = qMax(0, qCeil(m_cache->totalHeight()) - viewportH);

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
        // Realize the viewport plus one viewport-height either side, so a
        // scroll of up to a full page never exposes an unrealized block.
        const bool realized = m_cache->realizeRange(*m_doc, m_theme, top - height,
                                                    top + 2 * height);
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
    const int idx = m_cache->indexAtY(docY);
    if (idx < 0)
        return {};

    const auto &e = m_cache->entries()[size_t(idx)];
    if (e.style.isTable)
        return hitTestTable(idx, viewportPos, scrollY);
    if (!e.layout || e.style.isRule)
        return CanvasCursor{e.id, 0};

    const qreal contentX = pageMargin() + e.style.leftIndent;
    const qreal contentY = (e.y - scrollY) + e.style.topMargin;
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

CanvasCursor View::hitTestTable(int entryIndex, const QPoint &viewportPos, qreal scrollY) const
{
    const auto &e = m_cache->entries()[size_t(entryIndex)];
    if (e.tableCols <= 0 || e.tableCells.empty())
        return CanvasCursor{e.id, 0};

    const int rows = int(e.tableCells.size()) / e.tableCols;
    const qreal contentX = pageMargin() + e.style.leftIndent;
    const qreal contentY = (e.y - scrollY) + e.style.topMargin;
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
    const QTextLine line = cell.layout->lineAt(0);
    const int qcharPos = line.isValid() ? line.xToCursor(cellLocalX) : 0;

    const int byteOff = cell.startByte + cell.projection.layoutQCharToByte(qcharPos);
    return CanvasCursor{e.id, byteOff};
}

void View::setCaret(const CanvasCursor &caret)
{
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

    const qreal value = qreal(verticalScrollBar()->value());
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
    const qreal target = e.y + qBound(0.0f, fraction, 1.0f) * e.height;
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

    if (e.y < vbar->value())
        vbar->setValue(qFloor(e.y));
    else if (e.y + e.height > vbar->value() + viewportH)
        vbar->setValue(qCeil(e.y + e.height - viewportH));

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
            if (idx + 1 < int(m_cache->entries().size())) {
                m_caret.block = m_cache->entries()[size_t(idx + 1)].id;
                m_caret.byteOffset = 0;
            }
            return;
        }
        const int next = e.layout->nextCursorPosition(layoutPos);
        m_caret.byteOffset = e.projection.layoutQCharToByte(next);
    } else {
        if (layoutPos <= 0) {
            if (idx > 0) {
                const auto &prevEntry = m_cache->entries()[size_t(idx - 1)];
                m_caret.block = prevEntry.id;
                m_caret.byteOffset = m_doc->blockText(prevEntry.id).size();
            }
            return;
        }
        const int prev = e.layout->previousCursorPosition(layoutPos);
        m_caret.byteOffset = e.projection.layoutQCharToByte(prev);
    }
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
    // line. Exact column affinity is not a criterion (plan T2).
    const int nextIdx = idx + (forward ? 1 : -1);
    if (nextIdx < 0 || nextIdx >= int(m_cache->entries().size()))
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
    if (event->button() == Qt::LeftButton && m_doc) {
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

    const qreal scrollY    = verticalScrollBar()->value();
    const qreal viewBottom = scrollY + viewport()->height();
    const qreal margin     = pageMargin();
    const auto  sel        = orderedSelection();

    for (size_t entryIndex = 0; entryIndex < m_cache->entries().size(); ++entryIndex) {
        const auto &e = m_cache->entries()[entryIndex];
        if (e.y >= viewBottom)
            break;
        if (e.y + e.height <= scrollY)
            continue;

        const qreal blockTop = e.y - scrollY;
        const qreal contentX = margin + e.style.leftIndent;
        const qreal contentY = blockTop + e.style.topMargin;

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

        if (!e.style.marker.isEmpty()) {
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
