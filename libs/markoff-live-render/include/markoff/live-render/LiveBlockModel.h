// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/live-render/BlockRecord.h>
#include <markoff/live-render/AstBlockDiff.h>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <qqmlintegration.h>

#include <limits>

namespace Markoff::LiveRender {

/// `QAbstractListModel` over a list of `BlockRecord`s driven by
/// `LiveListModelBinding::applyOps`. Roles: kind, text, headingLevel,
/// codeLanguage, blockAnchor. Per-row edit-sequence tracking for the R4
/// freshness rule (spec §4.3).
///
/// Simplified from `markoff-view-qml`'s LiveBlockModel: no hole rows, no
/// composing-row deferral, no speculative-kind registry — those are retired
/// in the C-architecture per spec §4.4.
class MARKOFF_LIVE_RENDER_EXPORT LiveBlockModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveBlockModel is provided by LiveListModelBinding")

public:
    enum Role {
        KindRole         = Qt::UserRole + 1,
        TextRole,
        HeadingLevelRole,
        CodeLanguageRole,
        BlockAnchorRole,
        BlockAttrsRole,   // QVariantMap of block-kind attributes
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

    // ---- R4 freshness tracking (spec §4.2/§4.3). ----
    // Set by LiveEditBinding on each local keystroke. Read by
    // LiveListModelBinding::onParseUpdated for the parseFreshForRow check.
    // In R2 these are always 0; the machinery is in place for R4.
    quint64 rowEditSequence(int row) const;
    void    setRowEditSequence(int row, quint64 editSeq);

    // ---- R6: C++ accessor for inline spans. ----
    // InlineFormatHighlighter reads pre-baked spans to avoid re-parsing.
    const QList<Markoff::SourceSpan> &spansAtRow(int row) const;

Q_SIGNALS:
    /// Emitted when an Equal-op rewrite (collapsed Delete+Insert) updates
    /// the row's BlockAnchor in place. Listeners — primarily
    /// LiveCursorState — translate any cursor pinned to `oldAnchor` to
    /// `newAnchor` so the cursor survives the foundation's per-keystroke
    /// anchor renumbering at qtPos 0 of a block.
    void anchorRenumbered(int row,
                          Markoff::BlockAnchor oldAnchor,
                          Markoff::BlockAnchor newAnchor);

private:
    QList<BlockRecord> m_rows;
    QList<quint64>     m_rowEditSequences;  // parallel to m_rows
};

}  // namespace Markoff::LiveRender
