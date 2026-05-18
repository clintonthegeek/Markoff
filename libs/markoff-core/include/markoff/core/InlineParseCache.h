// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/core/BlockId.h>
#include <markoff/core/MarkoffCoreExport.h>
#include <markoff/parser/SourceSpan.h>

#include <QHash>
#include <QList>
#include <QtTypes>

namespace Markoff {

class MarkoffDocument;

/// Per-block inline parse cache. Keyed by (BlockId, blockEditSequence): returns
/// cached spans on repeated reads of the same block generation; transparently
/// re-parses when the edit counter increments.
///
/// Holds a reference to the owning MarkoffDocument — lifetime must not exceed
/// the document's lifetime (guaranteed since this is owned by MarkoffDocument::Private).
class MARKOFF_CORE_EXPORT InlineParseCache {
public:
    explicit InlineParseCache(MarkoffDocument &doc);

    /// Returns the inline formatting spans for `id`. On the first call for a
    /// given (id, blockEditSequence) pair, parses the block's current UTF-8
    /// text via inlineSpansFor() and caches the result. On subsequent calls
    /// with the same edit sequence, returns the cached result directly.
    QList<SourceSpan> spansFor(BlockId id) const;

    /// Evict all cache entries. Call this when the document is reloaded so
    /// stale entries keyed by now-retired BlockIds do not linger.
    void clear() { m_cache.clear(); }

private:
    struct Entry {
        QList<SourceSpan> spans;
        quint64 cachedAtSeq = 0;
    };

    MarkoffDocument &m_doc;
    mutable QHash<BlockId, Entry> m_cache;
};

}  // namespace Markoff
