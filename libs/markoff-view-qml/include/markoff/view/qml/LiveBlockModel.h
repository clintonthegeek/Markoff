// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <qqmlintegration.h>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff/view/qml/BlockRecord.h>
#include "../../../../src/AstBlockDiff.h"

namespace Markoff::View::Qml {

class LiveBlockModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("LiveBlockModel is provided by LiveListModelBinding")

public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        TextRole,
        SourceRole,
        HeadingLevelRole,
        ImageSrcRole,
        ImageAltRole,
        ImageTitleRole,
        CodeLanguageRole,
        CodeTextRole,
        BlockAnchorRole,
        IsHoleRole,
        HoleIdRole
    };

    explicit LiveBlockModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int roleForName(const QByteArray &name) const;

    void setRecords(const QList<BlockRecord> &records);

    /// Apply a diff op sequence relative to `nextRecords`.
    /// Equal: row stays; if role data differs, emit dataChanged.
    /// Insert: beginInsertRows + insert + endInsertRows.
    /// Delete: beginRemoveRows + remove + endRemoveRows.
    void applyOps(const QList<AstBlockDiff::Op> &ops,
                  const QList<BlockRecord> &nextRecords);

    const BlockRecord &recordAt(int row) const { return m_rows.at(row); }

    /// Speculatively override this row's kind in the view layer (before the
    /// parser confirms). Saves the current kind for later revert. If the row
    /// already has a speculative kind, the saved "original" is not overwritten.
    void speculativelyChangeKind(int row, const QString &newKind);

    /// Revert a speculative kind override back to the last confirmed kind.
    void revertSpeculativeKind(int row);

    /// Returns true if the row currently has a speculative kind.
    bool isSpeculative(int row) const;

    /// Returns the original (pre-speculative) kind for a row, or the current
    /// kind if there is no speculative override.
    QString confirmedKindAt(int row) const;

    /// Mark/unmark a model row as composing. While composing, dataChanged for
    /// that row is deferred. On clearing composing, any deferred notification
    /// is flushed. Highlight-format updates are not affected (they don't
    /// involve TextRole text replacement).
    void setComposingRow(int row, bool composing);

    // ---------------------------------------------------------------
    // Hole-row interleaving (Stage 4 / T17)
    //
    // Holes are synthetic rows the layer asks the model to render between
    // parsed blocks. Storage is kept separate from `m_rows` so `applyOps`
    // (which operates on parsed-block diffs) is unaffected. The view-row
    // count is `m_rows.size() + m_holes.size()`. View-row → (parsed | hole)
    // is computed via `viewRowToParsed()` / `viewRowForHole()`.
    // ---------------------------------------------------------------

    /// Insert a hole row after parsed row `afterParsedRow`. The hole appears
    /// at view row `afterParsedRow + 1 + (holes already attached to that row)`.
    /// Emits begin/endInsertRows. Returns the view row index where the hole
    /// landed.
    int insertHole(quint64 holeId, int afterParsedRow, const QString &kind);

    /// Remove the hole with id `holeId`. Emits begin/endRemoveRows. Returns
    /// the view row index that was removed, or -1 if no such hole.
    int removeHole(quint64 holeId);

    /// Returns true if the given view row is a hole row.
    bool isHoleRow(int viewRow) const;

    /// Returns the hole id for the given view row, or 0 if not a hole row.
    quint64 holeIdAt(int viewRow) const;

    /// Number of holes currently in the model.
    int holeCount() const { return m_holes.size(); }

    /// Map a view row to a parsed-row index, or -1 if it's a hole row.
    int parsedRowForViewRow(int viewRow) const;
    /// Map a parsed-row index to its view-row position.
    int viewRowForParsedRow(int parsedRow) const;

private:
    struct HoleRow {
        quint64 id = 0;
        int     afterParsedRow = -1;
        QString kind;
    };

    /// Compute the view row for a hole at the given index in m_holes,
    /// honoring sort by afterParsedRow.
    int viewRowForHole(int holeIndex) const;
    /// Compute the view row for a parsed row.
    int viewRowForParsed(int parsedRow) const;
    /// Map a view row to a parsed row index, or -1 if it's a hole row.
    int viewRowToParsed(int viewRow) const;
    /// Map a view row to a hole index in m_holes, or -1 if it's a parsed row.
    int viewRowToHole(int viewRow) const;

    QList<BlockRecord> m_rows;
    QList<HoleRow>     m_holes;  // sorted by afterParsedRow ascending
    QHash<int, QString> m_speculativeOriginals;  // row → original kind

    // Composing-row deferral: tracked by BlockAnchor (CRDT-stable) so the row
    // number can shift via Insert/Delete ops without losing the reference.
    // At most one block is composing at a time (single focused TextEdit).
    std::optional<Markoff::BlockAnchor> m_composingAnchor;
    bool m_hasDeferredDataChanged = false;
};

}  // namespace Markoff::View::Qml
