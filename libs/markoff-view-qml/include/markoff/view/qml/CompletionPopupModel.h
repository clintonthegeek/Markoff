// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QtQmlIntegration>

#include <memory>

#include <markoff-foundation/CompletionCandidate.h>
#include <markoff-foundation/CompletionRegistry.h>

namespace Markoff::View::Qml {

class CompletionPopupModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Role {
        DisplayRole   = Qt::UserRole + 1,
        InsertionRole,
        DetailRole,
        IconNameRole,
        PriorityRole,
    };

    explicit CompletionPopupModel(QObject *parent = nullptr);
    ~CompletionPopupModel() override;

    int      rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Build a CompletionContext from the trigger kind + prefix and gather
    /// synchronous candidates from the registry. Resets the model contents.
    /// Trigger is passed as an int matching `CompletionTrigger`'s enum values.
    Q_INVOKABLE void requestCompletions(int trigger, const QString &prefix);

    /// Register a provider with the internal registry.
    /// Used by the test app (and tests) to wire EmojiCompletionProvider.
    void registerProvider(std::shared_ptr<Markoff::CompletionProvider>);

    /// Clear all candidates.
    Q_INVOKABLE void clear();

Q_SIGNALS:
    void countChanged();

private:
    Markoff::CompletionRegistry         m_registry;
    QList<Markoff::CompletionCandidate> m_candidates;
};

}  // namespace Markoff::View::Qml
