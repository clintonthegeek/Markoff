// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/parser/SourceSpan.h>

#include "Utf8OffsetMap.h"

namespace Markoff {

// ---------------------------------------------------------------------------
// UTF-8 → QString offset mapping
// ---------------------------------------------------------------------------

QList<int> buildUtf8ToCharMap(const QByteArray &utf8)
{
    // Shared with the parser's own table; see src/Utf8OffsetMap.h. No QString
    // is available here, so the result is not clamped to a decoded length.
    return Markoff::Detail::buildByteToCharMap(utf8);
}

} // namespace Markoff
