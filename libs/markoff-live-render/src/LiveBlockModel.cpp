// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveBlockModel.h>

#include <markoff-foundation/BlockAnchor.h>

namespace Markoff::LiveRender {

namespace {
const QList<Markoff::SourceSpan> kEmptySpans;
}

LiveBlockModel::LiveBlockModel(QObject *parent) : QAbstractListModel(parent) {}

int LiveBlockModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_rows.size();
}

QHash<int, QByteArray> LiveBlockModel::roleNames() const
{
    return {
        { KindRole,         "kind" },
        { TextRole,         "text" },
        { HeadingLevelRole, "headingLevel" },
        { CodeLanguageRole, "codeLanguage" },
        { BlockAnchorRole,  "blockAnchor" },
    };
}

QVariant LiveBlockModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const BlockRecord &r = m_rows[index.row()];
    switch (role) {
        case KindRole:          return r.kind;
        case TextRole:          return r.text;
        case HeadingLevelRole:  return r.headingLevel;
        case CodeLanguageRole:  return r.codeLanguage;
        case BlockAnchorRole:   return QVariant::fromValue(r.blockAnchor);
        default:                return {};
    }
}

void LiveBlockModel::applyOps(const QList<AstBlockDiff::Op> &ops,
                              const QList<BlockRecord> &nextRecords)
{
    int row = 0;
    for (const auto &op : ops) {
        switch (op.kind) {
            case AstBlockDiff::OpKind::Equal: {
                const BlockRecord &next = nextRecords[op.nextIndex];
                if (m_rows[row] != next) {
                    m_rows[row] = next;
                    Q_EMIT dataChanged(index(row), index(row));
                }
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Insert: {
                beginInsertRows(QModelIndex(), row, row);
                m_rows.insert(row, nextRecords[op.nextIndex]);
                m_rowEditSequences.insert(row, quint64(0));
                endInsertRows();
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Delete: {
                beginRemoveRows(QModelIndex(), row, row);
                m_rows.removeAt(row);
                m_rowEditSequences.removeAt(row);
                endRemoveRows();
                // Do NOT increment row — next op references the shifted index.
                break;
            }
        }
    }
}

quint64 LiveBlockModel::rowEditSequence(int row) const
{
    if (row < 0 || row >= m_rowEditSequences.size()) return 0;
    return m_rowEditSequences[row];
}

void LiveBlockModel::setRowEditSequence(int row, quint64 editSeq)
{
    if (row >= 0 && row < m_rowEditSequences.size())
        m_rowEditSequences[row] = editSeq;
}

const QList<Markoff::SourceSpan> &LiveBlockModel::spansAtRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) return kEmptySpans;
    return m_rows[row].inlineSpans;
}

}  // namespace Markoff::LiveRender
