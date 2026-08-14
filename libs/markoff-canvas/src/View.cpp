// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/canvas/View.h>

#include <QByteArrayList>
#include <QClipboard>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QtMath>

#include <markoff/core/AttrNames.h>
#include <markoff/core/KindInference.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/StructuralKeyHandler.h>
#include <markoff/core/UndoLog.h>

#include "BlockLayoutCache.h"
#include "Coordinates.h"
#include "InputPredicate.h"

namespace coords = Markoff::Canvas::Detail::Coordinates;

namespace Markoff::Canvas {

namespace {
/// Page margin, in device-independent pixels, either side of the text column.
constexpr qreal kPageMargin = 16.0;
/// Gap between a list marker (or quote bar) and its content.
constexpr qreal kMarkerGap = 6.0;
/// Width of the blockquote bar.
constexpr qreal kQuoteBarWidth = 3.0;
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

    const QColor invisible = e.style.background.isValid()
                            ? e.style.background
                            : m_theme.color(Theme::Slot::EditorBackground);

    for (const QTextLayout::FormatRange &r : e.layout->formats()) {
        if (qchar >= r.start && qchar < r.start + r.length) {
            return r.format.hasProperty(QTextFormat::ForegroundBrush)
                && r.format.foreground().color() == invisible;
        }
    }
    return false;
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

qreal View::pageMargin() const
{
    return kPageMargin;
}

qreal View::textWidth() const
{
    return qMax(qreal(1), viewport()->width() - 2 * pageMargin());
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
        m_cache->sync(*m_doc, m_theme);
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
    const QByteArray text = m_doc->blockText(e.id);
    const int byteOff = int(coords::qtPosToByte(text, qcharPos));
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

    const QByteArray cellText = m_doc->blockText(e.id).mid(
        cell.startByte, cell.endByte - cell.startByte);
    const int byteOff = cell.startByte + int(coords::qtPosToByte(cellText, qcharPos));
    return CanvasCursor{e.id, byteOff};
}

void View::setCaret(const CanvasCursor &caret)
{
    m_caret = caret;
    ensureCaretVisible();
    viewport()->update();
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
        parts << m_doc->blockText(e.id).mid(from, to - from);
    }
    return parts.join("\n\n");
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

    const QByteArray text = m_doc->blockText(m_caret.block);
    const QString qtext = QString::fromUtf8(text);
    const int qcharPos = int(coords::byteToQtPos(text, m_caret.byteOffset));

