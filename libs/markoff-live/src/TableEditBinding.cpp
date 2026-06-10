// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/TableEditBinding.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/Coordinates.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Theme.h>
#include <markoff/core/UndoLog.h>
#include <markoff/parser/SourceSpan.h>
#include <markoff/parser/PerfProbe.h>

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScopeGuard>

namespace Markoff::Live {

namespace coords = ::Markoff::Live::Detail::Coordinates;

TableEditBinding::TableEditBinding(QObject *parent)
    : QObject(parent) {}

TableEditBinding::~TableEditBinding() = default;

LiveListModelBinding *TableEditBinding::binding() const
{
    return m_binding.data();
}

void TableEditBinding::setBinding(LiveListModelBinding *b)
{
    if (m_binding.data() == b) return;
    m_binding = b;
    Q_EMIT bindingChanged();
}

int TableEditBinding::modelIndex() const
{
    return m_modelIndex;
}

void TableEditBinding::setModelIndex(int row)
{
    if (m_modelIndex == row) return;
    m_modelIndex = row;
    Q_EMIT modelIndexChanged();
}

void TableEditBinding::applyCellEdit(int cellStartCharPos,
                                     int cellQtPos,
                                     int removed,
                                     const QString &added)
{
    MARKOFF_PERF_SCOPE("live.TableEditBinding::applyCellEdit");
    if (!m_binding || !m_binding->document() || !m_binding->model()) return;
    if (m_binding->readOnly()) return;  // read-only gate (spec §4.2)
    if (m_modelIndex < 0) return;
    if (m_modelIndex >= m_binding->model()->rowCount()) return;
    if (cellStartCharPos < 0 || cellQtPos < 0 || removed < 0) return;

    // No-op short-circuit. Cell `onTextChanged` fires every time the
    // `parsedTable → cellText → cellEdit.text` rebind cascade pushes a
    // (frequently unchanged) string back into the cell's TextEdit. Each
    // such echo reaches here with removed==0 and added empty — applying
    // it bumps the block edit sequence anyway (invalidating the inline
    // parse cache), triggers `flushPendingD2Changed`, which re-enters
    // onD2Changed, rebinds every cell again, and we cascade. Measured
    // pre-fix: 20+ applyCellEdit calls per user keystroke for a 3×4
    // table, 34+ for a 4×6 table. Drop the call; everything downstream
    // is wasted work for a no-op delta.
    if (removed == 0 && added.isEmpty()) {
        Markoff::Perf::Probe::instance().note(
            QStringLiteral("live.TableEditBinding::applyCellEdit.noop_skip"));
        return;
    }

    auto *doc   = m_binding->document();
    auto *model = m_binding->model();
    const auto &record = model->recordAt(m_modelIndex);

    // Authoritative pre-edit buffer. Query the CRDT directly rather
    // than the model's cached `record.text` so we're not racing the
    // `onD2Changed` cascade — applyCellEdit may fire from a cell's
    // contentsChange handler before the model has been notified of
    // the prior edit.
    const QByteArray preUtf8 = doc->blockText(record.blockAnchor);

    const int absoluteCharPos = cellStartCharPos + cellQtPos;
    const uint32_t byteOff = static_cast<uint32_t>(
        coords::qtPosToByte(preUtf8, absoluteCharPos));
    const uint32_t removedBytes = static_cast<uint32_t>(
        coords::qtPosToByte(preUtf8, absoluteCharPos + removed)) - byteOff;
    const QByteArray addedBytes = added.toUtf8();

    // Re-entrance guard (mirrors `LiveEditBinding::m_applyingTextUpdate`
    // and the dormant getter exposed at TableEditBinding.h:80). Set
    // before `flushPendingD2Changed` so the synchronous
    // onD2Changed → buildRecords → cellText binding refresh →
    // cellEdit.text re-push → cell `onTextChanged` cascade sees a true
    // flag and skips. Without this, the cells re-fire `applyCellEdit`
    // with no-op deltas; the previous commit catches them at the entry
    // point, but they still consume `cellText.eval` +
    // `inlineSpansForCell` + `setInlineSpans+rehighlight` work. Setting
    // this here lets the QML cell-level `onTextChanged` handler exit
    // early before any of that — N cells become 1.
    m_applyingTextUpdate = true;
    auto resetGuard = qScopeGuard([this] { m_applyingTextUpdate = false; });

    auto &undoLog = doc->d2UndoLog();
    UndoLog::Transaction t(undoLog);
    doc->d2ApplyBufferEdit(record.blockAnchor, byteOff, removedBytes,
                           addedBytes, t);

    // Flush so the model + delegate rebuild see the edit synchronously,
    // matching LiveEditBinding's contract for in-process edits.
    doc->flushPendingD2Changed();
}

QVariantList TableEditBinding::inlineSpansForCell(
    const QVariant &blockSpans,
    int cellStartChar, int cellEndChar) const
{
    MARKOFF_PERF_SCOPE("live.TableEditBinding::inlineSpansForCell");
    QVariantList out;
    if (cellEndChar <= cellStartChar) return out;
    // QML hands the model role through as either a typed
    // QList<SourceSpan> (the InlineSpansRole's wrapped value) or a
    // QVariantList (rare; defensive). Try the typed unwrap first.
    QList<Markoff::SourceSpan> spans;
    if (blockSpans.canConvert<QList<Markoff::SourceSpan>>()) {
        spans = blockSpans.value<QList<Markoff::SourceSpan>>();
    } else if (blockSpans.canConvert<QVariantList>()) {
        for (const QVariant &v : blockSpans.toList()) {
            if (v.canConvert<Markoff::SourceSpan>())
                spans.append(v.value<Markoff::SourceSpan>());
        }
    }
    out.reserve(spans.size());
    for (Markoff::SourceSpan s : spans) {
        const int spanStart = s.charOffset;
        const int spanEnd   = s.charOffset + s.charLength;
        // Strict containment — partial overlaps would paint across the
        // pipe boundary inside the cell document. Tree-sitter places
        // inline spans inside `pipe_table_cell` byte ranges (post-grammar
        // fix), so partial overlaps shouldn't arise in practice; the
        // strict check is defensive against future grammar changes.
        if (spanStart < cellStartChar || spanEnd > cellEndChar) continue;
        s.charOffset = spanStart - cellStartChar;
        // Zero out the UTF-8 byte fields. They're absolute block-buffer
        // byte offsets that shift any time bytes elsewhere in the table
        // grow or shrink — so unchanged cells receive spans that compare
        // unequal under `SourceSpan::operator==` purely because of
        // sibling-cell typing. That defeats `InlineHighlighterAttached`'s
        // setSpans equality short-circuit. Cell-level highlighting
        // (highlightBlock) does NOT consume these fields — it works
        // exclusively from charOffset/charLength. The find-pass adapter
        // uses utf8Offset/utf8Length, but find spans flow through a
        // separate pipeline that never touches inlineSpansForCell.
        // Zeroing here is safe and is the precondition for cell-identity
        // stability across keystrokes that don't change the cell's
        // content. (Bench doesn't show an improvement here because some
        // other SourceSpan field — likely parent ranges in delimiter
        // spans, or span ordering from tree-sitter — still varies for
        // unchanged cells; future work.)
        s.utf8Offset = 0;
        s.utf8Length = 0;
        if (s.parentCharStart >= 0) {
            s.parentCharStart -= cellStartChar;
            s.parentCharEnd   -= cellStartChar;
            // Clamp parent range to the cell document. Without this, the
            // delimiter-visibility test (`caret within parent range`) sees
            // a parent that extends past the cell's QTextDocument length;
            // the clamp keeps the value self-consistent for any future
            // consumer that uses it.
            const int cellLen = cellEndChar - cellStartChar;
            if (s.parentCharStart < 0)        s.parentCharStart = 0;
            if (s.parentCharEnd   > cellLen)  s.parentCharEnd   = cellLen;
        }
        out.append(QVariant::fromValue(s));
    }
    return out;
}

void TableEditBinding::perfTime(const QString &name, double ms) const
{
    Markoff::Perf::Probe::instance().recordMs(name, ms);
}

void TableEditBinding::perfNote(const QString &name) const
{
    Markoff::Perf::Probe::instance().note(name);
}

// --- E4 follow-up: column-width metric helpers (A1) ----------------

qreal TableEditBinding::cellMinWidth(const QString &text,
                                     const QFont &font,
                                     qreal padding)
{
    const QFontMetricsF fm(font);
    qreal widest = 0;
    // Split on whitespace; the longest token is the "unbreakable run"
    // that the wrap engine cannot shrink past. Matches the spirit of
    // WrapAtWordBoundaryOrAnywhere falling back to mid-word breaks only
    // as a last resort: we size to keep word boundaries breakable, and
    // accept that a single hyper-long unbreakable run sets the floor.
    const auto tokens = text.split(QRegularExpression(QStringLiteral("\\s+")),
                                   Qt::SkipEmptyParts);
    for (const auto &tok : tokens) {
        const qreal w = fm.horizontalAdvance(tok);
        if (w > widest) widest = w;
    }
    return std::max(widest + 2 * padding, kMinColumnWidth);
}

qreal TableEditBinding::cellMaxWidth(const QString &text,
                                     const QFont &font,
                                     qreal padding)
{
    const QFontMetricsF fm(font);
    return fm.horizontalAdvance(text) + 2 * padding;
}

// --- E4 follow-up: computeColumnWidths Q_INVOKABLE (A3) ----------

QVariantList TableEditBinding::computeColumnWidths(
    const QVariantList &headers,
    const QVariantList &body,
    qreal availWidth) const
{
    MARKOFF_PERF_SCOPE("live.TableEditBinding::computeColumnWidths");
    QVariantList out;

    const int n = headers.size();
    if (n == 0 || availWidth <= 0) return out;

    // Resolve fonts. Theme-aware when wired; QGuiApplication fallback
    // when the binding hasn't been hooked up yet (test path, init
    // transient). Header gets the body font with setBold(true); we do
    // not chase a separate FontRole::Heading because pipe-table headers
    // aren't document-level headings, just visually-bold body cells.
    QFont bodyFont;
    if (m_binding && m_binding->theme()) {
        const Markoff::Theme *t = m_binding->theme();
        bodyFont = t->font(Markoff::Theme::FontRole::Body);
        const qreal px = t->pixelSizeFor(Markoff::Theme::Slot::TextDefault);
        const qreal scale = m_binding->fontScale();
        if (px > 0 && scale > 0)
            bodyFont.setPixelSize(static_cast<int>(px * scale));
    } else {
        bodyFont = QGuiApplication::font();
    }
    QFont headerFont = bodyFont;
    headerFont.setBold(true);

    const qreal pad = cellPadding();

    // Aggregate per-column metrics. Header row uses headerFont; body
    // rows use bodyFont. The column-aggregate floor (max with kMin
    // and the maxWidth-not-below-minWidth invariant) is applied
    // after the row sweep.
    QList<ColumnMetrics> metrics(n);

    for (int c = 0; c < n; ++c) {
        const QString h = headers.at(c).toString();
        metrics[c].minWidth = std::max(metrics[c].minWidth,
                                       cellMinWidth(h, headerFont, pad));
        metrics[c].maxWidth = std::max(metrics[c].maxWidth,
                                       cellMaxWidth(h, headerFont, pad));
    }

    for (const QVariant &rowVar : body) {
        const QVariantList row = rowVar.toList();
        const int rn = qMin(row.size(), n);
        for (int c = 0; c < rn; ++c) {
            const QString cellText = row.at(c).toString();
            metrics[c].minWidth = std::max(metrics[c].minWidth,
                                           cellMinWidth(cellText, bodyFont, pad));
            metrics[c].maxWidth = std::max(metrics[c].maxWidth,
                                           cellMaxWidth(cellText, bodyFont, pad));
        }
    }

    // Floor + invariant.
    for (int c = 0; c < n; ++c) {
        metrics[c].minWidth = std::max(metrics[c].minWidth, kMinColumnWidth);
        metrics[c].maxWidth = std::max(metrics[c].maxWidth, metrics[c].minWidth);
    }

    const auto widths = distributeColumnsAuto(metrics, availWidth);
    out.reserve(widths.size());
    for (qreal w : widths) out.append(QVariant::fromValue(w));
    return out;
}

// --- E4 follow-up: distributeColumnsAuto (Penelope port, A2) -----
//
// Verbatim port of distributeColumnsAuto from
// ~/dev/Penelope/src/engine.cpp. Three branches: totalMax≤avail
// (everything fits — split surplus evenly), totalMin≥avail (cannot
// fit even with aggressive wrap — scale mins proportionally), else
// proportional distribution between min and max via W/D.
QList<qreal> TableEditBinding::distributeColumnsAuto(
    const QList<ColumnMetrics> &metrics, qreal availWidth)
{
    const int n = metrics.size();
    QList<qreal> widths(n);
    if (n == 0) return widths;

    qreal totalMin = 0, totalMax = 0;
    for (const auto &m : metrics) {
        totalMin += m.minWidth;
        totalMax += m.maxWidth;
    }

    if (totalMax <= availWidth) {
        // All content fits without wrapping — use max widths,
        // distribute surplus.
        const qreal surplus = availWidth - totalMax;
        for (int i = 0; i < n; ++i)
            widths[i] = metrics[i].maxWidth + surplus / n;
    } else if (totalMin >= availWidth) {
        // Can't avoid wrapping; can't even honor mins. Scale mins
        // proportionally so the result still sums to availWidth.
        for (int i = 0; i < n; ++i)
            widths[i] = metrics[i].minWidth * (availWidth / totalMin);
    } else {
        // Proportional distribution between min and max.
        const qreal W = availWidth - totalMin;
        const qreal D = totalMax - totalMin;
        for (int i = 0; i < n; ++i)
            widths[i] = metrics[i].minWidth
                      + (metrics[i].maxWidth - metrics[i].minWidth) * W / D;
    }
    return widths;
}

}  // namespace Markoff::Live
