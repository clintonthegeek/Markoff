// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/LiveClipboardController.h>

#include <markoff/live/LiveBlockModel.h>
#include <markoff/live/LiveCursorState.h>
#include <markoff/live/Coordinates.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/PasteMeta.h>

#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMimeData>
#include <QStringList>

namespace Markoff::Live {

namespace coords = Detail::Coordinates;

LiveClipboardController::LiveClipboardController(QObject *parent) : QObject(parent) {}

void LiveClipboardController::setDocument(Markoff::MarkoffDocument *doc) { m_document = doc; }
void LiveClipboardController::setSelectionView(LiveCursorState *sv) { m_selection = sv; }
void LiveClipboardController::setModel(const LiveBlockModel *model) { m_model = model; }
void LiveClipboardController::setReadOnlyProvider(std::function<bool()> provider)
{
    m_readOnlyProvider = std::move(provider);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// Serialize a single block's `attrs` (QHash<AttrName, AttrValue>) to a
/// JSON object. AttrValue is a `std::variant<int, QString, bool>` so the
/// JSON keeps each attr's native type (heading level as int, marker style
/// as QString, checkbox state as bool, etc.). The keys must match the
/// `Markoff::AttrNames::*` constants exactly so `reconstructFlatMarkdown`
/// in markoff-core can read them back.
QJsonObject attrsToJson(const QHash<Markoff::AttrName, Markoff::AttrValue> &attrs)
{
    QJsonObject out;
    for (auto it = attrs.cbegin(); it != attrs.cend(); ++it) {
        const QString key = QString::fromUtf8(it.key());
        std::visit([&](const auto &val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, int>)
                out[key] = val;
            else if constexpr (std::is_same_v<T, QString>)
                out[key] = val;
            else if constexpr (std::is_same_v<T, bool>)
                out[key] = val;
        }, it.value());
    }
    return out;
}

/// Serialize the selected spans of each block to a JSON array.
/// Each object has "kind" (QString), "text" (selected substring), and
/// "attrs" (per-block attrs JSON, when non-empty). The attrs are required
/// so the structured-paste path can reconstruct kind-specific markdown
/// prefixes (heading `#`, list-item marker, code-fence info string).
QJsonArray serializeSelection(const LiveCursorState &sel, const LiveBlockModel &model)
{
    QJsonArray out;
    if (!sel.hasSelection()) return out;
    const int rowCount = model.rowCount();
    for (int i = 0; i < rowCount; ++i) {
        const QPoint r = sel.rangeForBlock(i);
        if (r.x() < 0) continue;  // block not in selection
        const auto &rec = model.recordAt(i);
        const int startPos = r.x();
        const int endPos   = (r.y() == INT_MAX) ? rec.text.length() : r.y();
        const QString text = rec.text.mid(startPos, endPos - startPos);
        QJsonObject obj;
        obj["kind"] = rec.kind;
        obj["text"] = text;
        const QJsonObject attrs = attrsToJson(rec.attrs);
        if (!attrs.isEmpty()) obj["attrs"] = attrs;
        out.append(obj);
    }
    return out;
}

/// Serialize a single block at `row` as the same JSON shape produced by
/// `serializeSelection` — used when the cursor is `BlockSelected` (no
/// text-range selection), e.g. after Backspace on a paragraph adjacent
/// to a Table / HR / Image promotes the block to BlockSelected and the
/// user presses Ctrl+C to copy the block as a unit.
QJsonArray serializeBlockSelected(const LiveBlockModel &model, int row)
{
    QJsonArray out;
    if (row < 0 || row >= model.rowCount()) return out;
    const auto &rec = model.recordAt(row);
    QJsonObject obj;
    obj["kind"] = rec.kind;
    obj["text"] = rec.text;            // whole block content; no range subset
    const QJsonObject attrs = attrsToJson(rec.attrs);
    if (!attrs.isEmpty()) obj["attrs"] = attrs;
    out.append(obj);
    return out;
}

/// Plain-text fallback for the clipboard. Reuses
/// MarkoffDocument::reconstructFlatMarkdown so the markdown going to other
/// apps round-trips through Markoff (and external markdown viewers) the same
/// way the structured-paste path round-trips internally — list markers,
/// heading prefixes, blockquote `>`, code fences are preserved.
QString joinPlain(const QJsonArray &blocks)
{
    return QString::fromUtf8(
        Markoff::MarkoffDocument::reconstructFlatMarkdown(blocks));
}

/// Compute the flat byte offset of (blockIndex, qtPos) by walking
/// the document's iterateBlocks() list.  Uses the same model-text-UTF8
/// approach as LiveCursorState::deleteSelectionRange().
/// Returns UINT32_MAX if blockIndex is out of range.
uint32_t flatByteOffset(const LiveBlockModel &model,
                        Markoff::MarkoffDocument &doc,
                        int blockIndex, int qtPos)
{
    const auto allIds = doc.iterateBlocks();
    uint32_t cursor   = 0;
    for (int i = 0; i < static_cast<int>(allIds.size()); ++i) {
        const QByteArray rawText = doc.blockText(allIds[i]);
        if (i == blockIndex) {
            const QByteArray modelUtf8 = model.recordAt(blockIndex).text.toUtf8();
            const int clamped = qBound(0, qtPos, static_cast<int>(modelUtf8.size()));
            return cursor + static_cast<uint32_t>(
                coords::qtPosToByte(modelUtf8, clamped));
        }
        cursor += static_cast<uint32_t>(rawText.size());
    }
    return UINT32_MAX;
}

}  // namespace

