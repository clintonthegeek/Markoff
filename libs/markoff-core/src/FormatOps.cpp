// SPDX-License-Identifier: GPL-3.0-or-later
//
// Widget-free markdown format operations, hoisted verbatim from
// markoff-source's Editor.cpp (MarkdownView contract v2 §5,
// docs/specs/2026-06-09-markdownview-contract-v2-design.md). The only
// substitutions are mechanical: toPlainText() → the flatText parameter,
// QTextCursor anchor/position → the QtRange parameter, and the final
// "re-apply caret/selection to the widget" tail → the returned QtRange
// (std::nullopt where the donor early-returned without touching the
// cursor).
//
// All operate by resolving the cursor's qt-position to a markoff block and
// applying the edit via d2ApplyBufferEdit, bypassing the lossy sep→no-sep
// translation in SourceTextDocumentBinding (which sends range edits through
// MarkoffDocument::applyFlatEdit). The translation drops boundary direction
// and would route boundary-touching edits through applyFlatEdit's
// cross-block branch, merging blocks. See setHeadingLevel below and the
// 2026-05-21 source-view dogfood fix for the root-cause writeup.

#include <markoff/core/FormatOps.h>

#include <markoff/core/Detail/FlatBlockResolve.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Origin.h>
#include <markoff/core/SourceTextDocumentBinding.h>
#include <markoff/core/UndoLog.h>

#include <utility>

