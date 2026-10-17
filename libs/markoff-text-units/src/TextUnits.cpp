// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/TextUnits.h>

namespace Markoff::TextUnits {

namespace {
/// UTF-8 sequence length from a lead byte. Malformed lead bytes report 1
/// so the walk advances instead of stalling or over-running.
inline int sequenceLength(unsigned char c)
{
    if ((c & 0x80) == 0x00) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/// Return a valid UTF-8 scalar's byte length, or zero for malformed input.
int checkedSequenceLength(const QByteArray &text, qsizetype offset) noexcept
{
    const auto byte = [&text](qsizetype at) {
        return static_cast<unsigned char>(text[at]);
    };
    const qsizetype remaining = text.size() - offset;
    const unsigned char lead = byte(offset);
    if (lead <= 0x7F) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) {
        if (remaining < 2 || (byte(offset + 1) & 0xC0) != 0x80) return 0;
        return 2;
    }
    if (lead >= 0xE0 && lead <= 0xEF) {
        if (remaining < 3) return 0;
        const unsigned char second = byte(offset + 1);
        const unsigned char third = byte(offset + 2);
        if ((third & 0xC0) != 0x80) return 0;
        if (lead == 0xE0) return second >= 0xA0 && second <= 0xBF ? 3 : 0;
        if (lead == 0xED) return second >= 0x80 && second <= 0x9F ? 3 : 0;
        return (second & 0xC0) == 0x80 ? 3 : 0;
    }
    if (lead >= 0xF0 && lead <= 0xF4) {
        if (remaining < 4) return 0;
        const unsigned char second = byte(offset + 1);
        if ((byte(offset + 2) & 0xC0) != 0x80
            || (byte(offset + 3) & 0xC0) != 0x80) return 0;
        if (lead == 0xF0) return second >= 0x90 && second <= 0xBF ? 4 : 0;
        if (lead == 0xF4) return second >= 0x80 && second <= 0x8F ? 4 : 0;
        return (second & 0xC0) == 0x80 ? 4 : 0;
    }
    return 0;
}

bool validUtf8(const QByteArray &text) noexcept
{
    for (qsizetype offset = 0; offset < text.size();) {
        const int length = checkedSequenceLength(text, offset);
        if (length == 0) return false;
        offset += length;
    }
    return true;
}
}  // namespace

qsizetype byteToQtPos(const QByteArray &utf8, qsizetype byteOffset)
{
    const qsizetype size  = utf8.size();
    const qsizetype limit = qMin(byteOffset, size);
    qsizetype byteCursor  = 0;
    qsizetype qtCursor    = 0;
    while (byteCursor < limit) {
        const int seqLen = sequenceLength(static_cast<unsigned char>(utf8[byteCursor]));
        qtCursor   += (seqLen == 4) ? 2 : 1;  // non-BMP: surrogate pair
        byteCursor += seqLen;
    }
    return qtCursor;
}

qsizetype qtPosToByte(const QByteArray &utf8, qsizetype qtPos)
{
    const qsizetype size = utf8.size();
    qsizetype byteCursor = 0;
    qsizetype qtCursor   = 0;
    while (byteCursor < size && qtCursor < qtPos) {
        const int seqLen = sequenceLength(static_cast<unsigned char>(utf8[byteCursor]));
        qtCursor   += (seqLen == 4) ? 2 : 1;
        byteCursor += seqLen;
    }
    return byteCursor;
}

std::optional<qsizetype> checkedByteToQtPos(const QByteArray &utf8,
                                            qsizetype byteOffset)
{
    if (byteOffset < 0 || byteOffset > utf8.size() || !validUtf8(utf8))
        return std::nullopt;

    qsizetype byteCursor = 0;
    qsizetype qtCursor = 0;
    while (byteCursor < byteOffset) {
        const int length = checkedSequenceLength(utf8, byteCursor);
        if (byteCursor + length > byteOffset) return std::nullopt;
        qtCursor += length == 4 ? 2 : 1;
        byteCursor += length;
    }
    return qtCursor;
}

std::optional<qsizetype> checkedQtPosToByte(const QByteArray &utf8,
                                            qsizetype qtPos)
{
    if (qtPos < 0 || !validUtf8(utf8)) return std::nullopt;

    qsizetype byteCursor = 0;
    qsizetype qtCursor = 0;
    while (byteCursor < utf8.size()) {
        if (qtCursor == qtPos) return byteCursor;
        const int length = checkedSequenceLength(utf8, byteCursor);
        const qsizetype qtWidth = length == 4 ? 2 : 1;
        if (qtPos > qtCursor && qtPos < qtCursor + qtWidth)
            return std::nullopt;
        qtCursor += qtWidth;
        byteCursor += length;
    }
    return qtCursor == qtPos ? std::optional<qsizetype>(byteCursor) : std::nullopt;
}

}  // namespace Markoff::TextUnits