// ---------------------------------------------------------------------------
// Public slots
// ---------------------------------------------------------------------------

void LiveClipboardController::copy()
{
    if (!m_selection || !m_model || !m_document) return;

    // Two cursor shapes can produce a copy:
    //   (a) cross-block text-range selection (anchor != active, extended)
    //   (b) BlockSelected{block} — the block is selected as a unit
    QJsonArray blocks;
    if (m_selection->hasSelection()) {
        blocks = serializeSelection(*m_selection, *m_model);
    } else if (m_selection->cursorKind() == QStringLiteral("BlockSelected")) {
        const int row = m_selection->focusedAnchorRow();
        blocks = serializeBlockSelected(*m_model, row);
    }
    if (blocks.isEmpty()) return;

    QJsonObject payload;
    payload["version"]         = 1;
    payload["sourceReplicaId"] = static_cast<qint64>(m_document->replicaId());
    payload["blocks"]          = blocks;

    auto *mime = new QMimeData();
    mime->setText(joinPlain(blocks));
    mime->setData(kBlocksMime,
                  QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QApplication::clipboard()->setMimeData(mime);
}

void LiveClipboardController::cut()
{
    if (isReadOnly()) return;  // read-only gate (spec §4.2)
    if (!m_selection || !m_model || !m_document) return;
    if (!m_selection->hasSelection()) return;

    // Capture cutSeq = d2EditSequence + 1, the sequence the deletion will use.
    const quint64 cutSeq = m_document->d2EditSequence() + 1;

    // Collect the BlockIds of all selected blocks (for the recent-cuts cache).
    std::vector<Markoff::BlockId> affected;
    const auto allIds  = m_document->iterateBlocks();
    const int rc       = m_model->rowCount();
    for (int i = 0; i < rc; ++i) {
        const QPoint r = m_selection->rangeForBlock(i);
        if (r.x() < 0) continue;
        if (i < static_cast<int>(allIds.size()))
            affected.push_back(allIds[i]);
    }

    // Build clipboard payload (same as copy, plus cutSequenceNumber).
    const QJsonArray blocks = serializeSelection(*m_selection, *m_model);
    QJsonObject payload;
    payload["version"]             = 1;
    payload["sourceReplicaId"]     = static_cast<qint64>(m_document->replicaId());
    payload["cutSequenceNumber"]   = static_cast<qint64>(cutSeq);
    payload["blocks"]              = blocks;

    auto *mime = new QMimeData();
    mime->setText(joinPlain(blocks));
    mime->setData(kBlocksMime,
                  QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QApplication::clipboard()->setMimeData(mime);

    // Delete the selection (applies the flat edit, clears selection).
    m_selection->deleteSelection();

    // Register the BlockIds in the recent-cuts cache.
    m_document->recordRecentCut(cutSeq, std::move(affected));
}

void LiveClipboardController::paste()
{
    if (isReadOnly()) return;  // read-only gate (spec §4.2)
    pasteFrom(static_cast<int>(QClipboard::Clipboard));
}

void LiveClipboardController::pastePrimary()
{
    if (isReadOnly()) return;  // read-only gate (spec §4.2)
    // PRIMARY selection only exists on platforms that report it
    // (X11/XWayland, wayland-primary-selection). On platforms without
    // it (offscreen QPA on a default setup, native Windows/macOS) the
    // clipboard's selection-mode mimeData() returns null and the
    // pasteFrom helper short-circuits cleanly.
    if (!QApplication::clipboard()->supportsSelection()) return;
    pasteFrom(static_cast<int>(QClipboard::Selection));
}

bool LiveClipboardController::resolveSelectionByteRange(
    uint32_t &startByte, uint32_t &endByte) const
{
    int firstRow, firstQtPos, lastRow;
    return resolveSelectionRange(startByte, endByte, firstRow, firstQtPos, lastRow);
}

bool LiveClipboardController::resolveSelectionRange(
    uint32_t &startByte, uint32_t &endByte,
    int &firstRow, int &firstQtPos, int &lastRow) const
{
    if (!m_selection || !m_document || !m_model) return false;

    const int xb = m_selection->activeBlock();
    const int xp = m_selection->activeQtPos();
    if (xb < 0) return false;

    // No active selection anchor (a bare, non-dragging cursor) means no
    // range to replace — but that's still a valid insertion point, not an
    // error. Collapse to (activeBlock, activeQtPos) so paste/structured-
    // paste callers insert at the caret instead of silently no-opping.
    // Queue #10: this was the root cause of Ctrl+V doing nothing on a
    // plain (unselected) cursor.
    const int ab = (m_selection->anchorBlock() < 0) ? xb : m_selection->anchorBlock();
    const int ap = (m_selection->anchorBlock() < 0) ? xp : m_selection->anchorQtPos();

    int fb, fo, lb, lo;
    if (ab < xb || (ab == xb && ap <= xp)) {
        fb = ab; fo = ap; lb = xb; lo = xp;
    } else {
        fb = xb; fo = xp; lb = ab; lo = ap;
    }
    firstRow = fb; firstQtPos = fo; lastRow = lb;

    startByte = flatByteOffset(*m_model, *m_document, fb, fo);
    endByte   = flatByteOffset(*m_model, *m_document, lb, lo);
    return startByte != UINT32_MAX && endByte != UINT32_MAX;
}

void LiveClipboardController::advanceCaretPastPaste(
    int firstRow, int lastRow, int firstQtPos, const QString &insertedText)
{
    if (!m_selection || !m_document || !m_model) return;
    if (firstRow != lastRow) return;               // cross-block: out of scope
    if (insertedText.contains(u'\n')) return;       // structural: out of scope
    if (firstRow < 0 || firstRow >= m_model->rowCount()) return;

    // Force the model to rebuild from the just-applied edit before reading
    // recordAt() / resolving the anchor, so establishFocus lands against
    // current state (same discipline LiveEditBinding::onContentsChange
    // uses before its own establishFocus-adjacent calls).
    m_document->flushPendingD2Changed();
    if (firstRow >= m_model->rowCount()) return;

    const int newQtPos = firstQtPos + insertedText.size();
    m_selection->establishFocus(m_model->recordAt(firstRow).blockAnchor, newQtPos);
}

void LiveClipboardController::pasteFrom(int clipboardMode)
{
    if (isReadOnly()) return;  // read-only gate (spec §4.2)
    if (!m_selection || !m_document || !m_model) return;
    const auto mode = static_cast<QClipboard::Mode>(clipboardMode);
    const QMimeData *mime = QApplication::clipboard()->mimeData(mode);
    if (!mime || (!mime->hasText() && !mime->hasFormat(kBlocksMime))) return;

    uint32_t startByte = 0, endByte = 0;
    int firstRow = -1, firstQtPos = 0, lastRow = -1;
    if (!resolveSelectionRange(startByte, endByte, firstRow, firstQtPos, lastRow))
        return;

    // Try the structured fast-path.
    if (mime->hasFormat(kBlocksMime)) {
        const QJsonDocument jdoc =
            QJsonDocument::fromJson(mime->data(kBlocksMime));
        if (jdoc.isObject() && jdoc.object().value("version").toInt() == 1) {
            const QJsonObject obj    = jdoc.object();
            const QJsonArray  bArr   = obj.value("blocks").toArray();
            const quint16 srcReplica =
                static_cast<quint16>(obj.value("sourceReplicaId").toInt(0));

            Markoff::PasteMeta meta;
            if (srcReplica == m_document->replicaId()
                && obj.contains("cutSequenceNumber")) {
                meta.reuseBlockIds = true;
                meta.cutSeq = static_cast<quint64>(
                    obj.value("cutSequenceNumber").toDouble());
            }
            m_document->applyStructuredPaste(startByte, endByte, bArr, meta);
            m_selection->clearSelection();
            return;
        }
    }

    // Flat text fallback.
    if (mime->hasText()) {
        const QString insertedText = mime->text();
        insertAtOrReplace(startByte, endByte, firstRow, lastRow, firstQtPos,
                          insertedText);
        advanceCaretPastPaste(firstRow, lastRow, firstQtPos, insertedText);
    }
}

void LiveClipboardController::insertAtOrReplace(
    uint32_t startByte, uint32_t endByte, int firstRow, int lastRow,
    int firstQtPos, const QString &insertedText)
{
    const QByteArray inserted = insertedText.toUtf8();

    // Collapsed cursor within a single block: insert at the block-local
    // offset directly via d2ApplyBufferEdit rather than applyFlatEdit's
    // global zero-separator byte range. That global range is genuinely
    // ambiguous at a block boundary — MarkoffDocument::applyFlatEdit
    // deliberately biases a boundary cursor-edit to the START of the
    // FOLLOWING block (see its "Cursor at start-of-block: bias to this
    // block" comment, load-bearing for other flows). A caret at the END
    // of a non-last block sits at that exact boundary, so it would be
    // misrouted into the start of the next block instead of staying in
    // the block the user is actually in. Queue #10: this surfaced once
    // paste-without-selection started actually inserting text (previously
    // masked — the no-op bug meant this path never ran with real content).
    if (firstRow == lastRow && startByte == endByte && firstRow >= 0) {
        const auto ids = m_document->iterateBlocks();
        if (firstRow < static_cast<int>(ids.size())) {
            const Markoff::BlockId blockId = ids[static_cast<std::size_t>(firstRow)];
            const QByteArray blockUtf8 = m_document->blockText(blockId);
            const int clamped = qBound(0, firstQtPos, blockUtf8.size());
            const int byteOff = coords::qtPosToByte(blockUtf8, clamped);
            Markoff::UndoLog::Transaction t(m_document->d2UndoLog());
            m_document->d2ApplyBufferEdit(blockId, static_cast<uint32_t>(byteOff),
                                          0, inserted, t);
            m_selection->clearSelection();
            return;
        }
    }

    m_document->applyFlatEdit(startByte, endByte, inserted,
                              Markoff::Origin::UserEdit);
    m_selection->clearSelection();
}

void LiveClipboardController::pasteText(const QString &text)
{
    if (isReadOnly()) return;  // read-only gate (spec §4.2)
    if (text.isEmpty()) return;
    if (!m_selection || !m_document || !m_model) return;

    uint32_t startByte = 0, endByte = 0;
    int firstRow = -1, firstQtPos = 0, lastRow = -1;
    if (!resolveSelectionRange(startByte, endByte, firstRow, firstQtPos, lastRow))
        return;

    insertAtOrReplace(startByte, endByte, firstRow, lastRow, firstQtPos, text);
    advanceCaretPastPaste(firstRow, lastRow, firstQtPos, text);
}

}  // namespace Markoff::Live
