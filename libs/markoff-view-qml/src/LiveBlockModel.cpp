// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveBlockModel.h>

#include <markoff-foundation/BlockAnchor.h>

namespace Markoff::View::Qml {

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
        { SourceRole,       "source" },
        { HeadingLevelRole, "headingLevel" },
        { ImageSrcRole,     "imageSrc" },
        { ImageAltRole,     "imageAlt" },
        { ImageTitleRole,   "imageTitle" },
        { CodeLanguageRole, "codeLanguage" },
        { CodeTextRole,     "codeText" },
        { BlockAnchorRole,  "blockAnchor" },
    };
}

int LiveBlockModel::roleForName(const QByteArray &name) const
{
    const auto names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it) {
        if (it.value() == name) return it.key();
    }
    return -1;
}

QVariant LiveBlockModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const BlockRecord &r = m_rows[index.row()];
    switch (role) {
        case KindRole:         return r.kind;
        case TextRole:         return r.text;
        case SourceRole:       return r.source;
        case HeadingLevelRole: return r.headingLevel;
        case ImageSrcRole:     return r.imageSrc;
        case ImageAltRole:     return r.imageAlt;
        case ImageTitleRole:   return r.imageTitle;
        case CodeLanguageRole: return r.codeLanguage;
        case CodeTextRole:     return r.codeText;
        case BlockAnchorRole:  return QVariant::fromValue(r.blockAnchor);
        default:               return {};
    }
}

void LiveBlockModel::setRecords(const QList<BlockRecord> &records)
{
    beginResetModel();
    m_rows = records;
    endResetModel();
}

void LiveBlockModel::speculativelyChangeKind(int row, const QString &newKind)
{
    if (row < 0 || row >= m_rows.size()) return;
    if (!m_speculativeOriginals.contains(row))
        m_speculativeOriginals[row] = m_rows[row].kind;
    m_rows[row].kind = newKind;
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx, { KindRole });
}

void LiveBlockModel::revertSpeculativeKind(int row)
{
    if (!m_speculativeOriginals.contains(row)) return;
    m_rows[row].kind = m_speculativeOriginals.take(row);
    const QModelIndex idx = index(row, 0);
    Q_EMIT dataChanged(idx, idx, { KindRole });
}

bool LiveBlockModel::isSpeculative(int row) const
{
    return m_speculativeOriginals.contains(row);
}

QString LiveBlockModel::confirmedKindAt(int row) const
{
    if (m_speculativeOriginals.contains(row))
        return m_speculativeOriginals.value(row);
    if (row < 0 || row >= m_rows.size()) return {};
    return m_rows[row].kind;
}

void LiveBlockModel::setComposingRow(int row, bool composing)
{
    if (composing) {
        m_composingRows.insert(row);
    } else {
        m_composingRows.remove(row);
        if (m_deferredDataChanged.remove(row)) {
            const QModelIndex idx = index(row, 0);
            Q_EMIT dataChanged(idx, idx);
        }
    }
}

void LiveBlockModel::applyOps(const QList<AstBlockDiff::Op> &ops,
                              const QList<BlockRecord> &nextRecords)
{
    // Parse arrived — clear all speculative state; parser result is authoritative.
    m_speculativeOriginals.clear();

    int row = 0;
    for (const auto &op : ops) {
        switch (op.kind) {
            case AstBlockDiff::OpKind::Equal: {
                if (row < m_rows.size() && op.nextIndex >= 0 && op.nextIndex < nextRecords.size()) {
                    if (m_rows[row] != nextRecords[op.nextIndex]) {
                        m_rows[row] = nextRecords[op.nextIndex];
                        const QModelIndex idx = index(row, 0);
                        if (m_composingRows.contains(row)) {
                            m_deferredDataChanged.insert(row);   // flush when composing ends
                        } else {
                            Q_EMIT dataChanged(idx, idx);
                        }
                    }
                }
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Insert: {
                beginInsertRows(QModelIndex(), row, row);
                m_rows.insert(row, nextRecords[op.nextIndex]);
                endInsertRows();
                ++row;
                break;
            }
            case AstBlockDiff::OpKind::Delete: {
                m_composingRows.remove(row);
                m_deferredDataChanged.remove(row);
                beginRemoveRows(QModelIndex(), row, row);
                m_rows.removeAt(row);
                endRemoveRows();
                // Do NOT increment row — next op references the now-shifted index.
                break;
            }
        }
    }
}

}  // namespace Markoff::View::Qml