namespace Markoff::FormatOps {

namespace {

// Post-edit document length in UTF-16 units. Widget-free equivalent of the
// donor's `te->document()->characterCount() - 1`: after
// flushPendingD2Changed() the binding's QTextDocument mirrors
// widgetFlatView() exactly, so the lengths coincide.
int postEditDocLen(const Markoff::MarkoffDocument *doc) {
    return QString::fromUtf8(doc->widgetFlatView()).size();
}

} // anon

// Toggle `delim` wrap around the selection (or insert an empty pair at the
// cursor), mediated through the block-aware d2 API.
//
// Detection (per slice, matching the legacy QTextCursor impl):
//   * surroundedOutside — bytes outside the selection in the same block are
//     already `delim`. Unwrap by removing both.
//   * insideMarkers     — selection itself starts and ends with `delim`.
//     Unwrap by stripping the inner markers.
//   * otherwise         — wrap by inserting `delim` at both ends.
//
// Multi-block selections: each block's slice is handled independently
// (matching LiveFormatController::wrapPerBlock).
std::optional<QtRange> wrapToggle(MarkoffDocument *doc,
                                  const QString &flatText,
                                  QtRange sel,
                                  const QByteArray &delim) {
    if (!doc) return std::nullopt;

    // Normalize so start <= end (QTextCursor anchors may run backwards).
    if (sel.start > sel.end) std::swap(sel.start, sel.end);

    const QString &docText = flatText;
    const int delimLen = delim.size();  // ASCII delims: bytes == UTF-16 units

    // --- No selection: insert delim+delim, park cursor between. -----------
    if (sel.start == sel.end) {
        const int qtPos = sel.start;
        const quint32 sepByte =
            Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtPos);
        auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepByte, /*biasForward=*/true);
        if (!hit) {
            // Empty document: applyFlatEdit auto-creates a paragraph block.
            doc->applyFlatEdit(0, 0, delim + delim, Markoff::Origin::UserEdit);
        } else {
            Markoff::UndoLog::Transaction t(doc->d2UndoLog());
            doc->d2ApplyBufferEdit(hit->blockId, hit->byteInBlock, 0,
                                   delim + delim, t);
        }
        doc->flushPendingD2Changed();
        return QtRange{qtPos + delimLen, qtPos + delimLen};
    }

    // --- Selection: per-block toggle. ------------------------------------
    const int qtStart = sel.start;
    const int qtEnd   = sel.end;
    const quint32 sepStart =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtStart);
    const quint32 sepEnd =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtEnd);
    const QList<Markoff::Detail::BlockSlice> slices = Markoff::Detail::sliceByBlocks(doc, sepStart, sepEnd);
    if (slices.isEmpty()) return std::nullopt;

    enum class Mode { SurroundedOutside, InsideMarkers, Wrap };

    // Determine per-slice mode and compute the post-edit selection
    // restoration for the SINGLE-slice common case. For multi-slice we
    // collapse the cursor to the end after all edits.
    Mode firstMode = Mode::Wrap;

    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());

        // Process slices in reverse so later block edits don't shift earlier
        // ones' bytes (each slice is intra-block, so this matters only in
        // case of multi-block selection).
        for (int n = slices.size() - 1; n >= 0; --n) {
            const Markoff::Detail::BlockSlice &s = slices[n];
            const QByteArray content = doc->blockText(s.blockId);
            const int blockSize = content.size();
            const int loInt = static_cast<int>(s.byteLo);
            const int hiInt = static_cast<int>(s.byteHi);

            const bool surroundedOutside =
                loInt >= delimLen
                && hiInt + delimLen <= blockSize
                && content.mid(loInt - delimLen, delimLen) == delim
                && content.mid(hiInt, delimLen) == delim;
            const bool insideMarkers =
                !surroundedOutside
                && (hiInt - loInt) >= 2 * delimLen
                && content.mid(loInt, delimLen) == delim
                && content.mid(hiInt - delimLen, delimLen) == delim;

            Mode mode = Mode::Wrap;
            if (surroundedOutside)    mode = Mode::SurroundedOutside;
            else if (insideMarkers)   mode = Mode::InsideMarkers;
            if (n == 0) firstMode = mode;

            switch (mode) {
            case Mode::SurroundedOutside:
                // Remove trailing delim (higher byte) first, then leading.
                doc->d2ApplyBufferEdit(s.blockId, s.byteHi,
                                       static_cast<quint32>(delimLen),
                                       QByteArray(), t);
                doc->d2ApplyBufferEdit(s.blockId, s.byteLo - delimLen,
                                       static_cast<quint32>(delimLen),
                                       QByteArray(), t);
                break;
            case Mode::InsideMarkers:
                doc->d2ApplyBufferEdit(s.blockId, s.byteHi - delimLen,
                                       static_cast<quint32>(delimLen),
                                       QByteArray(), t);
                doc->d2ApplyBufferEdit(s.blockId, s.byteLo,
                                       static_cast<quint32>(delimLen),
                                       QByteArray(), t);
                break;
            case Mode::Wrap:
                // Insert trailing delim first (higher byte), then leading.
                doc->d2ApplyBufferEdit(s.blockId, s.byteHi, 0, delim, t);
                doc->d2ApplyBufferEdit(s.blockId, s.byteLo, 0, delim, t);
                break;
            }
        }
    }

    doc->flushPendingD2Changed();

    // Restore selection. For a single-slice (intra-block) edit we know the
    // exact range that survived. For multi-slice, collapse to the trailing
    // edge — multi-block format toggles are an edge case and per-slice
    // modes may differ, making exact restoration ambiguous.
    if (slices.size() == 1) {
        int newStart = qtStart;
        int newEnd   = qtEnd;
        switch (firstMode) {
        case Mode::SurroundedOutside:
            newStart -= delimLen;
            newEnd   -= delimLen;
            break;
        case Mode::InsideMarkers:
            newEnd -= 2 * delimLen;
            break;
        case Mode::Wrap:
            newStart += delimLen;
            newEnd   += delimLen;
            break;
        }
        return QtRange{newStart, newEnd};
    }
    // Multi-slice: park cursor near the trailing end without a
    // restored selection.
    const int docLen = postEditDocLen(doc);
    int newPos = qtEnd;
    if (newPos > docLen) newPos = docLen;
    if (newPos < 0)      newPos = 0;
    return QtRange{newPos, newPos};
}

