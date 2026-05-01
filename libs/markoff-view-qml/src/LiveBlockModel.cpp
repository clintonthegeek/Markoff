// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/view/qml/LiveBlockModel.h>

#include <markoff-foundation/BlockAnchor.h>

namespace Markoff::View::Qml {

LiveBlockModel::LiveBlockModel(QObject *parent) : QAbstractListModel(parent) {}

int LiveBlockModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_rows.size() + m_holes.size();
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
    const int viewRow = index.row();
    if (viewRow < 0 || viewRow >= rowCount()) return {};

    const int holeIdx = viewRowToHole(viewRow);
    if (holeIdx >= 0) {
        const HoleRow &h = m_holes[holeIdx];
        switch (role) {
            case KindRole:        return h.kind;
            case TextRole:        return QString();
            case SourceRole:      return QString();
            case BlockAnchorRole: return QVariant::fromValue(Markoff::BlockAnchor{});
            case IsHoleRole:      return true;
            case HoleIdRole:      return QVariant::fromValue(h.id);
            default:              return {};
        }
    }

    const int parsedRow = viewRowToParsed(viewRow);
    if (parsedRow < 0 || parsedRow >= m_rows.size()) return {};
    const BlockRecord &r = m_rows[parsedRow];
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
    // Drop any holes whose anchoring parsed row no longer exists.
    QList<HoleRow> kept;
    kept.reserve(m_holes.size());
    for (const HoleRow &h : m_holes) {
        if (h.afterParsedRow >= -1 && h.afterParsedRow < m_rows.size())
            kept.append(h);
    }
    m_holes = std::move(kept);
    endResetModel();
}

// ---------------------------------------------------------------
// Hole interleaving (Stage 4 / T17)
// ---------------------------------------------------------------

int LiveBlockModel::viewRowForParsed(int parsedRow) const
{
    // The view row of parsed row P = P + (number of holes whose
    // afterParsedRow < P).
    int viewRow = parsedRow;
    for (const HoleRow &h : m_holes) {
        if (h.afterParsedRow < parsedRow) ++viewRow;
    }
    return viewRow;
}

int LiveBlockModel::viewRowForHole(int holeIndex) const
{
    if (holeIndex < 0 || holeIndex >= m_holes.size()) return -1;
    const HoleRow &target = m_holes[holeIndex];
    // The view row of a hole H following parsed row P =
    //    (P+1) + (parsed rows counted as P or below already accounted)
    //  + (other holes that also follow rows <= P AND come earlier in storage
    //    order or have lower afterParsedRow).
    // Simpler walk: count parsed rows up to & including target.afterParsedRow,
    // then count holes (in m_holes order) up to and including this one whose
    // afterParsedRow == target.afterParsedRow OR less.
    int viewRow = target.afterParsedRow + 1;  // first slot after the parent
    for (int i = 0; i < holeIndex; ++i) {
        if (m_holes[i].afterParsedRow <= target.afterParsedRow)
            ++viewRow;
    }
    return viewRow;
}

int LiveBlockModel::viewRowToParsed(int viewRow) const
{
    // Walk parsed rows in order, counting holes that precede each.
    // A view row is a parsed row P iff sum_{P' < P, holes after P' < P} == 0
    // adjusted by leading offset.
    int cursor = 0;
    for (int p = 0; p < m_rows.size(); ++p) {
        if (cursor == viewRow) return p;
        ++cursor;
        // Insert any holes following this parsed row.
        for (const HoleRow &h : m_holes) {
            if (h.afterParsedRow == p) {
                if (cursor == viewRow) return -1;  // it's a hole
                ++cursor;
            }
        }
    }
    return -1;  // viewRow points to a trailing hole, or out of range
}

