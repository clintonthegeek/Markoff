// SPDX-License-Identifier: GPL-3.0-or-later
#include "TopLevelBlockScanner.h"

namespace Markoff::Detail {

namespace {

bool isBlankLine(const QByteArray &body, qsizetype lineStart, qsizetype lineEnd)
{
    for (qsizetype i = lineStart; i < lineEnd; ++i) {
        const char c = body[i];
        if (c != ' ' && c != '\t' && c != '\r') return false;
    }
    return true;
}

/// Returns true if the line at [lineStart, lineEnd) opens or closes
/// a fenced code block — i.e. starts with three or more backticks or
/// tildes (after up to three leading spaces).
bool isFenceOpenOrClose(const QByteArray &body, qsizetype lineStart, qsizetype lineEnd)
{
    qsizetype i = lineStart;
    int leadingSpaces = 0;
    while (i < lineEnd && body[i] == ' ' && leadingSpaces < 3) {
        ++i; ++leadingSpaces;
    }
    if (i >= lineEnd) return false;
    const char c = body[i];
    if (c != '`' && c != '~') return false;
    int run = 0;
    while (i < lineEnd && body[i] == c) { ++i; ++run; }
    return run >= 3;
}

/// Scan from `lineStart` to the start of the next line. Returns the
/// line-end (exclusive of the '\n'), and advances `cursor` to one
/// past the '\n' (or to body.size() if EOF).
qsizetype findLineEnd(const QByteArray &body, qsizetype lineStart, qsizetype &cursor)
{
    qsizetype i = lineStart;
    while (i < body.size() && body[i] != '\n') ++i;
    cursor = (i < body.size()) ? (i + 1) : body.size();
    return i;
}

}  // namespace

QList<BlockByteRange> scanTopLevelBlockRanges(const QByteArray &body)
{
    QList<BlockByteRange> result;
    qsizetype cursor = 0;

    while (cursor < body.size()) {
        // Skip blank lines.
        qsizetype lineStart = cursor;
        qsizetype lineEnd   = findLineEnd(body, lineStart, cursor);
        while (isBlankLine(body, lineStart, lineEnd) && cursor < body.size()) {
            lineStart = cursor;
            lineEnd   = findLineEnd(body, lineStart, cursor);
        }
        if (lineStart >= body.size()) break;
        if (isBlankLine(body, lineStart, lineEnd)) break;  // trailing blank, EOF

        BlockByteRange range;
        range.startByte = static_cast<quint32>(lineStart);

        if (isFenceOpenOrClose(body, lineStart, lineEnd)) {
            // Fenced code block. Read through close-fence or EOF.
            qsizetype lastIncludedEnd = lineEnd;
            while (cursor < body.size()) {
                const qsizetype nextStart = cursor;
                const qsizetype nextEnd   = findLineEnd(body, nextStart, cursor);
                lastIncludedEnd = nextEnd;
                if (isFenceOpenOrClose(body, nextStart, nextEnd)) break;
            }
            range.endByte = static_cast<quint32>(lastIncludedEnd);
            result.append(range);
            continue;
        }

        // Non-fence: collect lines until next blank line or EOF.
        qsizetype lastIncludedEnd = lineEnd;
        while (cursor < body.size()) {
            const qsizetype nextStart = cursor;
            const qsizetype nextEnd   = findLineEnd(body, nextStart, cursor);
            if (isBlankLine(body, nextStart, nextEnd)) break;
            lastIncludedEnd = nextEnd;
        }
        range.endByte = static_cast<quint32>(lastIncludedEnd);
        result.append(range);
    }

    return result;
}

}  // namespace Markoff::Detail
