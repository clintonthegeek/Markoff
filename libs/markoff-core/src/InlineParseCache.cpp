// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/InlineParseCache.h>
#include <markoff/core/MarkoffDocument.h>
#include <markoff/parser/TreeSitterParser.h>
#include <markoff/parser/PerfProbe.h>

namespace Markoff {

InlineParseCache::InlineParseCache(MarkoffDocument &doc)
    : m_doc(doc)
{}

QList<SourceSpan> InlineParseCache::spansFor(BlockId id) const
{
    MARKOFF_PERF_SCOPE("core.InlineParseCache::spansFor");
    auto curSeq = m_doc.blockEditSequence(id);
    auto it = m_cache.find(id);
    if (it != m_cache.end() && it->cachedAtSeq == curSeq) {
        Markoff::Perf::Probe::instance().note(QStringLiteral("core.InlineParseCache.hit"));
        return it->spans;
    }
    Markoff::Perf::Probe::instance().note(QStringLiteral("core.InlineParseCache.miss"));
    auto spans = inlineSpansFor(m_doc.blockText(id));
    m_cache.insert(id, Entry{spans, curSeq});
    return spans;
}

}  // namespace Markoff
