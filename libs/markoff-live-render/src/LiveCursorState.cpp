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
    // Default signal source is the inner LiveBlockModel; LiveListModelBinding
    // calls setSignalModel(proxy) once the proxy exists so resolution fires
    // after the proxy's row state is consistent with QML's view.
    setSignalModel(const_cast<LiveBlockModel *>(model));

    // Survive the foundation's per-keystroke BlockAnchor renumbering at
    // qtPos 0 of a block. The model emits anchorRenumbered when an
    // Equal-op rewrite (collapsed Delete+Insert) replaces a row's anchor
    // in place; if our cursor's TextCaret is pinned to the old anchor,
    // we re-pin it to the new one without firing cursorChanged (the
    // visible delegate is unchanged; we only swap the identity).
    if (m_model) {
        QObject::connect(m_model, &LiveBlockModel::anchorRenumbered,
                         this, &LiveCursorState::onAnchorRenumbered);
    }
}

void LiveCursorState::onAnchorRenumbered(int /*row*/,
                                          Markoff::BlockAnchor oldAnchor,
                                          Markoff::BlockAnchor newAnchor)
{
    auto *tc = std::get_if<TextCaret>(&m_cursor);
    if (!tc) return;
    if (std::holds_alternative<HoleBlockId>(tc->block)) return;
    if (anchorOf(tc->block) != oldAnchor) return;
    qInfo().noquote() << "[dogfood] CursorState: onAnchorRenumbered swap-in-place";
    // In-place swap: don't emit cursorChanged. The QML delegate is the
    // same QQuickItem — re-firing focusEditAt would clobber the user's
    // current cursor position inside the TextEdit.
    tc->block = BlockId{newAnchor};
}