int LiveBlockModel::viewRowToHole(int viewRow) const
{
    int cursor = 0;
    for (int p = 0; p < m_rows.size(); ++p) {
        if (cursor == viewRow) return -1;  // parsed row
        ++cursor;
        for (int i = 0; i < m_holes.size(); ++i) {
            if (m_holes[i].afterParsedRow != p) continue;
            if (cursor == viewRow) return i;
            ++cursor;
        }
    }
    // Trailing holes (afterParsedRow == m_rows.size() - 1 already covered;
    // afterParsedRow == -1 also covered separately below).
    // Handle holes with afterParsedRow < 0: prepended (not used in v0).
    return -1;
}

bool LiveBlockModel::isHoleRow(int viewRow) const
{
    return viewRowToHole(viewRow) >= 0;
}

int LiveBlockModel::parsedRowForViewRow(int viewRow) const
{
    return viewRowToParsed(viewRow);
}

int LiveBlockModel::viewRowForParsedRow(int parsedRow) const
{
    return viewRowForParsed(parsedRow);
}

quint64 LiveBlockModel::holeIdAt(int viewRow) const
{
    const int idx = viewRowToHole(viewRow);
    if (idx < 0) return 0;
    return m_holes[idx].id;
}

int LiveBlockModel::insertHole(quint64 holeId, int afterParsedRow, const QString &kind)
{
    if (afterParsedRow < -1 || afterParsedRow >= m_rows.size()) return -1;
    HoleRow h;
    h.id = holeId;
    h.afterParsedRow = afterParsedRow;
    h.kind = kind.isEmpty() ? QStringLiteral("paragraph") : kind;

    // Compute the target view row: just after the last hole already attached
    // to the same parsed row (so newest holes appear last among siblings).
    int viewRow = afterParsedRow + 1;
    for (const HoleRow &existing : m_holes) {
        if (existing.afterParsedRow <= afterParsedRow) ++viewRow;
    }

    beginInsertRows(QModelIndex(), viewRow, viewRow);
    m_holes.append(h);
    endInsertRows();
    return viewRow;
}

int LiveBlockModel::removeHole(quint64 holeId)
{
    int idx = -1;
    for (int i = 0; i < m_holes.size(); ++i) {
        if (m_holes[i].id == holeId) { idx = i; break; }
    }
    if (idx < 0) return -1;
    const int viewRow = viewRowForHole(idx);
    beginRemoveRows(QModelIndex(), viewRow, viewRow);
    m_holes.removeAt(idx);
    endRemoveRows();
    return viewRow;
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
    // Parse arrived — clear all speculative state; parser result is authoritative.
    m_speculativeOriginals.clear();

    int row = 0;
    for (const auto &op : ops) {
        switch (op.kind) {
            case AstBlockDiff::OpKind::Equal: {
                if (row < m_rows.size() && op.nextIndex >= 0 && op.nextIndex < nextRecords.size()) {
                    if (m_rows[row] != nextRecords[op.nextIndex]) {
                        m_rows[row] = nextRecords[op.nextIndex];
                        const int viewRow = viewRowForParsed(row);
                        const QModelIndex idx = index(viewRow, 0);
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
                const int viewRow = viewRowForParsed(row);
                beginInsertRows(QModelIndex(), viewRow, viewRow);
                m_rows.insert(row, nextRecords[op.nextIndex]);
                // Holes anchored to row >= the insertion point shift right.
                for (HoleRow &h : m_holes) {
                    if (h.afterParsedRow >= row) ++h.afterParsedRow;
                }
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
                // Drop any holes anchored to the row being deleted; shift
                // holes anchored to higher rows down by one.
                {
                    QList<HoleRow> kept;
                    kept.reserve(m_holes.size());
                    for (const HoleRow &h : m_holes) {
                        if (h.afterParsedRow == row) continue;  // dropped
                        HoleRow shifted = h;
                        if (shifted.afterParsedRow > row) --shifted.afterParsedRow;
                        kept.append(shifted);
                    }
                    m_holes = std::move(kept);
                }
                const int viewRow = viewRowForParsed(row);
                beginRemoveRows(QModelIndex(), viewRow, viewRow);
                m_rows.removeAt(row);
                endRemoveRows();
                // Do NOT increment row — next op references the now-shifted index.
                break;
            }
        }
    }
}

}  // namespace Markoff::View::Qml
