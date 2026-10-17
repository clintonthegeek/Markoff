// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/FormatOps.h>

#include <markoff/core/Detail/FlatBlockResolve.h>
#include <markoff/core/MarkoffDocument.h>

#include <algorithm>
#include <vector>

namespace Markoff::FormatOps {
namespace {

quint32 qtPosToByteOffset(const QString& text, int qtOffset)
{
    const int limit = std::clamp(qtOffset, 0, static_cast<int>(text.size()));
    quint32 bytes = 0;
    for (int i = 0; i < limit;) {
        const ushort u = text.at(i).unicode();
        if (u < 0x80) { ++bytes; ++i; }
        else if (u < 0x800) { bytes += 2; ++i; }
        else if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 == limit) break;
            bytes += 4;
            i += 2;
        } else { bytes += 3; ++i; }
    }
    return bytes;
}

int documentLength(const MarkoffDocument* doc)
{
    return QString::fromUtf8(doc->widgetFlatView()).size();
}

enum class ToggleMode { surrounded, inside, wrap };

ToggleMode toggleMode(const QByteArray& text, int lo, int hi, int delimiterLength,
                      const QByteArray& delimiter)
{
    const bool surrounded = lo >= delimiterLength
        && hi + delimiterLength <= text.size()
        && text.mid(lo - delimiterLength, delimiterLength) == delimiter
        && text.mid(hi, delimiterLength) == delimiter;
    const bool inside = !surrounded && hi - lo >= 2 * delimiterLength
        && text.mid(lo, delimiterLength) == delimiter
        && text.mid(hi - delimiterLength, delimiterLength) == delimiter;
    return surrounded ? ToggleMode::surrounded
        : inside ? ToggleMode::inside : ToggleMode::wrap;
}

bool committed(const Markoff::VNext::ActionPublicationResult& result)
{
    return result.state == Markoff::VNext::ActionPublicationState::committed
        || result.state == Markoff::VNext::ActionPublicationState::committedHistoryPending;
}

} // namespace

std::optional<QtRange> wrapToggle(MarkoffDocument* doc, const QString& flatText,
                                  QtRange selection, const QByteArray& delimiter)
{
    if (!doc || delimiter.isEmpty()) return std::nullopt;
    if (selection.start > selection.end) std::swap(selection.start, selection.end);
    const int delimiterLength = delimiter.size();
    QList<BlockEdit> edits;
    if (selection.start == selection.end) {
        const quint32 byte = qtPosToByteOffset(flatText, selection.start);
        const auto hit = Detail::findBlockAtSepByte(doc, byte, true);
        if (!hit) return std::nullopt;
        edits.append({hit->blockId, hit->byteInBlock, 0, delimiter + delimiter});
        if (!committed(doc->applyBlockEdits(edits))) return std::nullopt;
        return QtRange{selection.start + delimiterLength,
                       selection.start + delimiterLength};
    }

    const quint32 byteStart = qtPosToByteOffset(flatText, selection.start);
    const quint32 byteEnd = qtPosToByteOffset(flatText, selection.end);
    const auto slices = Detail::sliceByBlocks(doc, byteStart, byteEnd);
    if (slices.isEmpty()) return std::nullopt;
    ToggleMode firstMode = ToggleMode::wrap;
    for (int i = slices.size() - 1; i >= 0; --i) {
        const auto& slice = slices[i];
        const QByteArray text = doc->blockText(slice.blockId);
        const auto mode = toggleMode(text, static_cast<int>(slice.byteLo),
            static_cast<int>(slice.byteHi), delimiterLength, delimiter);
        if (i == 0) firstMode = mode;
        if (mode == ToggleMode::surrounded) {
            edits.append({slice.blockId, slice.byteHi,
                static_cast<uint32_t>(delimiterLength), {}});
            edits.append({slice.blockId, slice.byteLo - delimiterLength,
                static_cast<uint32_t>(delimiterLength), {}});
        } else if (mode == ToggleMode::inside) {
            edits.append({slice.blockId, slice.byteHi - delimiterLength,
                static_cast<uint32_t>(delimiterLength), {}});
            edits.append({slice.blockId, slice.byteLo,
                static_cast<uint32_t>(delimiterLength), {}});
        } else {
            edits.append({slice.blockId, slice.byteHi, 0, delimiter});
            edits.append({slice.blockId, slice.byteLo, 0, delimiter});
        }
    }
    if (!committed(doc->applyBlockEdits(edits))) return std::nullopt;
    if (slices.size() > 1) {
        const int pos = std::clamp(selection.end, 0, documentLength(doc));
        return QtRange{pos, pos};
    }
    int start = selection.start, end = selection.end;
    if (firstMode == ToggleMode::surrounded) { start -= delimiterLength; end -= delimiterLength; }
    else if (firstMode == ToggleMode::inside) end -= 2 * delimiterLength;
    else { start += delimiterLength; end += delimiterLength; }
    return QtRange{start, end};
}