std::optional<QtRange> insertLink(MarkoffDocument *doc,
                                  const QString &flatText,
                                  QtRange sel) {
    if (!doc) return std::nullopt;

    // Normalize so start <= end (QTextCursor anchors may run backwards).
    if (sel.start > sel.end) std::swap(sel.start, sel.end);

    const QString &docText = flatText;

    // --- No selection: insert `[](url)` template, park cursor between `[]`. -
    if (sel.start == sel.end) {
        const int qtPos = sel.start;
        const quint32 sepByte =
            Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtPos);
        auto hit = Markoff::Detail::findBlockAtSepByte(doc, sepByte, /*biasForward=*/true);
        const QByteArray payload = QByteArrayLiteral("[](url)");
        if (!hit) {
            doc->applyFlatEdit(0, 0, payload, Markoff::Origin::UserEdit);
        } else {
            Markoff::UndoLog::Transaction t(doc->d2UndoLog());
            doc->d2ApplyBufferEdit(hit->blockId, hit->byteInBlock, 0,
                                   payload, t);
        }
        doc->flushPendingD2Changed();
        return QtRange{qtPos + 1, qtPos + 1};
    }

    // --- Selection: wrap `[selection](url)` per block slice. ---------------
    const int qtStart = sel.start;
    const int qtEnd   = sel.end;
    const quint32 sepStart =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtStart);
    const quint32 sepEnd =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(docText, qtEnd);
    const QList<Markoff::Detail::BlockSlice> slices = Markoff::Detail::sliceByBlocks(doc, sepStart, sepEnd);
    if (slices.isEmpty()) return std::nullopt;

    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        // Reverse order: later block edits don't shift earlier ones' bytes.
        // Within a slice: insert trailing `](url)` first (higher byte), then
        // leading `[` — same higher-then-lower pattern as wrapToggle.
        for (int n = slices.size() - 1; n >= 0; --n) {
            const Markoff::Detail::BlockSlice &s = slices[n];
            doc->d2ApplyBufferEdit(s.blockId, s.byteHi, 0,
                                   QByteArrayLiteral("](url)"), t);
            doc->d2ApplyBufferEdit(s.blockId, s.byteLo, 0,
                                   QByteArrayLiteral("["), t);
        }
    }

    doc->flushPendingD2Changed();

    if (slices.size() == 1) {
        // Single-block selection: park selection over `url` for easy replace.
        // Layout: `[selection](url)` starting at qtStart; url at qtEnd+3..qtEnd+6.
        return QtRange{qtEnd + 3, qtEnd + 6};
    }
    // Multi-slice: collapse near the trailing end (matches wrapToggle).
    const int docLen = postEditDocLen(doc);
    int newPos = qtEnd;
    if (newPos > docLen) newPos = docLen;
    if (newPos < 0)      newPos = 0;
    return QtRange{newPos, newPos};
}

std::optional<QtRange> setHeadingLevel(MarkoffDocument *doc,
                                       const QString &flatText,
                                       int caretQtPos, int level) {
    if (!doc) return std::nullopt;
    if (level < 0 || level > 6) return std::nullopt;

    // Editing the heading prefix via QTextCursor on the inner QPlainTextEdit
    // and letting it route through SourceTextDocumentBinding::onQtContentsChange
    // would issue a range edit whose start sits exactly at a markoff block
    // boundary whenever the heading is the first line of a non-first block.
    // The sep-view→no-sep-view translation loses the boundary direction, and
    // applyFlatEdit's range-edit boundary bias ("<= blkEnd") then routes the
    // edit through the cross-block-edit branch, removing the heading block
    // and merging its tail into the previous block. Bypass the lossy
    // coordinate translation by mutating the target block's buffer directly,
    // the same way LiveFormatController::setHeadingLevel does.

    const int origQtPos = caretQtPos;
    // Line start in the flat view — the widget-free equivalent of the
    // donor's `textCursor().block().position()` (QPlainTextEdit blocks are
    // '\n'-delimited paragraphs of toPlainText()).
    const int lineStartQt = (origQtPos <= 0)
        ? 0
        : static_cast<int>(flatText.lastIndexOf(QLatin1Char('\n'),
                                                origQtPos - 1)) + 1;
    const QString &text = flatText;
    const quint32 lineStartSep =
        Markoff::SourceTextDocumentBinding::qtPosToByteOffset(text, lineStartQt);

    // Resolve the line-start sep-byte to its model block via the shared helper,
    // which tracks the WP-unification single-'\n' separator (SEP_LEN == 1). The
    // bespoke walk this replaced hardcoded SEP_LEN == 2 ("\n\n"); once
    // widgetFlatView() went single-'\n', that walk over-advanced its cursor by
    // one byte per preceding boundary, underflowing byteInBlock (quint32 wrap)
    // for any heading below the first block — so the prefix landed at
    // end-of-block ("Hello## ") instead of the line start. biasForward == false
    // keeps an empty line's own (zero-length) block as the target rather than
    // skipping forward to the next block.
    const auto hit = Markoff::Detail::findBlockAtSepByte(
        doc, lineStartSep, /*biasForward=*/false);
    if (!hit) return std::nullopt;
    const Markoff::BlockId targetBlock = hit->blockId;
    const quint32 byteInBlock = hit->byteInBlock;

    const QByteArray content = doc->blockText(targetBlock);
    const int blockSize = content.size();
    int oldBytes = 0;
    while (oldBytes < 6
           && static_cast<int>(byteInBlock) + oldBytes < blockSize
           && content[static_cast<int>(byteInBlock) + oldBytes] == '#') {
        ++oldBytes;
    }
    if (oldBytes > 0
        && static_cast<int>(byteInBlock) + oldBytes < blockSize
        && content[static_cast<int>(byteInBlock) + oldBytes] == ' ') {
        ++oldBytes;
    } else if (oldBytes > 0
               && static_cast<int>(byteInBlock) + oldBytes != blockSize) {
        // "##" with non-space follower — not an ATX prefix; leave alone.
        oldBytes = 0;
    }

    const QByteArray newPrefix = (level == 0)
        ? QByteArray()
        : QByteArray(level, '#') + ' ';

    if (newPrefix.size() == oldBytes
        && content.mid(static_cast<int>(byteInBlock), oldBytes) == newPrefix)
        return std::nullopt;

    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2ApplyBufferEdit(targetBlock, byteInBlock,
                               static_cast<quint32>(oldBytes),
                               newPrefix, t);
    }

    // Flush the debounced d2DocumentChanged so the binding syncs the
    // QTextDocument synchronously. Users expect immediate visual feedback;
    // tests expect toPlainText() to reflect the change without spinning the
    // event loop.
    doc->flushPendingD2Changed();

    // Restore cursor: prefixes are ASCII so byte-count == UTF-16-unit count.
    const int delta = static_cast<int>(newPrefix.size()) - oldBytes;
    int newPos = origQtPos + delta;
    if (newPos < lineStartQt) newPos = lineStartQt;
    const int docLen = postEditDocLen(doc);
    if (newPos > docLen) newPos = docLen;
    return QtRange{newPos, newPos};
}

