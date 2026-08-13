// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/canvas/View.h>

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QtMath>

#include <markoff/core/MarkoffDocument.h>

#include "BlockLayoutCache.h"

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
        const bool realized = m_cache->realizeRange(*m_doc, top - height,
                                                    top + 2 * height);
        updateScrollRange();
        if (!realized && verticalScrollBar()->value() == top)
            break;
    }
}

void View::onDocumentChanged()
{
    if (m_doc) {
        m_cache->setTextWidth(textWidth());
        m_cache->sync(*m_doc, m_theme);
    }
    ensureLayoutForViewport();
    viewport()->update();
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

    // T1 is read-only, so the arrow keys scroll. T2 takes them back for
    // caret motion and leaves only the page/document keys here.
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
    case Qt::Key_Down:
        vbar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
        event->accept();
        return;
    case Qt::Key_Up:
        vbar->triggerAction(QAbstractSlider::SliderSingleStepSub);
        event->accept();
        return;
    default:
        break;
    }

    QAbstractScrollArea::keyPressEvent(event);
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

    for (const auto &e : m_cache->entries()) {
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

        p.setPen(e.style.foreground);
        e.layout->draw(&p, QPointF(contentX, contentY));
    }
}

}  // namespace Markoff::Canvas