std::optional<QtRange> insertLink(MarkoffDocument* doc, const QString& flatText,
                                  QtRange selection)
{
    if (!doc) return std::nullopt;
    if (selection.start > selection.end) std::swap(selection.start, selection.end);
    QList<BlockEdit> edits;
    if (selection.start == selection.end) {
        const auto hit = Detail::findBlockAtSepByte(doc,
            qtPosToByteOffset(flatText, selection.start), true);
        if (!hit) return std::nullopt;
        edits.append({hit->blockId, hit->byteInBlock, 0, QByteArrayLiteral("[](url)")});
        if (!committed(doc->applyBlockEdits(edits))) return std::nullopt;
        return QtRange{selection.start + 1, selection.start + 1};
    }
    const auto slices = Detail::sliceByBlocks(doc,
        qtPosToByteOffset(flatText, selection.start),
        qtPosToByteOffset(flatText, selection.end));
    if (slices.isEmpty()) return std::nullopt;
    for (int i = slices.size() - 1; i >= 0; --i) {
        const auto& slice = slices[i];
        edits.append({slice.blockId, slice.byteHi, 0, QByteArrayLiteral("](url)")});
        edits.append({slice.blockId, slice.byteLo, 0, QByteArrayLiteral("[")});
    }
    if (!committed(doc->applyBlockEdits(edits))) return std::nullopt;
    if (slices.size() > 1) {
        const int pos = std::clamp(selection.end, 0, documentLength(doc));
        return QtRange{pos, pos};
    }
    return QtRange{selection.end + 3, selection.end + 6};
}

std::optional<QtRange> setHeadingLevel(MarkoffDocument* doc,
                                       const QString& flatText,
                                       int caretQtPos, int level)
{
    if (!doc || level < 0 || level > 6) return std::nullopt;
    const int lineStartQt = caretQtPos <= 0 ? 0
        : static_cast<int>(flatText.lastIndexOf(QLatin1Char('\n'), caretQtPos - 1)) + 1;
    const auto hit = Detail::findBlockAtSepByte(doc,
        qtPosToByteOffset(flatText, lineStartQt), false);
    if (!hit) return std::nullopt;
    const QByteArray content = doc->blockText(hit->blockId);
    int prefixBytes = 0;
    while (prefixBytes < 6 && hit->byteInBlock + prefixBytes < quint32(content.size())
           && content[hit->byteInBlock + prefixBytes] == '#') ++prefixBytes;
    if (prefixBytes && hit->byteInBlock + prefixBytes < quint32(content.size())
        && content[hit->byteInBlock + prefixBytes] == ' ') ++prefixBytes;
    else if (prefixBytes && hit->byteInBlock + prefixBytes != quint32(content.size()))
        prefixBytes = 0;
    const QByteArray newPrefix = level == 0 ? QByteArray() : QByteArray(level, '#') + ' ';
    if (content.mid(hit->byteInBlock, prefixBytes) == newPrefix
        && int(newPrefix.size()) == prefixBytes) return std::nullopt;
    if (!committed(doc->applyBlockEdits({BlockEdit{hit->blockId, hit->byteInBlock,
            static_cast<uint32_t>(prefixBytes), newPrefix}}))) return std::nullopt;
    const int pos = std::clamp(caretQtPos + int(newPrefix.size()) - prefixBytes,
                               lineStartQt, documentLength(doc));
    return QtRange{pos, pos};
}

std::optional<ByteRange> wrapToggleInBlock(MarkoffDocument* doc, BlockId block,
                                           ByteRange selection, const QByteArray& delimiter)
{
    if (!doc || block.isNull() || delimiter.isEmpty()) return std::nullopt;
    if (selection.start > selection.end) std::swap(selection.start, selection.end);
    const QByteArray text = doc->blockText(block);
    const int lo = std::clamp(selection.start, 0, int(text.size()));
    const int hi = std::clamp(selection.end, lo, int(text.size()));
    const int n = delimiter.size();
    const auto mode = toggleMode(text, lo, hi, n, delimiter);
    if (lo == hi) {
        if (!committed(doc->applyBlockEdit({block, quint32(lo), 0, delimiter + delimiter}))) return std::nullopt;
        return ByteRange{lo + n, lo + n};
    }
    if (mode == ToggleMode::surrounded) {
        QList<BlockEdit> edits{{block, quint32(hi), quint32(n), {}},
                               {block, quint32(lo - n), quint32(n), {}}};
        if (!committed(doc->applyBlockEdits(edits))) return std::nullopt;
        return ByteRange{lo - n, hi - n};
    }
    if (mode == ToggleMode::inside) {
        QList<BlockEdit> edits{{block, quint32(hi - n), quint32(n), {}},
                               {block, quint32(lo), quint32(n), {}}};
        if (!committed(doc->applyBlockEdits(edits))) return std::nullopt;
        return ByteRange{lo, hi - 2*n};
    }
    QList<BlockEdit> edits{{block, quint32(hi), 0, delimiter},
                           {block, quint32(lo), 0, delimiter}};
    if (!committed(doc->applyBlockEdits(edits))) return std::nullopt;
    return ByteRange{lo + n, hi + n};
}