// ---------------------------------------------------------------------
// Per-block byte-offset overloads (canvas production plan P4.3).
//
// Same three algorithms as above, minus the flat-position resolution
// step (qtPosToByteOffset / findBlockAtSepByte / sliceByBlocks) — the
// caller already names the block and the local byte range, so there is
// only ever one slice, and no cross-block loop is needed here at all.
// ---------------------------------------------------------------------

std::optional<ByteRange> wrapToggleInBlock(MarkoffDocument *doc,
                                           BlockId block,
                                           ByteRange sel,
                                           const QByteArray &delim) {
    if (!doc || block.isNull()) return std::nullopt;

    if (sel.start > sel.end) std::swap(sel.start, sel.end);
    const int delimLen = delim.size();

    // --- No selection: insert delim+delim, park caret between. ------------
    if (sel.start == sel.end) {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2ApplyBufferEdit(block, static_cast<quint32>(sel.start), 0,
                               delim + delim, t);
        doc->flushPendingD2Changed();
        const int pos = sel.start + delimLen;
        return ByteRange{pos, pos};
    }

    // --- Selection: single-slice toggle (same detection as wrapToggle's
    // per-slice branch). -----------------------------------------------
    const QByteArray content = doc->blockText(block);
    const int blockSize = content.size();
    const int loInt = sel.start;
    const int hiInt = sel.end;

    const bool surroundedOutside =
        loInt >= delimLen
        && hiInt + delimLen <= blockSize
        && content.mid(loInt - delimLen, delimLen) == delim
        && content.mid(hiInt, delimLen) == delim;
    const bool insideMarkers =
        !surroundedOutside
        && (hiInt - loInt) >= 2 * delimLen
        && content.mid(loInt, delimLen) == delim
        && content.mid(hiInt - delimLen, delimLen) == delim;

    enum class Mode { SurroundedOutside, InsideMarkers, Wrap };
    Mode mode = Mode::Wrap;
    if (surroundedOutside)  mode = Mode::SurroundedOutside;
    else if (insideMarkers) mode = Mode::InsideMarkers;

    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        switch (mode) {
        case Mode::SurroundedOutside:
            // Remove trailing delim (higher byte) first, then leading.
            doc->d2ApplyBufferEdit(block, static_cast<quint32>(hiInt),
                                   static_cast<quint32>(delimLen), QByteArray(), t);
            doc->d2ApplyBufferEdit(block, static_cast<quint32>(loInt - delimLen),
                                   static_cast<quint32>(delimLen), QByteArray(), t);
            break;
        case Mode::InsideMarkers:
            doc->d2ApplyBufferEdit(block, static_cast<quint32>(hiInt - delimLen),
                                   static_cast<quint32>(delimLen), QByteArray(), t);
            doc->d2ApplyBufferEdit(block, static_cast<quint32>(loInt),
                                   static_cast<quint32>(delimLen), QByteArray(), t);
            break;
        case Mode::Wrap:
            // Insert trailing delim first (higher byte), then leading.
            doc->d2ApplyBufferEdit(block, static_cast<quint32>(hiInt), 0, delim, t);
            doc->d2ApplyBufferEdit(block, static_cast<quint32>(loInt), 0, delim, t);
            break;
        }
    }
    doc->flushPendingD2Changed();

    int newStart = loInt;
    int newEnd   = hiInt;
    switch (mode) {
    case Mode::SurroundedOutside:
        newStart -= delimLen;
        newEnd   -= delimLen;
        break;
    case Mode::InsideMarkers:
        newEnd -= 2 * delimLen;
        break;
    case Mode::Wrap:
        newStart += delimLen;
        newEnd   += delimLen;
        break;
    }
    return ByteRange{newStart, newEnd};
}

