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
        CodeTextRole
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

private:
    QList<BlockRecord> m_rows;
};

}  // namespace Markoff::View::Qml
