// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveBlockModel.h>

#include <markoff-foundation/BlockAnchor.h>

namespace Markoff::View::Qml {

LiveBlockModel::LiveBlockModel(QObject *parent) : QAbstractListModel(parent) {}

int LiveBlockModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_rows.size() + (m_hole.has_value() ? 1 : 0);
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
        { IsHoleRole,       "isHole" },
        { HoleIdRole,       "holeId" },
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
    if (!index.isValid()) return {};
    const int row = index.row();
    const int total = rowCount();
    if (row < 0 || row >= total) return {};

    // Hole row first (it sits at viewRow = afterParsedRow + 1).
    if (m_hole.has_value() && row == holeViewRowFor(*m_hole)) {
        switch (role) {
            case KindRole:         return m_hole->kind;
            case TextRole:         return m_hole->bufferText;
            case SourceRole:       return QString();
            case BlockAnchorRole:  return QVariant::fromValue(Markoff::BlockAnchor{});
            case IsHoleRole:       return true;
            case HoleIdRole:       return QVariant::fromValue(m_hole->id);
            case HeadingLevelRole: return 0;
            case ImageSrcRole:
            case ImageAltRole:
            case ImageTitleRole:
            case CodeLanguageRole:
            case CodeTextRole:     return QString();
            default:               return {};
        }
    }

    // Translate viewRow → records index, accounting for the hole inserted
    // before this row (if any).
    int recIdx = row;
    if (m_hole.has_value() && holeViewRowFor(*m_hole) < row) {
        recIdx = row - 1;
    }
    if (recIdx < 0 || recIdx >= m_rows.size()) return {};
    const BlockRecord &r = m_rows[recIdx];
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
        case IsHoleRole:       return false;
        case HoleIdRole:       return QVariant::fromValue<quint64>(0);
        default:               return {};
    }
}

void LiveBlockModel::setRecords(const QList<BlockRecord> &records)
{
    beginResetModel();
    m_rows = records;
    // Resetting wipes any pending hole — `setRecords` is only used at
    // initialization / wholesale rebuild paths.
    m_hole.reset();
    endResetModel();
}

void LiveBlockModel::insertHoleRow(quint64 holeId, const QString &kind,
                                   const QString &bufferText, int afterParsedRow)
{
    Q_ASSERT_X(!m_hole.has_value(), "LiveBlockModel::insertHoleRow",
               "v1 invariant violation: hole row already present");
    HoleRow h;
    h.id = holeId;
    h.kind = kind;
    h.bufferText = bufferText;
    h.afterParsedRow = afterParsedRow;
    const int viewRow = holeViewRowFor(h);
    beginInsertRows(QModelIndex(), viewRow, viewRow);
    m_hole = h;
    endInsertRows();
}

void LiveBlockModel::setHoleBufferText(const QString &bufferText)
{
    if (!m_hole.has_value()) return;
    if (m_hole->bufferText == bufferText) return;
    m_hole->bufferText = bufferText;
    const int viewRow = holeViewRowFor(*m_hole);
    const QModelIndex idx = index(viewRow, 0);
    Q_EMIT dataChanged(idx, idx, { TextRole });
}

void LiveBlockModel::removeHoleRow()
{
    if (!m_hole.has_value()) return;
    const int viewRow = holeViewRowFor(*m_hole);
    beginRemoveRows(QModelIndex(), viewRow, viewRow);
    m_hole.reset();
    endRemoveRows();
}

int LiveBlockModel::holeViewRow() const
{
    if (!m_hole.has_value()) return -1;
    return holeViewRowFor(*m_hole);
}

quint64 LiveBlockModel::holeId() const
{
    return m_hole.has_value() ? m_hole->id : 0;
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
        m_composingAnchor = (row >= 0 && row < m_rows.size())
            ? std::optional<Markoff::BlockAnchor>(m_rows[row].blockAnchor)
            : std::nullopt;
        m_hasDeferredDataChanged = false;
    } else {
        if (m_composingAnchor.has_value() && m_hasDeferredDataChanged) {
            // Find the current row for this anchor (may have shifted via ops).
            for (int i = 0; i < m_rows.size(); ++i) {
                if (m_rows[i].blockAnchor == m_composingAnchor.value()) {
                    Q_EMIT dataChanged(index(i, 0), index(i, 0));
                    break;
                }
            }
        }
        m_composingAnchor = std::nullopt;
        m_hasDeferredDataChanged = false;
    }
}

void LiveBlockModel::applyOps(const QList<AstBlockDiff::Op> &ops,
                              const QList<BlockRecord> &nextRecords)
{
    // Contract: parse-driven ops must not interleave with a pending hole.
    // The projection layer is responsible for committing or dropping the
    // hole BEFORE allowing parse ops to land. `commitBlockHole` removes the
    // hole synchronously before issuing `applyLocalEdit`; `dropBlockHole`
    // removes it without any source mutation.
    Q_ASSERT_X(!m_hole.has_value(), "LiveBlockModel::applyOps",
               "applyOps called while a hole row is present");

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
                        // Defer the notification if this row is currently composing
                        // (identified by anchor, not row number, for Insert/Delete safety).
                        if (m_composingAnchor.has_value()
                                && m_rows[row].blockAnchor == m_composingAnchor.value()) {
                            m_hasDeferredDataChanged = true;
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
                // If the composing block is being deleted, clear composing state.
                if (m_composingAnchor.has_value()
                        && row < m_rows.size()
                        && m_rows[row].blockAnchor == m_composingAnchor.value()) {
                    m_composingAnchor = std::nullopt;
                    m_hasDeferredDataChanged = false;
                }
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