std::optional<ByteRange> insertLinkInBlock(MarkoffDocument *doc,
                                           BlockId block,
                                           ByteRange sel) {
    if (!doc || block.isNull()) return std::nullopt;

    if (sel.start > sel.end) std::swap(sel.start, sel.end);

    // --- No selection: insert `[](url)` template, park caret between `[]`. -
    if (sel.start == sel.end) {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2ApplyBufferEdit(block, static_cast<quint32>(sel.start), 0,
                               QByteArrayLiteral("[](url)"), t);
        doc->flushPendingD2Changed();
        const int pos = sel.start + 1;
        return ByteRange{pos, pos};
    }

    // --- Selection: wrap `[selection](url)`. -------------------------------
    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        // Insert trailing `](url)` first (higher byte), then leading `[`.
        doc->d2ApplyBufferEdit(block, static_cast<quint32>(sel.end), 0,
                               QByteArrayLiteral("](url)"), t);
        doc->d2ApplyBufferEdit(block, static_cast<quint32>(sel.start), 0,
                               QByteArrayLiteral("["), t);
    }
    doc->flushPendingD2Changed();

    // Park selection over `url` for easy replace. Layout:
    // `[selection](url)` starting at sel.start; url at sel.end+3..sel.end+6.
    return ByteRange{sel.end + 3, sel.end + 6};
}

std::optional<int> setHeadingLevelInBlock(MarkoffDocument *doc,
                                          BlockId block,
                                          int caretByteOffset, int level) {
    if (!doc || block.isNull()) return std::nullopt;
    if (level < 0 || level > 6) return std::nullopt;

    // No flat-text line-start search needed here: per B1 (block buffer
    // convention, markoff-core/CLAUDE.md), a block's buffer never carries
    // an internal '\n', so the block start IS the line start — exactly
    // what the flat version's backward-\n search resolves to, without
    // needing to resolve it.
    const QByteArray content = doc->blockText(block);
    const int blockSize = content.size();

    int oldBytes = 0;
    while (oldBytes < 6 && oldBytes < blockSize && content[oldBytes] == '#') {
        ++oldBytes;
    }
    if (oldBytes > 0 && oldBytes < blockSize && content[oldBytes] == ' ') {
        ++oldBytes;
    } else if (oldBytes > 0 && oldBytes != blockSize) {
        // "##" with non-space follower — not an ATX prefix; leave alone.
        oldBytes = 0;
    }

    const QByteArray newPrefix = (level == 0)
        ? QByteArray()
        : QByteArray(level, '#') + ' ';

    if (newPrefix.size() == oldBytes && content.left(oldBytes) == newPrefix)
        return std::nullopt;

    {
        Markoff::UndoLog::Transaction t(doc->d2UndoLog());
        doc->d2ApplyBufferEdit(block, 0, static_cast<quint32>(oldBytes),
                               newPrefix, t);
    }
    doc->flushPendingD2Changed();

    // Restore caret: prefixes are ASCII so byte-count is exact.
    const int delta = static_cast<int>(newPrefix.size()) - oldBytes;
    int newPos = caretByteOffset + delta;
    if (newPos < 0) newPos = 0;
    const int newBlockSize = static_cast<int>(doc->blockText(block).size());
    if (newPos > newBlockSize) newPos = newBlockSize;
    return newPos;
}

}  // namespace Markoff::FormatOps