    if (forward) {
        if (qcharPos >= qtext.size())
            return;  // at block end: T3's job (merge with next block)
        QTextLayout layout(qtext);
        const int next = layout.nextCursorPosition(qcharPos);
        const int removed = int(coords::qtPosToByte(text, next))
                           - m_caret.byteOffset;
        UndoLog::Transaction t(m_doc->d2UndoLog());
        m_doc->d2ApplyBufferEdit(m_caret.block, uint32_t(m_caret.byteOffset),
                                 uint32_t(removed), QByteArray(), t);
    } else {
        if (qcharPos <= 0)
            return;  // at block start: T3's job (merge with previous block)
        QTextLayout layout(qtext);
        const int prev = layout.previousCursorPosition(qcharPos);
        const int prevByte = int(coords::qtPosToByte(text, prev));
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

    const QByteArray text = m_doc->blockText(m_caret.block);
    const QString qtext = QString::fromUtf8(text);
    const int qcharPos = int(coords::byteToQtPos(text, m_caret.byteOffset));

    if (forward) {
        if (qcharPos >= qtext.size()) {
            const int idx = m_cache->indexOf(m_caret.block);
            if (idx >= 0 && idx + 1 < int(m_cache->entries().size())) {
                m_caret.block = m_cache->entries()[size_t(idx + 1)].id;
                m_caret.byteOffset = 0;
            }
            return;
        }
        QTextLayout layout(qtext);
        const int next = layout.nextCursorPosition(qcharPos);
        m_caret.byteOffset = int(coords::qtPosToByte(text, next));
    } else {
        if (qcharPos <= 0) {
            const int idx = m_cache->indexOf(m_caret.block);
            if (idx > 0) {
                const auto &prevEntry = m_cache->entries()[size_t(idx - 1)];
                m_caret.block = prevEntry.id;
                m_caret.byteOffset = m_doc->blockText(prevEntry.id).size();
            }
            return;
        }
        QTextLayout layout(qtext);
        const int prev = layout.previousCursorPosition(qcharPos);
        m_caret.byteOffset = int(coords::qtPosToByte(text, prev));
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

    const QByteArray text = m_doc->blockText(m_caret.block);
    const int qcharPos = int(coords::byteToQtPos(text, m_caret.byteOffset));
    const QTextLine line = e.layout->lineForTextPosition(qcharPos);
    if (!line.isValid())
        return;

    const int edgeQChar = home ? line.textStart() : line.textStart() + line.textLength();
    m_caret.byteOffset = int(coords::qtPosToByte(text, edgeQChar));
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

    const QByteArray text = m_doc->blockText(m_caret.block);
    const int qcharPos = int(coords::byteToQtPos(text, m_caret.byteOffset));
    const QTextLine curLine = e.layout->lineForTextPosition(qcharPos);
    const int lineNo = curLine.isValid() ? curLine.lineNumber() : 0;
    const qreal x = curLine.isValid() ? curLine.cursorToX(qcharPos) : 0;

    const int targetLineNo = lineNo + (forward ? 1 : -1);
    if (targetLineNo >= 0 && targetLineNo < e.layout->lineCount()) {
        const QTextLine targetLine = e.layout->lineAt(targetLineNo);
        const int newQChar = targetLine.xToCursor(x);
        m_caret.byteOffset = int(coords::qtPosToByte(text, newQChar));
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
    if (!target.layout || target.layout->lineCount() == 0) {
        m_caret.byteOffset = 0;
        return;
    }
    const QTextLine edgeLine = target.layout->lineAt(forward ? 0 : target.layout->lineCount() - 1);
    const int newQChar = edgeLine.xToCursor(x);
    const QByteArray targetText = m_doc->blockText(target.id);
    m_caret.byteOffset = int(coords::qtPosToByte(targetText, newQChar));
}

// ---- Events -------------------------------------------------------------

void View::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    ensureLayoutForViewport();
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
        if (m_doc) {
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
        if (!m_cache->entries().empty()) {
            const auto &first = m_cache->entries().front();
            const auto &last = m_cache->entries().back();
            m_selectionAnchor = CanvasCursor{first.id, 0};
            setCaret(CanvasCursor{last.id, int(m_doc->blockText(last.id).size())});
        }
        event->accept();
        return;
    }

    if (ctrl && (event->key() == Qt::Key_C || event->key() == Qt::Key_X)) {
        if (hasSelection()) {
            QGuiApplication::clipboard()->setText(QString::fromUtf8(selectedText()));
            if (event->key() == Qt::Key_X) {
                collapseSelection();
                ensureCaretVisible();
                viewport()->update();
            }
        }
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

void View::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_doc) {
        const CanvasCursor hit = hitTest(event->pos());
        if (!hit.block.isNull()) {
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
        const QByteArray text = m_doc->blockText(m_caret.block);
        const qsizetype qcharPos = coords::byteToQtPos(text, m_caret.byteOffset);
        m_cache->setPreedit(*m_doc, m_theme, m_caret.block, int(qcharPos), m_preeditText);
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
    case Qt::ImCursorRectangle: {
        const int idx = m_cache->indexOf(m_caret.block);
        if (idx < 0)
            return {};
        const auto &e = m_cache->entries()[size_t(idx)];
        if (!e.layout)
            return {};
        const qsizetype qcharPos = coords::byteToQtPos(text, m_caret.byteOffset);
        const QTextLine line = e.layout->lineForTextPosition(int(qcharPos));
        if (!line.isValid())
            return {};
        const qreal scrollY = verticalScrollBar()->value();
        const qreal contentX = pageMargin() + e.style.leftIndent;
        const qreal contentY = (e.y - scrollY) + e.style.topMargin;
        return QRectF(contentX + line.cursorToX(int(qcharPos)), contentY + line.y(),
                     1, line.height()).toRect();
    }
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

    qreal rowY = blockTop;
    for (int r = 0; r < rows; ++r) {
        qreal colX = contentX;
        const qreal rowH = e.tableRowHeights[size_t(r)];
        for (int c = 0; c < e.tableCols; ++c) {
            const qreal colW = e.tableColWidths[size_t(c)];
            const QRectF cellRect(colX, rowY, colW, rowH);
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
                    const QByteArray cellText = m_doc->blockText(e.id).mid(
                        cell.startByte, cell.endByte - cell.startByte);
                    const int qcharPos = int(coords::byteToQtPos(
                        cellText, m_caret.byteOffset - cell.startByte));
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
                const QByteArray blockText = m_doc->blockText(e.id);
                const int qFrom = int(coords::byteToQtPos(blockText, fromByte));
                const int qTo   = int(coords::byteToQtPos(blockText, toByte));
                QTextCharFormat fmt;
                fmt.setBackground(m_theme.color(Theme::Slot::SelectionBackground));
                selections.push_back({qFrom, qTo - qFrom, fmt});
            }
        }

        p.setPen(e.style.foreground);
        e.layout->draw(&p, QPointF(contentX, contentY), selections);

        if (m_hasFocus && e.id == m_caret.block) {
            const QByteArray blockText = m_doc->blockText(e.id);
            const int qcharPos = int(coords::byteToQtPos(blockText, m_caret.byteOffset));
            p.setPen(m_theme.color(Theme::Slot::CursorPrimary));
            e.layout->drawCursor(&p, QPointF(contentX, contentY), qcharPos);
        }
    }
}

}  // namespace Markoff::Canvas