void LiveCursorState::setSignalModel(QAbstractItemModel *signalModel)
{
    if (m_signalModel == signalModel) return;
    if (m_signalModel) {
        QObject::disconnect(m_signalModel, &QAbstractItemModel::rowsInserted,
                            this, &LiveCursorState::onRowsInserted);
    }
    m_signalModel = signalModel;
    if (m_signalModel) {
        QObject::connect(m_signalModel, &QAbstractItemModel::rowsInserted,
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

quint64 LiveCursorState::focusedHoleId() const
{
    if (const auto *tc = std::get_if<TextCaret>(&m_cursor)) {
        if (std::holds_alternative<HoleBlockId>(tc->block))
            return std::get<HoleBlockId>(tc->block).holeId;
    }
    return 0;
}

int LiveCursorState::focusedAnchorRow() const
{
    if (!m_model) return -1;
    if (const auto *tc = std::get_if<TextCaret>(&m_cursor)) {
        if (std::holds_alternative<HoleBlockId>(tc->block)) return -1;
        return rowForBlock(anchorOf(tc->block));
    }
    return -1;
}

int LiveCursorState::focusedQtPos() const
{
    if (const auto *tc = std::get_if<TextCaret>(&m_cursor))
        return static_cast<int>(tc->cachedByteOffset);
    return -1;
}

void LiveCursorState::request(const Cursor &newCursor)
{
    if (!validateVariant(newCursor)) {
        qCWarning(lcCursor) << "cursor request rejected: invalid variant for kind";
        return;
    }
    // Explicit request supersedes any pending structural delivery. Without
    // this, a later rowsInserted could resolve a stale pending and clobber
    // the cursor we are trying to set right now (e.g. Enter-on-hole-at-EOB
    // commits the old hole AND opens a new one in immediate succession; the
    // commit's pending must not overwrite the new-hole request).
    m_pendingRow.reset();
    if (m_cursor == newCursor) return;
    m_cursor = newCursor;

    QString desc = QStringLiteral("None");
    if (auto *tc = std::get_if<TextCaret>(&newCursor)) {
        if (std::holds_alternative<HoleBlockId>(tc->block)) {
            desc = QStringLiteral("TextCaret(holeId=%1, qtPos=%2)")
                       .arg(std::get<HoleBlockId>(tc->block).holeId)
                       .arg(tc->cachedByteOffset);
        } else {
            const int row = m_model ? rowForBlock(anchorOf(tc->block)) : -1;
            desc = QStringLiteral("TextCaret(innerRow=%1, qtPos=%2)")
                       .arg(row).arg(tc->cachedByteOffset);
        }
    } else if (std::holds_alternative<NoCursor>(newCursor)) {
        desc = QStringLiteral("NoCursor");
    }
    qInfo().noquote() << "[dogfood] CursorState: request" << desc;

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
    if (!m_signalModel) return;
    if (expectedRow < 0) return;
    qInfo().noquote() << "[dogfood] CursorState: requestTextCaretAtRow row=" << expectedRow
                      << "qtPos=" << qtPos
                      << "(signalModel.rowCount=" << m_signalModel->rowCount() << ")";
    m_pendingRow = PendingRow{ expectedRow, qtPos, 0 };
    if (expectedRow < m_signalModel->rowCount())
        resolvePendingForRow(expectedRow);
}

void LiveCursorState::requestTextCaretAtNewRow(int expectedRow, int qtPos)
{
    if (!m_signalModel) return;
    if (expectedRow < 0) return;
    qInfo().noquote() << "[dogfood] CursorState: requestTextCaretAtNewRow row=" << expectedRow
                      << "qtPos=" << qtPos
                      << "(signalModel.rowCount=" << m_signalModel->rowCount() << ")";
    // Pure-pending: do NOT resolve against the current row at this index —
    // that would land the cursor on whatever block currently sits there
    // (the block that's about to be SHIFTED by the upcoming insertion).
    // Wait for the next rowsInserted whose range covers expectedRow.
    m_pendingRow = PendingRow{ expectedRow, qtPos, 0 };
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
    qInfo().noquote() << "[dogfood] CursorState: onRowsInserted [" << first << "," << last << "]"
                      << "pending=" << (m_pendingRow ? QString::number(m_pendingRow->row) : QStringLiteral("none"));
    if (!m_pendingRow) return;
    if (m_pendingRow->row < first || m_pendingRow->row > last) return;
    resolvePendingForRow(m_pendingRow->row);
}

void LiveCursorState::resolvePendingForRow(int row)
{
    if (!m_signalModel) return;
    if (row < 0 || row >= m_signalModel->rowCount()) return;

    // Look up the BlockAnchor via the signal model's data() so this works
    // whether the signal model is the inner LiveBlockModel or the proxy
    // (which passes BlockAnchorRole through for non-hole rows).
    const QVariant v = m_signalModel->data(
        m_signalModel->index(row, 0), LiveBlockModel::BlockAnchorRole);
    if (!v.isValid() || !v.canConvert<Markoff::BlockAnchor>()) {
        // Hole rows return an empty QVariant for BlockAnchorRole; the
        // pending mechanism is anchor-side only. Hole-row carets are set
        // directly by LiveStructuralKeyHandler via request(HoleBlockId).
        return;
    }

    const int qtPos = m_pendingRow ? m_pendingRow->qtPos : 0;
    // Reset BEFORE request(): a cursorChanged consumer may re-enter and
    // call requestTextCaretAtRow; reading a stale m_pendingRow during
    // that re-entrance would produce the wrong qtPos.
    m_pendingRow.reset();

    TextCaret tc;
    tc.block            = BlockId{v.value<Markoff::BlockAnchor>()};
    tc.cachedByteOffset = static_cast<quint32>(qtPos);
    // positionAnchor: left default — selection projection refreshes it.
    request(tc);
}

bool LiveCursorState::validateVariant(const Cursor &c) const
{
    if (std::holds_alternative<NoCursor>(c)) return true;

    const BlockId *blockIdPtr = nullptr;
    if (auto *tc = std::get_if<TextCaret>(&c))               blockIdPtr = &tc->block;
    else if (auto *bs = std::get_if<BlockSelected>(&c))      blockIdPtr = &bs->block;
    else if (auto *bi = std::get_if<BlockInternalEdit>(&c))  blockIdPtr = &bi->block;
    if (!blockIdPtr) return false;

    // Holes don't (yet) pass through validateVariant — they bypass normal
    // validation because LiveHoleLayer manages their lifecycle directly.
    // Task 4: all live cursors are anchor-side; hole path is reserved for R5.5+.
    if (isHoleBlockId(*blockIdPtr)) return true;

    const int row = rowForBlock(anchorOf(*blockIdPtr));
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