std::optional<ByteRange> insertLinkInBlock(MarkoffDocument* doc, BlockId block,
                                           ByteRange selection)
{
    if (!doc || block.isNull()) return std::nullopt;
    if (selection.start > selection.end) std::swap(selection.start, selection.end);
    const int lo = std::clamp(selection.start, 0, int(doc->blockText(block).size()));
    const int hi = std::clamp(selection.end, lo, int(doc->blockText(block).size()));
    if (lo == hi) {
        if (!committed(doc->applyBlockEdit({block, quint32(lo), 0, QByteArrayLiteral("[](url)")})))
            return std::nullopt;
        return ByteRange{lo + 1, lo + 1};
    }
    if (!committed(doc->applyBlockEdits({BlockEdit{block, quint32(hi), 0, QByteArrayLiteral("](url)")},
                                         BlockEdit{block, quint32(lo), 0, QByteArrayLiteral("[")}})))
        return std::nullopt;
    return ByteRange{hi + 3, hi + 6};
}

std::optional<int> setHeadingLevelInBlock(MarkoffDocument* doc, BlockId block,
                                          int caretByteOffset, int level)
{
    if (!doc || block.isNull() || level < 0 || level > 6) return std::nullopt;
    const QByteArray text = doc->blockText(block);
    const int caret = std::clamp(caretByteOffset, 0, int(text.size()));
    int lo = text.lastIndexOf('\n', caret - 1) + 1;
    int prefix = 0;
    while (prefix < 6 && lo + prefix < text.size() && text[lo + prefix] == '#') ++prefix;
    if (prefix && lo + prefix < text.size() && text[lo + prefix] == ' ') ++prefix;
    else if (prefix && lo + prefix != text.size()) prefix = 0;
    const QByteArray replacement = level ? QByteArray(level, '#') + ' ' : QByteArray();
    if (text.mid(lo, prefix) == replacement && prefix == replacement.size()) return std::nullopt;
    if (!committed(doc->applyBlockEdit({block, quint32(lo), quint32(prefix), replacement})))
        return std::nullopt;
    return caret + int(replacement.size()) - prefix;
}

std::optional<QList<ByteRange>> wrapToggleAcrossBlocks(
    MarkoffDocument* doc, const QList<BlockByteRange>& ranges, const QByteArray& delimiter)
{
    if (!doc || ranges.isEmpty() || delimiter.isEmpty()) return std::nullopt;
    QList<BlockEdit> edits;
    QList<ByteRange> results;
    const int n = delimiter.size();
    for (const auto& item : ranges) {
        const QByteArray text = doc->blockText(item.block);
        const int lo = std::clamp(item.range.start, 0, int(text.size()));
        const int hi = std::clamp(item.range.end, lo, int(text.size()));
        if (lo == hi) return std::nullopt;
        const auto mode = toggleMode(text, lo, hi, n, delimiter);
        if (mode == ToggleMode::surrounded) {
            edits.append({item.block, quint32(hi), quint32(n), {}});
            edits.append({item.block, quint32(lo - n), quint32(n), {}});
            results.append({lo - n, hi - n});
        } else if (mode == ToggleMode::inside) {
            edits.append({item.block, quint32(hi - n), quint32(n), {}});
            edits.append({item.block, quint32(lo), quint32(n), {}});
            results.append({lo, hi - 2*n});
        } else {
            edits.append({item.block, quint32(hi), 0, delimiter});
            edits.append({item.block, quint32(lo), 0, delimiter});
            results.append({lo + n, hi + n});
        }
    }
    if (!committed(doc->applyBlockEdits(edits))) return std::nullopt;
    return results;
}

std::optional<QList<ByteRange>> insertLinkAcrossBlocks(
    MarkoffDocument* doc, const QList<BlockByteRange>& ranges)
{
    if (!doc || ranges.isEmpty()) return std::nullopt;
    QList<BlockEdit> edits;
    QList<ByteRange> results;
    for (const auto& item : ranges) {
        const int size = int(doc->blockText(item.block).size());
        const int lo = std::clamp(item.range.start, 0, size);
        const int hi = std::clamp(item.range.end, lo, size);
        if (lo == hi) return std::nullopt;
        edits.append({item.block, quint32(hi), 0, QByteArrayLiteral("](url)")});
        edits.append({item.block, quint32(lo), 0, QByteArrayLiteral("[")});
        results.append({hi + 3, hi + 6});
    }
    if (!committed(doc->applyBlockEdits(edits))) return std::nullopt;
    return results;
}

} // namespace Markoff::FormatOps
