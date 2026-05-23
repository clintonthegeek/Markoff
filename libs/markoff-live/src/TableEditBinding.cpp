// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/TableEditBinding.h>
#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/Coordinates.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/UndoLog.h>
#include <markoff/parser/SourceSpan.h>

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

QVariantList TableEditBinding::inlineSpansForCell(
    const QVariant &blockSpans,
    int cellStartChar, int cellEndChar) const
{
    QVariantList out;
    if (cellEndChar <= cellStartChar) return out;
    // QML hands the model role through as either a typed
    // QList<SourceSpan> (the InlineSpansRole's wrapped value) or a
    // QVariantList (rare; defensive). Try the typed unwrap first.
    QList<Markoff::SourceSpan> spans;
    if (blockSpans.canConvert<QList<Markoff::SourceSpan>>()) {
        spans = blockSpans.value<QList<Markoff::SourceSpan>>();
    } else if (blockSpans.canConvert<QVariantList>()) {
        for (const QVariant &v : blockSpans.toList()) {
            if (v.canConvert<Markoff::SourceSpan>())
                spans.append(v.value<Markoff::SourceSpan>());
        }
    }
    out.reserve(spans.size());
    for (Markoff::SourceSpan s : spans) {
        const int spanStart = s.charOffset;
        const int spanEnd   = s.charOffset + s.charLength;
        // Strict containment — partial overlaps would paint across the
        // pipe boundary inside the cell document. Tree-sitter places
        // inline spans inside `pipe_table_cell` byte ranges (post-grammar
        // fix), so partial overlaps shouldn't arise in practice; the
        // strict check is defensive against future grammar changes.
        if (spanStart < cellStartChar || spanEnd > cellEndChar) continue;
        s.charOffset = spanStart - cellStartChar;
        if (s.parentCharStart >= 0) {
            s.parentCharStart -= cellStartChar;
            s.parentCharEnd   -= cellStartChar;
            // Clamp parent range to the cell document. Without this, the
            // delimiter-visibility test (`caret within parent range`) sees
            // a parent that extends past the cell's QTextDocument length;
            // the clamp keeps the value self-consistent for any future
            // consumer that uses it.
            const int cellLen = cellEndChar - cellStartChar;
            if (s.parentCharStart < 0)        s.parentCharStart = 0;
            if (s.parentCharEnd   > cellLen)  s.parentCharEnd   = cellLen;
        }
        out.append(QVariant::fromValue(s));
    }
    return out;
}

}  // namespace Markoff::Live
