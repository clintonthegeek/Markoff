// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/TableEditBinding.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/Coordinates.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>

namespace Markoff::Live {

namespace coords = ::Markoff::Live::Detail::Coordinates;

TableEditBinding::TableEditBinding(QObject *parent)
    : QObject(parent) {}

TableEditBinding::~TableEditBinding() = default;

LiveListModelBinding *TableEditBinding::binding() const
{
    return m_binding.data();
}

void TableEditBinding::setBinding(LiveListModelBinding *b)
{
    if (m_binding.data() == b) return;
    m_binding = b;
    Q_EMIT bindingChanged();
}

int TableEditBinding::modelIndex() const
{
    return m_modelIndex;
}

void TableEditBinding::setModelIndex(int row)
{
    if (m_modelIndex == row) return;
    m_modelIndex = row;
    Q_EMIT modelIndexChanged();
}

void TableEditBinding::applyCellEdit(int cellStartCharPos,
                                     int cellQtPos,
                                     int removed,
                                     const QString &added)
{
    // FALSIFIABILITY STUB — reverted by the companion commit. Proves
    // tst_live_render_table_cell_edit fails when the cell edit doesn't
    // reach d2ApplyBufferEdit.
    (void)cellStartCharPos; (void)cellQtPos; (void)removed; (void)added;
    return;

    if (!m_binding || !m_binding->document() || !m_binding->model()) return;
    if (m_modelIndex < 0) return;
    if (m_modelIndex >= m_binding->model()->rowCount()) return;
    if (cellStartCharPos < 0 || cellQtPos < 0 || removed < 0) return;

    auto *doc   = m_binding->document();
    auto *model = m_binding->model();
    const auto &record = model->recordAt(m_modelIndex);

    // Authoritative pre-edit buffer. Query the CRDT directly rather
    // than the model's cached `record.text` so we're not racing the
    // `onD2Changed` cascade — applyCellEdit may fire from a cell's
    // contentsChange handler before the model has been notified of
    // the prior edit.
    const QByteArray preUtf8 = doc->blockText(record.blockAnchor);

    const int absoluteCharPos = cellStartCharPos + cellQtPos;
    const uint32_t byteOff = static_cast<uint32_t>(
        coords::qtPosToByte(preUtf8, absoluteCharPos));
    const uint32_t removedBytes = static_cast<uint32_t>(
        coords::qtPosToByte(preUtf8, absoluteCharPos + removed)) - byteOff;
    const QByteArray addedBytes = added.toUtf8();

    auto &undoLog = doc->d2UndoLog();
    UndoLog::Transaction t(undoLog);
    doc->d2ApplyBufferEdit(record.blockAnchor, byteOff, removedBytes,
                           addedBytes, t);

    // Flush so the model + delegate rebuild see the edit synchronously,
    // matching LiveEditBinding's contract for in-process edits.
    doc->flushPendingD2Changed();
}

}  // namespace Markoff::Live
