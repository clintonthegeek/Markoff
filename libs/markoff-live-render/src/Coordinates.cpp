// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/Coordinates.h>

namespace Markoff::LiveRender::Coordinates {

qsizetype byteToQtPos(const QByteArray &utf8, qsizetype byteOffset)
{
    const qsizetype size  = utf8.size();
    const qsizetype limit = qMin(byteOffset, size);
    qsizetype byteCursor  = 0;
    qsizetype qtCursor    = 0;
    while (byteCursor < limit) {
        const unsigned char c = static_cast<unsigned char>(utf8[byteCursor]);
        int seqLen;
        if      ((c & 0x80) == 0x00) seqLen = 1;
        else if ((c & 0xE0) == 0xC0) seqLen = 2;
        else if ((c & 0xF0) == 0xE0) seqLen = 3;
        else if ((c & 0xF8) == 0xF0) seqLen = 4;
        else                          seqLen = 1;  // malformed: skip
        qtCursor   += (seqLen == 4) ? 2 : 1;
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
        const unsigned char c = static_cast<unsigned char>(utf8[byteCursor]);
        int seqLen;
        if      ((c & 0x80) == 0x00) seqLen = 1;
        else if ((c & 0xE0) == 0xC0) seqLen = 2;
        else if ((c & 0xF0) == 0xE0) seqLen = 3;
        else if ((c & 0xF8) == 0xF0) seqLen = 4;
        else                          seqLen = 1;
        qtCursor   += (seqLen == 4) ? 2 : 1;
        byteCursor += seqLen;
    }
    return byteCursor;
}

}  // namespace Markoff::LiveRender::Coordinates
