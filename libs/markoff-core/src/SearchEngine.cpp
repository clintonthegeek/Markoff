// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDebug>
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
bool SearchEngine::supports(FindFlag flag)
{
    return flag == NoFlags || flag == CaseSensitive;
}

QList<SearchHit> SearchEngine::findByBlock(const MarkoffDocument &doc,
                                            const QString &needle,
                                            FindFlags flags)
{
    // Only CaseSensitive is implemented. The other flags are declared API, so
    // a release build must not pretend to honour them: Q_ASSERT compiles out
    // under NDEBUG, which made a "regex" search a silent literal search
    // (ASK-M11). Refuse the request, loudly, and return no matches.
    if (flags & (Regex | WholeWords | Backwards)) {
        qWarning() << "Markoff::SearchEngine::findByBlock: unsupported FindFlags"
                   << int(flags & (Regex | WholeWords | Backwards))
                   << "- only CaseSensitive is implemented; returning no matches";
        return {};
    }

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
            // Case-insensitive: convert the block to QString once, search per
            // u16 index, and carry a running UTF-16→UTF-8 byte cursor. The old
            // form re-serialised the whole prefix twice per match, so a short
            // needle in a large block was O(len x matches) (ASK-M16).
            const QString blockStr = QString::fromUtf8(blockBytes);
            int u16from = 0;
            int u16at = 0;    // u16 index the byte cursor stands at
            int byteAt = 0;   // UTF-8 byte offset of u16at
            for (;;) {
                const int u16idx = blockStr.indexOf(needle, u16from, Qt::CaseInsensitive);
                if (u16idx < 0) break;
                if (u16idx > u16at) {
                    byteAt += blockStr.mid(u16at, u16idx - u16at).toUtf8().size();
                    u16at = u16idx;
                }
                const int u16end = u16idx + needle.size();
                const int byteLen = blockStr.mid(u16at, u16end - u16at).toUtf8().size();
                hits.append(SearchHit{id, static_cast<uint32_t>(byteAt),
                                      static_cast<uint32_t>(byteLen)});
                byteAt += byteLen;
                u16at = u16end;
                u16from = u16end;
            }
        }
    }
    return hits;
}

}  // namespace Markoff
