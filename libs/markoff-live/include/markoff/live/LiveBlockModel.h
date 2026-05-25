// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/live/BlockRecord.h>
#include <markoff/live/AstBlockDiff.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <qqmlintegration.h>

#include <limits>

namespace Markoff::Live {

using Detail::AstBlockDiff;  // internal diff helper; consumers see it through this alias

/// `QAbstractListModel` over a list of `BlockRecord`s driven by
/// `LiveListModelBinding::applyOps`. Roles: kind, text, headingLevel,
/// codeLanguage, blockAnchor. Per-row edit-sequence tracking for the R4
/// freshness rule (spec §4.3).
///
/// Simplified from `markoff-view-qml`'s LiveBlockModel: no hole rows, no
/// composing-row deferral, no speculative-kind registry — those are retired
/// in the C-architecture per spec §4.4.
class MARKOFF_LIVE_EXPORT LiveBlockModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveBlockModel is provided by LiveListModelBinding")

public:
    enum Role {
        KindRole         = Qt::UserRole + 1,
        TextRole,
        HeadingLevelRole,
        HeadingFormRole,
        CodeLanguageRole,
        BlockAnchorRole,
        BlockAttrsRole,   // QVariantMap of block-kind attributes
        MarkerStyleRole,
        MarkerNumberRole,
        IndentLevelRole,
        CheckedRole,
        LooseRunRole,
        InlineSpansRole,
        DelegateClassRole,  // new: see Markoff::Live::delegateClassFor.
        FindSpansRole,      // QList<Markoff::Live::FindSpan>; written by LiveFindAdapter.
    };

    explicit LiveBlockModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Apply a diff op sequence relative to `nextRecords`, applying the
    /// R4 freshness rule (spec §4.3): for each Equal op, the row's
    /// text-role update is gated on
    ///     rowEditSequence(row) <= parseInputEditSeq
    /// Stale rows preserve their existing model text but still receive
    /// non-text role updates (kind / headingLevel / codeLanguage /
    /// blockAnchor). Insert / Delete ops are unconditional.
    /// Note: `inlineSpans` is not part of `BlockRecord::operator==`, so
    /// when a stale row's only "change" would be its inline spans, the
    /// equality short-circuit suppresses the assignment and the prior
    /// spans are retained. This is a pre-existing limitation of the
    /// equality-based change detection.
    ///
    /// `parseInputEditSeq` defaults to `std::numeric_limits<quint64>::max()`,
    /// which means "all rows fresh" — preserves the R2/R3 callsite shape.
    void applyOps(const QList<AstBlockDiff::Op> &ops,
                  const QList<BlockRecord> &nextRecords,
                  quint64 parseInputEditSeq = std::numeric_limits<quint64>::max());

    const BlockRecord &recordAt(int row) const { return m_rows.at(row); }

    /// Replace this block's find-match list. Emits a single `dataChanged`
    /// with roles == {FindSpansRole} so consumers can react minimally.
    /// No-op (no signal) if the block is unknown or the list is identical
    /// to the current value. Empty list clears any prior matches and
    /// emits dataChanged so renderers can vanish highlights.
    void setFindSpans(const Markoff::BlockAnchor &anchor,
                      const QList<FindSpan> &spans);

    /// Spec 2026-05-11-focus-chokepoint-design.md §5.1.1. Returns the kind
    /// of the block with the given anchor, or empty string if not found.
    /// Used by LiveCursorState's stale-registration check.
    QString kindFor(Markoff::BlockAnchor anchor) const;

    /// Test-only helper: append a minimal row with the given anchor/kind/text.
    /// Does not emit model signals. Only for unit tests that need to populate
    /// the model without going through applyOps.
    void insertTestRow(Markoff::BlockAnchor anchor, const QString &kind, const QString &text);

    // ---- R4 freshness tracking (spec §4.2/§4.3). ----
    // Set by LiveEditBinding on each local keystroke. Read by
    // LiveListModelBinding::onParseUpdated for the parseFreshForRow check.
    // In R2 these are always 0; the machinery is in place for R4.
    quint64 rowEditSequence(int row) const;
    void    setRowEditSequence(int row, quint64 editSeq);

    // ---- R6: C++ accessor for inline spans. ----
    // InlineFormatHighlighter reads pre-baked spans to avoid re-parsing.
    // Returns by value (QList is COW) so QML can invoke it via Q_INVOKABLE.
    Q_INVOKABLE QList<Markoff::SourceSpan> spansAtRow(int row) const;

private:
    QList<BlockRecord> m_rows;
    QList<quint64>     m_rowEditSequences;  // parallel to m_rows
};

}  // namespace Markoff::Live
