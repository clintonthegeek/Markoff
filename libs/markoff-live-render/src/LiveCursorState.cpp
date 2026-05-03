// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/LiveCursorState.h>
#include <markoff/live-render/BlockKindRegistry.h>
#include <markoff/live-render/LiveBlockModel.h>

#include <QAbstractItemModel>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcCursor, "markoff.live.cursor", QtWarningMsg)

namespace Markoff::LiveRender {

LiveCursorState::LiveCursorState(const BlockKindRegistry *registry,
                                 const LiveBlockModel    *model,
                                 QObject                 *parent)
    : QObject(parent)
    , m_cursor(NoCursor{})
    , m_registry(registry)
    , m_model(model)
{
    if (m_model) {
        QObject::connect(m_model, &QAbstractItemModel::rowsInserted,
                         this, &LiveCursorState::onRowsInserted);
    }
}

QString LiveCursorState::cursorKind() const
{
    if (std::holds_alternative<TextCaret>(m_cursor))           return QStringLiteral("TextCaret");
    if (std::holds_alternative<BlockSelected>(m_cursor))       return QStringLiteral("BlockSelected");
    if (std::holds_alternative<BlockInternalEdit>(m_cursor))   return QStringLiteral("BlockInternalEdit");
    return QStringLiteral("none");
}

void LiveCursorState::request(const Cursor &newCursor)
{
    if (!validateVariant(newCursor)) {
        qCWarning(lcCursor) << "cursor request rejected: invalid variant for kind";
        return;
    }
    if (m_cursor == newCursor) return;
    m_cursor = newCursor;
    Q_EMIT cursorChanged();
}

void LiveCursorState::clear()
{
    if (std::holds_alternative<NoCursor>(m_cursor)) return;
    m_cursor = NoCursor{};
    Q_EMIT cursorChanged();
}

int LiveCursorState::rowForBlock(const Markoff::BlockAnchor &block) const
{
    for (int i = 0; i < m_model->rowCount(); ++i) {
        if (m_model->recordAt(i).blockAnchor == block)
            return i;
    }
    return -1;
}

void LiveCursorState::requestTextCaretAtRow(int expectedRow, int qtPos)
{
    if (!m_model) return;
    if (expectedRow < 0) return;
    m_pendingRow = PendingRow{ expectedRow, qtPos, 0 };
    if (expectedRow < m_model->rowCount())
        resolvePendingForRow(expectedRow);
}

void LiveCursorState::noteParseArrived(quint64 /*parseSeq*/)
{
    if (!m_pendingRow) return;
    ++m_pendingRow->parseCyclesSeen;
    if (m_pendingRow->parseCyclesSeen >= 2) {
        qCInfo(lcCursor) << "pending cursor request dropped after"
                         << m_pendingRow->parseCyclesSeen
                         << "parse cycles without resolution; row"
                         << m_pendingRow->row;
        m_pendingRow.reset();
    }
}

void LiveCursorState::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    if (parent.isValid()) return;  // top-level only
    if (!m_pendingRow) return;
    if (m_pendingRow->row < first || m_pendingRow->row > last) return;
    resolvePendingForRow(m_pendingRow->row);
}

void LiveCursorState::resolvePendingForRow(int row)
{
    if (!m_model) return;
    if (row < 0 || row >= m_model->rowCount()) return;
    const int qtPos = m_pendingRow ? m_pendingRow->qtPos : 0;
    // Reset BEFORE request(): a cursorChanged consumer may re-enter and
    // call requestTextCaretAtRow; reading a stale m_pendingRow during
    // that re-entrance would produce the wrong qtPos.
    m_pendingRow.reset();

    TextCaret tc;
    tc.block            = m_model->recordAt(row).blockAnchor;
    tc.cachedByteOffset = static_cast<quint32>(qtPos);
    // positionAnchor: left default — selection projection refreshes it.
    request(tc);
}

bool LiveCursorState::validateVariant(const Cursor &c) const
{
    if (std::holds_alternative<NoCursor>(c)) return true;

    const Markoff::BlockAnchor *blockPtr = nullptr;
    if (auto *tc = std::get_if<TextCaret>(&c))               blockPtr = &tc->block;
    else if (auto *bs = std::get_if<BlockSelected>(&c))      blockPtr = &bs->block;
    else if (auto *bi = std::get_if<BlockInternalEdit>(&c))  blockPtr = &bi->block;
    if (!blockPtr) return false;

    const int row = rowForBlock(*blockPtr);
    if (row < 0) {
        qCWarning(lcCursor) << "cursor request for unknown block";
        return false;
    }

    const QString kind = m_model->recordAt(row).kind;
    const auto *desc = m_registry->find(kind);
    if (!desc) {
        qCWarning(lcCursor) << "cursor request for unregistered kind" << kind;
        return false;
    }

    QString variantName;
    if (std::holds_alternative<TextCaret>(c))            variantName = QStringLiteral("TextCaret");
    else if (std::holds_alternative<BlockSelected>(c))   variantName = QStringLiteral("BlockSelected");
    else if (std::holds_alternative<BlockInternalEdit>(c)) variantName = QStringLiteral("BlockInternalEdit");

    return desc->supportedCursorVariants.contains(variantName);
}

}  // namespace Markoff::LiveRender
