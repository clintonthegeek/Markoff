// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>

#include <markoff/live/LiveListModelBinding.h>

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

Q_SIGNALS:
    void bindingChanged();
    void modelIndexChanged();

private:
    QPointer<LiveListModelBinding> m_binding;
    int                            m_modelIndex = -1;
    bool                           m_applyingTextUpdate = false;
};

}  // namespace Markoff::Live
