// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-parser/SourceSpan.h>

namespace Markoff {

// ---------------------------------------------------------------------------
// UTF-8 → QString offset mapping
// ---------------------------------------------------------------------------

QList<int> buildUtf8ToCharMap(const QByteArray &utf8)
{
    // For each byte position in the UTF-8 string, compute the corresponding
    // QString (UTF-16) character index. Multi-byte UTF-8 sequences map
    // multiple bytes to the same char index.
    QList<int> map(utf8.size() + 1, 0);

    int charIdx = 0;
    int i = 0;
    while (i < utf8.size()) {
        map[i] = charIdx;
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        int seqLen;
        if (c < 0x80)       seqLen = 1;
        else if (c < 0xE0)  seqLen = 2;
        else if (c < 0xF0)  seqLen = 3;
        else                 seqLen = 4;  // 4-byte → surrogate pair in UTF-16

        // All bytes in the sequence map to the same char index
        for (int j = 1; j < seqLen && (i + j) < utf8.size(); ++j)
            map[i + j] = charIdx;

        charIdx += (seqLen == 4) ? 2 : 1;  // surrogate pair = 2 UTF-16 units
        i += seqLen;
    }
    map[utf8.size()] = charIdx;  // sentinel for end-of-string

    return map;
}

} // namespace Markoff
