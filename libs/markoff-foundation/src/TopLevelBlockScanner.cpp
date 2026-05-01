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

/// True if the line at [lineStart, lineEnd) opens a fenced code block.
/// Mirrors view-qml BlockWalker's regex `^```\s*(\S*)\s*$` — exactly
/// 3 backticks at the start of the line (no leading spaces, no tildes),
/// optionally followed by an info string and trailing whitespace.
bool isFenceOpen(const QByteArray &body, qsizetype lineStart, qsizetype lineEnd)
{
    // Need at least 3 chars and they must all be backticks.
    if (lineEnd - lineStart < 3) return false;
    if (body[lineStart]     != '`') return false;
    if (body[lineStart + 1] != '`') return false;
    if (body[lineStart + 2] != '`') return false;
    // Anything after the 3 backticks is fine — info string + trailing whitespace.
    // (Mirrors BlockWalker's regex which captures (\S*)\s*$ after the leading ```.)
    return true;
}

/// True if the line at [lineStart, lineEnd) closes a fenced code block.
/// Mirrors view-qml BlockWalker's regex `^```\s*$` — exactly 3 backticks
/// at the start of the line, followed only by whitespace.
bool isFenceClose(const QByteArray &body, qsizetype lineStart, qsizetype lineEnd)
{
    if (lineEnd - lineStart < 3) return false;
    if (body[lineStart]     != '`') return false;
    if (body[lineStart + 1] != '`') return false;
    if (body[lineStart + 2] != '`') return false;
    // After the 3 backticks, every byte must be whitespace.
    for (qsizetype i = lineStart + 3; i < lineEnd; ++i) {
        const char c = body[i];
        if (c != ' ' && c != '\t' && c != '\r') return false;
    }
    return true;
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

        if (isFenceOpen(body, lineStart, lineEnd)) {
            // Fenced code block. Read through close-fence or EOF.
            qsizetype lastIncludedEnd = lineEnd;
            while (cursor < body.size()) {
                const qsizetype nextStart = cursor;
                const qsizetype nextEnd   = findLineEnd(body, nextStart, cursor);
                lastIncludedEnd = nextEnd;
                if (isFenceClose(body, nextStart, nextEnd)) break;
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
