// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <markoff/live/LiveListModelBinding.h>

#include <QFont>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <qqmlintegration.h>

#include <markoff/parser/SourceSpan.h>

namespace Markoff::Live {

/// Per-delegate edit binding for `TableDelegate`.
///
/// Translates a cell-local edit (within-cell qtPos + char-delta) into a
/// D2 per-block buffer mutation via `MarkoffDocument::d2ApplyBufferEdit`.
/// The QML cell calls `applyCellEdit(cellStartCharPos, cellQtPos,
/// removedChars, addedText)` from its `onContentsChange` handler in C2:
///   - `cellStartCharPos` is the cell's start position within the block
///     buffer (sourced from `parsedTable.cellCharRanges[row][col].start`).
///   - `cellQtPos` is the position within the cell where the edit fired.
///   - `removedChars` / `addedText` are the QTextDocument
///     `contentsChange` delta for the cell's TextEdit.
///
/// Sibling to `LiveEditBinding` — that one drives flat-block edits (one
/// TextEdit per block, identity intra-block translation); this one
/// drives multi-cell edits where the per-cell qtPos must be re-projected
/// into the block-buffer frame.
///
/// Re-entrance guard. `isApplyingTextUpdate()` mirrors
/// `LiveEditBinding`'s pattern. C1 (this commit) only exposes the
/// getter; the value stays false until C2 wires the setter when the
/// `model.text → parsedTable rebuild → cell.text` re-fire path starts
/// producing onContentsChange echoes that must be filtered out.
class MARKOFF_LIVE_EXPORT TableEditBinding : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(Markoff::Live::LiveListModelBinding *binding
               READ binding WRITE setBinding NOTIFY bindingChanged)
    Q_PROPERTY(int modelIndex
               READ modelIndex WRITE setModelIndex NOTIFY modelIndexChanged)

public:
    explicit TableEditBinding(QObject *parent = nullptr);
    ~TableEditBinding() override;

    LiveListModelBinding *binding() const;
    void setBinding(LiveListModelBinding *b);

    int  modelIndex() const;
    void setModelIndex(int row);

    /// Apply a cell-local edit to the block buffer at the appropriate
    /// byte offset. Per plan §C1 step 2; spec §6.1.
    ///
    /// Computes:
    ///   absoluteCharPos = cellStartCharPos + cellQtPos
    ///   bufferByteOffset = qtPosToByte(blockBufferUtf8, absoluteCharPos)
    ///   removedBytes     = qtPosToByte(blockBufferUtf8,
    ///                                  absoluteCharPos + removed)
    ///                      - bufferByteOffset
    ///   addedBytes       = added.toUtf8()
    /// Then calls `d2ApplyBufferEdit(blockAnchor, bufferByteOffset,
    ///                               removedBytes, addedBytes, txn)`.
    ///
    /// Silently drops when binding / document / model are missing or
    /// `modelIndex` is out of range, mirroring `LiveEditBinding`'s
    /// defensive style.
    Q_INVOKABLE void applyCellEdit(int cellStartCharPos,
                                   int cellQtPos,
                                   int removed,
                                   const QString &added);

    /// Re-entrance guard exposed for cell delegates per
    /// `LiveEditBinding`'s pattern. Always returns false in C1; C2
    /// wires the setter when parsedTable-driven cell.text refreshes
    /// start firing onContentsChange echoes that must be filtered out.
    Q_INVOKABLE bool isApplyingTextUpdate() const { return m_applyingTextUpdate; }

    /// F1: take a block-level inlineSpans QVariantList (`Markoff::SourceSpan`
    /// wrapped in QVariant per-element) and return the subset whose
    /// `[charOffset, charOffset+charLength)` falls inside
    /// `[cellStartChar, cellEndChar)`. Returned spans have `charOffset` and
    /// `parentCharStart`/`parentCharEnd` re-projected into the cell's local
    /// frame (block-offset - cellStartChar). `utf8Offset`/`utf8Length` are
    /// not consumed by `InlineHighlighter::highlightBlock` (only the find-
    /// pass uses them, and find spans flow through a separate adapter); we
    /// leave them at their block-relative values rather than introduce a
    /// byte-projection dependency in QML. Used by `TableDelegate`'s per-cell
    /// `InlineHighlighterAttached`.
    Q_INVOKABLE QVariantList inlineSpansForCell(
        const QVariant &blockSpans,
        int cellStartChar,
        int cellEndChar) const;

