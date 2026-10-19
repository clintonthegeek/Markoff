// SPDX-License-Identifier: GPL-3.0-or-later
//
// UTF-8 byte offset ↔ QString (UTF-16) unit offset mapping, shared by the
// parser's own tables and by bakeInlineSpansIntoBlocks().
//
// The sequence length is derived from a *validated* sequence: continuation
// bytes are checked, and overlong encodings, surrogates and truncated
// sequences are rejected. The old lead-byte-only rule desynchronised the map
// on Latin-1 or truncated input (ASK-M30), so every offset derived from it
// (headings, links, tags, footnote refs, inline spans) was silently skewed.
// A rejected byte counts as one replacement character, matching
// QString::fromUtf8's substitution.
#pragma once

#include <QByteArray>
#include <QList>

#include <algorithm>
#include <climits>

namespace Markoff::Detail {

/// Length in bytes of the well-formed UTF-8 sequence at `p`, or 0 when the
/// bytes there are not well-formed (bad lead/continuation byte, overlong,
/// surrogate, or shorter than `remaining`).
inline int utf8SequenceLength(const char *p, int remaining)
{
    if (remaining <= 0) return 0;
    const auto b = [p](int i) { return static_cast<unsigned char>(p[i]); };
    const unsigned char c = b(0);
    if (c < 0x80) return 1;

    const auto continuation = [&](int i) {
        return i < remaining && (b(i) & 0xC0) == 0x80;
    };

    if (c >= 0xC2 && c <= 0xDF)
        return continuation(1) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        if (remaining < 3 || !continuation(2)) return 0;
        const unsigned char b1 = b(1);
        if (c == 0xE0) return (b1 >= 0xA0 && b1 <= 0xBF) ? 3 : 0;  // no overlong
        if (c == 0xED) return (b1 >= 0x80 && b1 <= 0x9F) ? 3 : 0;  // no surrogates
        return (b1 & 0xC0) == 0x80 ? 3 : 0;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (remaining < 4 || !continuation(2) || !continuation(3)) return 0;
        const unsigned char b1 = b(1);
        if (c == 0xF0) return (b1 >= 0x90 && b1 <= 0xBF) ? 4 : 0;  // no overlong
        if (c == 0xF4) return (b1 >= 0x80 && b1 <= 0x8F) ? 4 : 0;  // <= U+10FFFF
        return (b1 & 0xC0) == 0x80 ? 4 : 0;
    }
    return 0;  // stray continuation byte or 0xF5..0xFF
}

/// Map each UTF-8 byte offset to a UTF-16 unit offset. A well-formed 4-byte
/// sequence maps to two units (surrogate pair); a malformed byte advances one
/// unit. `utf16Length` (when >= 0) clamps the result to the actual QString
/// length, so a caller that decoded the same bytes cannot read past it.
inline QList<int> buildByteToCharMap(const QByteArray &utf8, int utf16Length = -1)
{
    QList<int> map(utf8.size() + 1, 0);
    const int limit = utf16Length >= 0 ? utf16Length : INT_MAX;
    int charIdx = 0;
    int i = 0;
    while (i < utf8.size()) {
        const int clamped = std::min(charIdx, limit);
        map[i] = clamped;
        const int len = utf8SequenceLength(utf8.constData() + i, utf8.size() - i);
        const int advance = len > 0 ? len : 1;  // invalid byte: one replacement
        const int units = len == 4 ? 2 : 1;
        for (int j = 1; j < advance && (i + j) < utf8.size(); ++j)
            map[i + j] = clamped;
        charIdx = std::min(charIdx + units, limit);
        i += advance;
    }
    map[utf8.size()] = std::min(charIdx, limit);
    return map;
}

} // namespace Markoff::Detail
