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

}  // namespace Markoff::TextUnits
