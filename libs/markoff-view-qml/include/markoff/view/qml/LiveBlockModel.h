// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <qqmlintegration.h>

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
        BlockAnchorRole
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

private:
    QList<BlockRecord> m_rows;
    QHash<int, QString> m_speculativeOriginals;  // row → original kind
};

}  // namespace Markoff::View::Qml
