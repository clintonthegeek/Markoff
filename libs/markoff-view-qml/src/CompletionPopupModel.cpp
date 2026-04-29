// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/CompletionPopupModel.h>

#include <markoff-foundation/CompletionContext.h>

namespace Markoff::View::Qml {

CompletionPopupModel::CompletionPopupModel(QObject *parent)
    : QAbstractListModel(parent) {}
CompletionPopupModel::~CompletionPopupModel() = default;

int CompletionPopupModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_candidates.size();
}

QVariant CompletionPopupModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_candidates.size())
        return {};
    const Markoff::CompletionCandidate &c = m_candidates.at(index.row());
    switch (role) {
    case Qt::DisplayRole:  // for QML's default role
    case DisplayRole:      return c.display;
    case InsertionRole:    return c.insertion;
    case DetailRole:       return c.detail;
    case IconNameRole:     return c.iconName;
    case PriorityRole:     return c.priority;
    default:               return {};
    }
}

QHash<int, QByteArray> CompletionPopupModel::roleNames() const
{
    return {
        { DisplayRole,   "display"   },
        { InsertionRole, "insertion" },
        { DetailRole,    "detail"    },
        { IconNameRole,  "iconName"  },
        { PriorityRole,  "priority"  },
    };
}

void CompletionPopupModel::requestCompletions(int trigger, const QString &prefix)
{
    Markoff::CompletionContext ctx;
    ctx.trigger = static_cast<Markoff::CompletionTrigger>(trigger);
    ctx.prefix  = prefix;

    static quint64 sNextRequestId = 1;
    QList<Markoff::CompletionCandidate> next = m_registry.gather(ctx, sNextRequestId++);

    beginResetModel();
    m_candidates = std::move(next);
    endResetModel();
    Q_EMIT countChanged();
}

void CompletionPopupModel::registerProvider(std::shared_ptr<Markoff::CompletionProvider> p)
{
    m_registry.registerProvider(std::move(p));
}

void CompletionPopupModel::clear()
{
    if (m_candidates.isEmpty()) return;
    beginResetModel();
    m_candidates.clear();
    endResetModel();
    Q_EMIT countChanged();
}

}  // namespace Markoff::View::Qml
