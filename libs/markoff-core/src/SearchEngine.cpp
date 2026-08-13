// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SearchEngine.h>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>

namespace Markoff {

SearchEngine::SearchEngine(QObject *parent) : QObject(parent) {}
SearchEngine::~SearchEngine() = default;

void SearchEngine::clearMatches(Session *sess)
{
    if (sess) sess->clearSecondarySelectionsOfKind(Selection::Kind::SearchMatch);
}

// D2: per-block search — iterates blocks directly, no Session side-effects.
QList<SearchHit> SearchEngine::findByBlock(const MarkoffDocument &doc,
                                            const QString &needle,
                                            FindFlags flags)
{
    // Only CaseSensitive is implemented for findByBlock; Regex and WholeWords
    // are not yet supported and passing them is a programming error.
    Q_ASSERT(!(flags & Regex));
    Q_ASSERT(!(flags & WholeWords));

    QList<SearchHit> hits;
    if (needle.isEmpty()) return hits;

    const Qt::CaseSensitivity cs = (flags & CaseSensitive)
        ? Qt::CaseSensitive : Qt::CaseInsensitive;

    for (const BlockId id : doc.iterateBlocks()) {
        const QByteArray blockBytes = doc.blockText(id);
        if (blockBytes.isEmpty()) continue;

        if (cs == Qt::CaseSensitive) {
            // Fast ASCII-safe path: search raw UTF-8 bytes.
            const QByteArray needleUtf8 = needle.toUtf8();
            int from = 0;
            for (;;) {
                const int idx = blockBytes.indexOf(needleUtf8, from);
                if (idx < 0) break;
                hits.append(SearchHit{id,
                    static_cast<uint32_t>(idx),
                    static_cast<uint32_t>(needleUtf8.size())});
                from = idx + needleUtf8.size();
            }
        } else {
            // Case-insensitive: convert block to QString once, search per u16 index,
            // then map each match back to byte offsets.
            const QString blockStr = QString::fromUtf8(blockBytes);
            int u16from = 0;
            for (;;) {
                const int u16idx = blockStr.indexOf(needle, u16from, Qt::CaseInsensitive);
                if (u16idx < 0) break;
                const int u16end = u16idx + needle.size();
                const uint32_t byteStart = static_cast<uint32_t>(
                    blockStr.left(u16idx).toUtf8().size());
                const uint32_t byteEnd = static_cast<uint32_t>(
                    blockStr.left(u16end).toUtf8().size());
                hits.append(SearchHit{id, byteStart, byteEnd - byteStart});
                u16from = u16end;
            }
        }
    }
    return hits;
}

}  // namespace Markoff
