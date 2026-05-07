// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveBlockModel.h>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/AttrNames.h>

#include <QVariantMap>
#include <variant>

namespace Markoff::Live {

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
        { BlockAttrsRole,   "blockAttrs" },
        { MarkerStyleRole,  "markerStyle" },
        { MarkerNumberRole, "markerNumber" },
        { IndentLevelRole,  "indentLevel" },
        { CheckedRole,      "checked" },
        { LooseRunRole,     "looseRun" },
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
        case BlockAttrsRole: {
            const auto &a = r.attrs;
            QVariantMap map;
            for (auto it = a.cbegin(); it != a.cend(); ++it) {
                const QString key = QString::fromLatin1(it.key());
                map.insert(key, std::visit([](auto &&v) -> QVariant {
                    return QVariant::fromValue(v);
                }, it.value()));
            }
            return map;
        }
        case MarkerStyleRole: {
            auto it = r.attrs.constFind(Markoff::AttrNames::MarkerStyle);
            if (it != r.attrs.constEnd()) {
                if (const auto *v = std::get_if<QString>(&it.value())) return *v;
            }
            return QString{};
        }
        case MarkerNumberRole: {
            auto it = r.attrs.constFind(Markoff::AttrNames::MarkerNumber);
            if (it != r.attrs.constEnd()) {
                if (const auto *v = std::get_if<int>(&it.value())) return *v;
            }
            return 0;
        }
        case IndentLevelRole: {
            auto it = r.attrs.constFind(Markoff::AttrNames::IndentLevel);
            if (it != r.attrs.constEnd()) {
                if (const auto *v = std::get_if<int>(&it.value())) return *v;
            }
            return 0;
        }
        case CheckedRole: {
            auto it = r.attrs.constFind(Markoff::AttrNames::Checked);
            if (it != r.attrs.constEnd()) {
                if (const auto *v = std::get_if<bool>(&it.value())) return *v;
            }
            return false;
        }
        case LooseRunRole: {
            auto it = r.attrs.constFind(Markoff::AttrNames::LooseRun);
            if (it != r.attrs.constEnd()) {
                if (const auto *v = std::get_if<bool>(&it.value())) return *v;
            }
            return false;
        }
        default:                return {};
    }
}

void LiveBlockModel::applyOps(const QList<AstBlockDiff::Op> &ops,
                              const QList<BlockRecord> &nextRecords,
                              quint64 parseInputEditSeq)
{
    int row = 0;
    for (const auto &op : ops) {
        switch (op.kind) {
            case AstBlockDiff::OpKind::Equal: {
                const BlockRecord &next = nextRecords[op.nextIndex];
                BlockRecord merged = next;
                // D2: parseInputEditSeq is always std::numeric_limits<quint64>::max() in D2
                // (every row is always fresh; the stale-text preservation path is dead).
                // Cleanup deferred to Phase 14.
                const bool fresh = (m_rowEditSequences[row] <= parseInputEditSeq);
                if (!fresh) {
                    // Stale: keep our text; accept everything else from parse.
                    merged.text = m_rows[row].text;
                }
                const Markoff::BlockAnchor oldAnchor = m_rows[row].blockAnchor;
                const bool anchorChanged = (oldAnchor != merged.blockAnchor);
                if (m_rows[row] != merged) {
                    m_rows[row] = merged;
                    Q_EMIT dataChanged(index(row), index(row));
                    if (anchorChanged) {
                        // AstBlockDiff collapsed a Delete+Insert pair into
                        // this Equal — the foundation handed us a new anchor
                        // for the same logical block (typing at qtPos 0
                        // changes the block's first-byte character identity).
                        // Notify listeners so cursor anchors survive.
                        Q_EMIT anchorRenumbered(row, oldAnchor, merged.blockAnchor);
                    }
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

}  // namespace Markoff::Live
