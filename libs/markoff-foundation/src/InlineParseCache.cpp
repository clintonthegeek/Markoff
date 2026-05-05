// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/InlineParseCache.h>
#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-parser/TreeSitterParser.h>

namespace Markoff {

InlineParseCache::InlineParseCache(MarkoffDocument &doc)
    : m_doc(doc)
{}

QList<SourceSpan> InlineParseCache::spansFor(BlockId id)
{
    auto curSeq = m_doc.blockEditSequence(id);
    auto it = m_cache.find(id);
    if (it != m_cache.end() && it->cachedAtSeq == curSeq)
        return it->spans;
    auto spans = inlineSpansFor(m_doc.blockText(id));
    m_cache.insert(id, Entry{spans, curSeq});
    return spans;
}

}  // namespace Markoff