    /// Perf instrumentation hooks for QML/JS sites that the C++ scoped
    /// probe can't reach. `perfTime` records caller-measured milliseconds
    /// against `name`; `perfNote` just increments a call count. Both are
    /// dispatched through `Markoff::Perf::Probe` (header-only singleton).
    Q_INVOKABLE void perfTime(const QString &name, double ms) const;
    Q_INVOKABLE void perfNote(const QString &name) const;

    // --- E4 follow-up: column-width metrics (A1) --------------------
    //
    // Spec: docs/specs/2026-05-23-e4-cell-wrap-and-column-width-design.md §3.1
    // Public-static rather than anonymous-namespace so the unit test can
    // drive the pure-function helpers directly without a friend
    // declaration or test-only Q_INVOKABLE shim. Still no separate
    // header, still no second consumer — refinement of the plan-time
    // resolution, not a deviation.

    /// Floor applied at the per-cell `cellMinWidth` and at column
    /// aggregation. Preserves the 60px first-pass minimum from E4 B2.
    static constexpr qreal kMinColumnWidth = 60.0;

    /// Padding applied symmetrically inside each cell. Returned as a
    /// scalar (not a Theme-keyed accessor) so the C++ side can compute
    /// metrics without bouncing through QML. Matches the QML side's
    /// `anchors.margins: 4` in TableDelegate's per-cell Rectangle.
    static qreal cellPadding() { return 4.0; }

    /// Widest unbreakable run in `text` (split on whitespace), measured
    /// in `font`, with `2*padding` added. Floored at `kMinColumnWidth`
    /// so empty / very-short cells still occupy a readable slot.
    static qreal cellMinWidth(const QString &text,
                              const QFont &font,
                              qreal padding);

    /// Full single-line width of `text` measured in `font`, with
    /// `2*padding` added. Not floored — the column aggregation step
    /// applies the floor via `max(metrics[c].minWidth, kMinColumnWidth)`
    /// and `max(metrics[c].maxWidth, metrics[c].minWidth)`.
    static qreal cellMaxWidth(const QString &text,
                              const QFont &font,
                              qreal padding);

    /// Per-column min/max width metrics, aggregated across rows by `max`.
    /// Public so the unit test can construct fixtures directly.
    struct ColumnMetrics {
        qreal minWidth = 0;
        qreal maxWidth = 0;
    };

    /// Verbatim port of Penelope's `distributeColumnsAuto`. Three
    /// branches: totalMax≤avail → distribute surplus evenly above maxes;
    /// totalMin≥avail → scale mins proportionally; else → proportional
    /// between min and max via W=avail-totalMin / D=totalMax-totalMin.
    ///
    /// In every branch `sum(widths)` equals `availWidth` (modulo FP
    /// rounding). Empty `metrics` returns an empty list; the caller is
    /// responsible for the "no columns" guard.
    ///
    /// Spec: docs/specs/2026-05-23-e4-cell-wrap-and-column-width-design.md §3.3.
    static QList<qreal> distributeColumnsAuto(const QList<ColumnMetrics> &metrics,
                                              qreal availWidth);

    /// QML-facing column-width computer (A3). `headers` is a JS array
    /// of strings (one per column); `body` is a JS array of arrays of
    /// strings (one row per outer entry, one cell per inner entry).
    /// `availWidth` is the layout-available width in pixels (typically
    /// the GridLayout's `width`).
    ///
    /// Returns a `QVariantList<qreal>` of length `headers.size()`. The
    /// cell-default font is read from `binding()->theme()` via the
    /// `TextDefault` slot (header cells get `setBold(true)`); falls
    /// back to `QGuiApplication::font()` when no theme is wired.
    ///
    /// Empty headers or `availWidth <= 0` → empty list (spec §7 risks
    /// 3 and 4 — the binding's second fire after construction is the
    /// source of truth).
    Q_INVOKABLE QVariantList computeColumnWidths(const QVariantList &headers,
                                                 const QVariantList &body,
                                                 qreal availWidth) const;

Q_SIGNALS:
    void bindingChanged();
    void modelIndexChanged();

private:
    QPointer<LiveListModelBinding> m_binding;
    int                            m_modelIndex = -1;
    bool                           m_applyingTextUpdate = false;
};

}  // namespace Markoff::Live
