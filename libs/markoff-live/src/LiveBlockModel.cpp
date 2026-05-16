// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveBlockModel.h>

#include <markoff/core/BlockAnchor.h>
#include <markoff/core/AttrNames.h>

#include <QVariantMap>
#include <variant>

namespace Markoff::Live {

LiveBlockModel::LiveBlockModel(QObject *parent) : QAbstractListModel(parent)
{
    qRegisterMetaType<Markoff::SourceSpan>("Markoff::SourceSpan");
    qRegisterMetaType<QList<Markoff::SourceSpan>>("QList<Markoff::SourceSpan>");
}

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
        { HeadingFormRole,  "headingForm" },
        { CodeLanguageRole, "codeLanguage" },
        { BlockAnchorRole,  "blockAnchor" },
        { BlockAttrsRole,   "blockAttrs" },
        { MarkerStyleRole,  "markerStyle" },
        { MarkerNumberRole, "markerNumber" },
        { IndentLevelRole,  "indentLevel" },
        { CheckedRole,      "checked" },
        { LooseRunRole,     "looseRun" },
        { InlineSpansRole,  "inlineSpans" },
        { DelegateClassRole, "delegateClass" },
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
        case HeadingFormRole:   return r.headingForm;
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
        case InlineSpansRole:   return QVariant::fromValue(r.inlineSpans);
        case DelegateClassRole: return r.delegateClass;
        default:                return {};
    }
}

void LiveBlockModel::applyOps(const QList<AstBlockDiff::Op> &ops,
                              const QList<BlockRecord> &nextRecords,
                              quint64 parseInputEditSeq)
{
    // Detect kind-change-only ops (Delete immediately followed by Insert at the
    // same row, blockAnchor preserved). Plain Delete+Insert via Q_EMIT
    // rowsRemoved+rowsInserted does NOT cause DelegateChooser to instantiate
    // the new delegate type — ListView's pool reuses the previous delegate
    // bound to a stale `modelIndex=-1`, and the new template (e.g. HeadingDelegate
    // for paragraph→heading) is never created. A model reset forces a clean
    // re-realization. Identify the case and short-circuit through reset.
    //
    // Note: model reset would normally also reset QQuickListView's contentY to
    // 0 (visible as "view jumps to top when typing `#`"). LiveView.qml's
    // Connections on this model's modelAboutToBeReset/modelReset signals
    // snapshots and restores contentY around the reset to suppress the jump.
    bool kindOnlySwap = false;
    if (ops.size() == int(nextRecords.size()) + 1 && m_rows.size() == nextRecords.size()) {
        // Same row count before+after; one extra op means a Delete+Insert pair.
        // Walk and check: must be all Equal except one Delete followed by an
        // Insert at the same logical row, with matching blockAnchor.
        int deletedAt = -1;
        int insertedNextIdx = -1;
        int idx = 0, opPos = 0;
        bool ok = true;
        for (; opPos < ops.size(); ++opPos) {
            const auto &op = ops[opPos];
            if (op.kind == AstBlockDiff::OpKind::Delete && deletedAt < 0) {
                deletedAt = idx;
            } else if (op.kind == AstBlockDiff::OpKind::Insert && deletedAt == idx
                       && insertedNextIdx < 0) {
                insertedNextIdx = op.nextIndex;
                ++idx;
            } else if (op.kind == AstBlockDiff::OpKind::Equal) {
                ++idx;
            } else {
                ok = false;
                break;
            }
        }
        if (ok && deletedAt >= 0 && insertedNextIdx >= 0
            && m_rows[deletedAt].blockAnchor == nextRecords[insertedNextIdx].blockAnchor
            && m_rows[deletedAt].kind       != nextRecords[insertedNextIdx].kind) {
            kindOnlySwap = true;
        }
    }
    if (kindOnlySwap) {
        beginResetModel();
        m_rows.clear();
        m_rowEditSequences.clear();
        for (const auto &r : nextRecords) {
            m_rows.append(r);
            m_rowEditSequences.append(quint64(0));
        }
        endResetModel();
        return;
    }

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
                // Kind change inside an Equal op (e.g. Paragraph→Heading via
                // typing `# `): a plain dataChanged does NOT cause
                // `DelegateChooser` to swap the delegate — the chooser binds
                // delegate type at create-time and listens only to row insert/
                // remove. Synthesise a remove+insert here so the chooser
                // destroys the old kind's delegate and creates the new one,
                // which is required for the chokepoint's `delegateAvailable`
                // / `delegateGoingAway` flow to deliver focus to the right
                // delegate after a kind transition.
                if (m_rows[row] != merged) {
                    m_rows[row] = merged;
                    Q_EMIT dataChanged(index(row), index(row));
                } else if (m_rows[row].inlineSpans != merged.inlineSpans) {
                    // Non-span fields are identical; only spans changed.
                    m_rows[row].inlineSpans = merged.inlineSpans;
                    Q_EMIT dataChanged(index(row), index(row), {InlineSpansRole});
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

QList<Markoff::SourceSpan> LiveBlockModel::spansAtRow(int row) const
{
    if (row < 0 || row >= m_rows.size()) return {};
    return m_rows[row].inlineSpans;
}

void LiveBlockModel::insertTestRow(Markoff::BlockAnchor anchor,
                                   const QString &kind,
                                   const QString &text)
{
    BlockRecord r;
    r.blockAnchor = anchor;
    r.kind = kind;
    r.text = text;
    m_rows.append(r);
    m_rowEditSequences.append(0);
}

QString LiveBlockModel::kindFor(Markoff::BlockAnchor anchor) const
{
    for (const auto &r : m_rows)
        if (r.blockAnchor == anchor) return r.kind;
    return {};
}

}  // namespace Markoff::Live
