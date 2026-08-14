// SPDX-License-Identifier: GPL-3.0-or-later
#include "BlockLayoutCache.h"

#include <algorithm>

#include <QFontMetricsF>
#include <QTextOption>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/TextUnits.h>
#include <markoff/core/Theme.h>
#include <markoff/parser/SourceSpan.h>

#include "InlineFormatting.h"
#include "TableGeometry.h"

namespace coords = Markoff::TextUnits;

namespace Markoff::Canvas {

namespace {

/// Layout text for a block. Content-only: the ListItem marker is painted
/// as decoration and is deliberately NOT part of this string, so QChar
/// indices into it map to buffer bytes with no marker fudge-factor.
///
/// QTextLayout does not treat '\n' as a line break — it only breaks where
/// createLine() runs out of width, or at QChar::LineSeparator. A code
/// block would otherwise render as one run-on line. The substitution is
/// deliberately 1 QChar → 1 QChar so that QChar indices into this string
/// still correspond one-for-one with QChar indices into the block's real
/// text.
///
/// T2 trap, stated here because this is where the divergence is created:
/// the byte↔QChar helper must convert against the block's ACTUAL text
/// (doc.blockText(id)), never against this layout string. U+2028 is one
/// byte as '\n' but three bytes as itself, so round-tripping this string
/// through toUtf8() yields byte offsets that are wrong by two per
/// preceding newline.
QString layoutTextFor(const MarkoffDocument &doc, BlockId id)
{
    QString text = QString::fromUtf8(doc.blockText(id));
    text.replace(QLatin1Char('\n'), QChar::LineSeparator);
    return text;
}

}  // namespace

void BlockLayoutCache::setTextWidth(qreal width)
{
    if (qFuzzyCompare(m_textWidth, width))
        return;
    m_textWidth = width;
    // Wrapping changed under every layout; keep the order and the
    // estimates, drop the realizations.
    for (Entry &e : m_entries) {
        e.layout.reset();
        e.realized = false;
    }
    recomputePositions();
}

void BlockLayoutCache::clear()
{
    m_entries.clear();
    m_index.clear();
    m_totalHeight = 0;
    m_structuralSeq = 0;
    m_caretBlock = BlockId();
    m_caretByte = -1;
    m_preeditBlock = BlockId();
    m_preeditQCharPos = -1;
    m_preeditText.clear();
}

void BlockLayoutCache::sync(const MarkoffDocument &doc, const Theme &theme)
{
    // A structural edit (insert/remove/change-kind) can move any block's
    // kind, attrs, and therefore its font — and blockEditSequence does not
    // bump for those (it counts buffer edits only). Restyle everything in
    // that case. Content-only typing leaves this untouched, which is the
    // case that has to stay cheap.
    const quint64 structural = doc.structuralEditSequence();
    const bool restyleAll = (structural != m_structuralSeq);
    m_structuralSeq = structural;

    const std::vector<BlockId> order = doc.iterateBlocks();

    std::vector<Entry> next;
    next.reserve(order.size());
    QHash<BlockId, int> nextIndex;
    nextIndex.reserve(int(order.size()));

    for (const BlockId id : order) {
        Entry e;
        e.id = id;

        const auto old = m_index.constFind(id);
        if (old != m_index.cend())
            e = std::move(m_entries[*old]);

        const quint64 seq = doc.blockEditSequence(id);
        const bool stale = (!e.realized && e.height <= 0)  // never measured
                        || e.seq != seq
                        || restyleAll;

        if (stale) {
            e.layout.reset();
            e.realized = false;
            e.seq      = seq;
            e.style    = presentationFor(doc, id, theme);
            e.height   = estimateHeight(doc, e);
        }

        nextIndex.insert(id, int(next.size()));
        next.push_back(std::move(e));
    }

    m_entries = std::move(next);
    m_index   = std::move(nextIndex);
    recomputePositions();
}

qreal BlockLayoutCache::estimateHeight(const MarkoffDocument &doc,
                                       const Entry &e) const
{
    const qreal lineHeight = QFontMetricsF(e.style.font).lineSpacing();
    if (e.style.isRule)
        return e.style.topMargin + e.style.bottomMargin + lineHeight * 0.5;

    // Cheap newline count — no layout, no wrap simulation. Wrapped long
    // lines are under-estimated; realizeRange() corrects them as they come
    // into view, which is the whole point of the estimate being cheap.
    const QByteArray text = doc.blockText(e.id);
    const int lines = 1 + int(std::count(text.cbegin(), text.cend(), '\n'));
    return e.style.topMargin + e.style.bottomMargin + lineHeight * lines;
}

void BlockLayoutCache::realize(const MarkoffDocument &doc, const Theme &theme, Entry &e)
{
    if (e.style.isTable) {
        realizeTable(doc, theme, e);
        e.realized = true;
        return;
    }

    e.layout = std::make_unique<QTextLayout>(layoutTextFor(doc, e.id),
                                             e.style.font);

    QTextOption opt;
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    e.layout->setTextOption(opt);

    // restyleInline() does the formats-then-lines work, including the
    // first (and every subsequent) create-line pass — see its comment for
    // why the two cannot be split across separate calls.
    restyleInline(doc, theme, e);

    if (e.style.isRule)
        e.height = e.style.topMargin + e.style.bottomMargin
                 + QFontMetricsF(e.style.font).lineSpacing() * 0.5;

    e.realized = true;
}

void BlockLayoutCache::realizeTable(const MarkoffDocument &doc, const Theme &theme, Entry &e)
{
    // No single per-block layout for tables — the grid of per-cell layouts
    // below is the whole of this entry's realized state (plan T9).
    e.layout.reset();
    e.tableCells.clear();
    e.tableColWidths.clear();
    e.tableRowHeights.clear();

    const QByteArray text = doc.blockText(e.id);
    const ParsedTable parsed = parseTableBlock(text);

    const qreal lineHeight = QFontMetricsF(e.style.font).lineSpacing();
    if (!parsed.ok) {
        // Malformed table source (should not happen for a real
        // BlockKind::Table block, but the parser is defensive rather than
        // assert-and-crash): render as a single empty row so the block
        // still occupies sane space rather than collapsing to zero height.
        e.tableCols = 0;
        e.height = e.style.topMargin + e.style.bottomMargin + lineHeight;
        return;
    }

    const int rows = int(parsed.rows.size());
    const int cols = parsed.cols;
    e.tableCols = cols;
    e.tableColWidths.assign(size_t(cols), qreal(0));
    e.tableRowHeights.assign(size_t(rows), qreal(0));
    e.tableCells.resize(size_t(rows) * size_t(cols));

    QFont headerFont = e.style.font;
    headerFont.setBold(true);
    // Column widths cap at a fixed budget rather than the text column
    // width (plan T9: "capped"; no wrap/reflow policy is in spike scope,
    // so an unbounded cap would let one long cell blow the table past the
    // page margin with nothing to stop it).
    constexpr qreal kMaxColumnWidth = 240.0;
    constexpr qreal kMinColumnWidth = 40.0;

    for (int r = 0; r < rows; ++r) {
        const QFont &rowFont = (r == 0) ? headerFont : e.style.font;
        const QFontMetricsF fm(rowFont);
        qreal rowHeight = fm.lineSpacing();

        for (int c = 0; c < cols; ++c) {
            const TableCellRange &range = parsed.rows[size_t(r)][size_t(c)];
            const QByteArray cellBytes = text.mid(range.start, range.end - range.start);
            const QString cellText = QString::fromUtf8(cellBytes);

            auto layout = std::make_unique<QTextLayout>(cellText, rowFont);
            QTextOption opt;
            opt.setWrapMode(QTextOption::NoWrap);
            layout->setTextOption(opt);
            layout->beginLayout();
            QTextLine line = layout->createLine();
            if (line.isValid())
                line.setPosition(QPointF(0, 0));
            layout->endLayout();

            qreal &colWidth = e.tableColWidths[size_t(c)];
            const qreal natural = fm.horizontalAdvance(cellText) + 2 * kTableCellPadding;
            colWidth = std::max(colWidth, std::min(natural, kMaxColumnWidth));

            TableCell &cell = e.tableCells[size_t(r) * size_t(cols) + size_t(c)];
            cell.startByte = range.start;
            cell.endByte   = range.end;
            cell.layout    = std::move(layout);
        }

        e.tableRowHeights[size_t(r)] = rowHeight + 2 * kTableCellPadding;
    }

    for (qreal &w : e.tableColWidths)
        w = std::max(w, kMinColumnWidth);

    qreal total = e.style.topMargin + e.style.bottomMargin;
    for (qreal h : e.tableRowHeights)
        total += h;
    e.height = total;
}

void BlockLayoutCache::restyleInline(const MarkoffDocument &doc, const Theme &theme,
                                     Entry &e) const
{
    if (!e.layout)
        return;

    const QByteArray text = doc.blockText(e.id);
    const QList<SourceSpan> spans = doc.inlineSpansFor(e.id);

    int caretQChar = -1;
    if (e.id == m_caretBlock && m_caretByte >= 0)
        caretQChar = int(coords::byteToQtPos(text, m_caretByte));

    const QColor invisible = e.style.background.isValid()
                            ? e.style.background
                            : theme.color(Theme::Slot::EditorBackground);

    QList<QTextLayout::FormatRange> ranges =
        Detail::inlineFormatRanges(spans, caretQChar, theme, invisible);

    // Preedit area (T8): set before beginLayout() (it's a QTextEngine
    // rebuild trigger, same as setFormats() below) so the spliced-in text
    // takes part in this pass's line breaking. Base format ranges are
    // computed above against the block's real text, so any range starting
    // at or after the splice point needs to shift by the preedit's length
    // to keep landing on its real characters, same as Qt's own
    // QTextDocumentPrivate-backed engine does internally for document-
    // backed layouts (this leaf's layouts are standalone QTextLayouts —
    // C3 — so that shift is not automatic and has to happen here). A range
    // straddling the splice point is widened rather than split: an
    // over-formatted preedit run is a cosmetic nit, not a correctness bug.
    if (e.id == m_preeditBlock && m_preeditQCharPos >= 0) {
        e.layout->setPreeditArea(m_preeditQCharPos, m_preeditText);
        for (QTextLayout::FormatRange &r : ranges) {
            if (r.start >= m_preeditQCharPos)
                r.start += m_preeditText.size();
            else if (r.start + r.length > m_preeditQCharPos)
                r.length += m_preeditText.size();
        }
    } else {
        e.layout->setPreeditArea(-1, QString());
    }

    // QTextLayout::setFormats() unconditionally invalidates the layout's
    // line data when the format list is non-empty
    // (QTextEngine::setFormats -> invalidate() + clearLineData(), Qt
    // 6.11's qtextengine.cpp) — even when called on an already-realized
    // layout, well after its own beginLayout()/endLayout() pass. This
    // function is called both from realize() (first build) and from
    // setCaret() (delimiter-visibility recompute on caret move, no text
    // or width change) — so the create-line pass below must run every
    // time this runs, not just the first. Skipping it after a caret-move
    // call left every line-based query (hit-test, caret motion) broken
    // with lineCount()==0 until the next full re-realize — found via T7's
    // E7 test.
    e.layout->setFormats(ranges);

    const qreal width = qMax(qreal(1), m_textWidth - e.style.leftIndent);
    qreal h = 0;
    e.layout->beginLayout();
    while (true) {
        QTextLine line = e.layout->createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(width);
        line.setPosition(QPointF(0, h));
        h += line.height();
    }
    e.layout->endLayout();

    // Not touched for the isRule case: realize() overwrites e.height with
    // the fixed rule height right after calling this. Recomputing it here
    // unconditionally would be harmless (same text/width in the
    // caret-move-only call path) but pointless work for every other kind.
    e.height = e.style.topMargin + e.style.bottomMargin + h;
}

bool BlockLayoutCache::realizeRange(const MarkoffDocument &doc, const Theme &theme,
                                    qreal top, qreal bottom)
{
    if (m_entries.empty() || m_textWidth <= 0)
        return false;

    bool changed = false;
    for (Entry &e : m_entries) {
        if (e.realized)
            continue;
        // m_entries is in document order with monotonic y, so once we are
        // past the range we are done.
        if (e.y >= bottom)
            break;
        if (e.y + e.height <= top)
            continue;
        realize(doc, theme, e);
        changed = true;
    }

    if (changed)
        recomputePositions();
    return changed;
}

void BlockLayoutCache::setCaret(const MarkoffDocument &doc, const Theme &theme,
                                BlockId block, int byteOffset)
{
    if (m_caretBlock == block && m_caretByte == byteOffset)
        return;

    const BlockId oldBlock = m_caretBlock;
    m_caretBlock = block;
    m_caretByte  = byteOffset;

    if (oldBlock != block) {
        const int oldIdx = indexOf(oldBlock);
        if (oldIdx >= 0)
            restyleInline(doc, theme, m_entries[size_t(oldIdx)]);
    }
    const int newIdx = indexOf(block);
    if (newIdx >= 0)
        restyleInline(doc, theme, m_entries[size_t(newIdx)]);
}

void BlockLayoutCache::setPreedit(const MarkoffDocument &doc, const Theme &theme,
                                  BlockId block, int qcharPos, const QString &text)
{
    if (m_preeditBlock == block && m_preeditQCharPos == qcharPos && m_preeditText == text)
        return;

    const BlockId oldBlock = m_preeditBlock;
    m_preeditBlock     = block;
    m_preeditQCharPos  = qcharPos;
    m_preeditText      = text;

    if (oldBlock != block) {
        const int oldIdx = indexOf(oldBlock);
        if (oldIdx >= 0)
            restyleInline(doc, theme, m_entries[size_t(oldIdx)]);
    }
    const int idx = indexOf(block);
    if (idx >= 0)
        restyleInline(doc, theme, m_entries[size_t(idx)]);
}

void BlockLayoutCache::clearPreedit(const MarkoffDocument &doc, const Theme &theme)
{
    if (m_preeditQCharPos < 0)
        return;

    const BlockId block = m_preeditBlock;
    m_preeditBlock     = BlockId();
    m_preeditQCharPos  = -1;
    m_preeditText.clear();

    const int idx = indexOf(block);
    if (idx >= 0)
        restyleInline(doc, theme, m_entries[size_t(idx)]);
}

void BlockLayoutCache::recomputePositions()
{
    qreal y = 0;
    for (Entry &e : m_entries) {
        e.y = y;
        y += e.height;
    }
    m_totalHeight = y;
}

int BlockLayoutCache::realizedCount() const
{
    return int(std::count_if(m_entries.cbegin(), m_entries.cend(),
                             [](const Entry &e) { return e.realized; }));
}

int BlockLayoutCache::indexAtY(qreal y) const
{
    if (m_entries.empty())
        return -1;
    if (y < 0)
        return 0;

    // Upper bound on y, then step back one: entries are sorted by y.
    const auto it = std::upper_bound(
        m_entries.cbegin(), m_entries.cend(), y,
        [](qreal value, const Entry &e) { return value < e.y; });
    if (it == m_entries.cbegin())
        return 0;
    return int(std::distance(m_entries.cbegin(), it)) - 1;
}

int BlockLayoutCache::indexOf(BlockId id) const
{
    const auto it = m_index.constFind(id);
    return it == m_index.cend() ? -1 : *it;
}

}  // namespace Markoff::Canvas
