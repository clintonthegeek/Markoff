// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/SearchEngine.h>

#include <algorithm>

#include <QRegularExpression>

#include <markoff/core/MarkoffDocument.h>
#include <markoff/core/Selection.h>
#include <markoff/core/Session.h>

namespace Markoff {

namespace {
QList<Selection> matchesInOrder(Session *sess, const MarkoffDocument *doc) {
    QList<Selection> ms;
    for (const Selection &x : sess->secondarySelections())
        if (x.kind == Selection::Kind::SearchMatch) ms << x;
    std::sort(ms.begin(), ms.end(),
              [doc](const Selection &a, const Selection &b) {
                  return doc->resolveTextAnchor(a.anchor)
                       < doc->resolveTextAnchor(b.anchor);
              });
    return ms;
}
}

SearchEngine::SearchEngine(QObject *parent) : QObject(parent) {}
SearchEngine::~SearchEngine() = default;

int SearchEngine::findAll(MarkoffDocument *doc, Session *sess,
                           const QString &needle, FindFlags flags)
{
    if (!doc || !sess || needle.isEmpty()) {
        if (sess) sess->clearSecondarySelectionsOfKind(Selection::Kind::SearchMatch);
        return 0;
    }
    sess->clearSecondarySelectionsOfKind(Selection::Kind::SearchMatch);

    const QString hay = doc->toMarkdown();
    const Qt::CaseSensitivity cs = (flags & CaseSensitive)
        ? Qt::CaseSensitive : Qt::CaseInsensitive;

    QList<Selection> matches;
    if (flags & Regex) {
        QRegularExpression::PatternOptions opt = QRegularExpression::NoPatternOption;
        if (cs == Qt::CaseInsensitive) opt |= QRegularExpression::CaseInsensitiveOption;
        QRegularExpression re(needle, opt);
        if (!re.isValid()) return 0;
        auto it = re.globalMatch(hay);
        while (it.hasNext()) {
            const auto m = it.next();
            const int u16start = m.capturedStart();
            const int u16end   = m.capturedEnd();
            const quint32 bs = static_cast<quint32>(
                hay.left(u16start).toUtf8().size());
            const quint32 be = static_cast<quint32>(
                hay.left(u16end).toUtf8().size());
            Selection x;
            x.kind = Selection::Kind::SearchMatch;
            x.anchor = doc->textAnchorAt(bs, /*rightBias*/ false);
            x.active = doc->textAnchorAt(be, /*rightBias*/ true);
            matches << x;
        }
    } else {
        int from = 0;
        for (;;) {
            const int idx = hay.indexOf(needle, from, cs);
            if (idx < 0) break;
            const int u16end = idx + needle.size();
            const quint32 bs = static_cast<quint32>(
                hay.left(idx).toUtf8().size());
            const quint32 be = static_cast<quint32>(
                hay.left(u16end).toUtf8().size());
            Selection x;
            x.kind = Selection::Kind::SearchMatch;
            x.anchor = doc->textAnchorAt(bs, /*rightBias*/ false);
            x.active = doc->textAnchorAt(be, /*rightBias*/ true);
            matches << x;
            from = u16end;
            if (needle.isEmpty()) ++from;
        }
    }
    for (const Selection &x : matches) sess->addSecondarySelection(x);
    return matches.size();
}

bool SearchEngine::findNext(MarkoffDocument *doc, Session *sess)
{
    if (!doc || !sess) return false;
    const auto ms = matchesInOrder(sess, doc);
    if (ms.isEmpty()) return false;
    const quint32 cur = doc->resolveTextAnchor(sess->primarySelection().active);
    for (const Selection &x : ms) {
        if (doc->resolveTextAnchor(x.anchor) >= cur) {
            Selection p = x; p.kind = Selection::Kind::Primary;
            sess->setPrimarySelection(p);
            return true;
        }
    }
    Selection p = ms.first(); p.kind = Selection::Kind::Primary;
    sess->setPrimarySelection(p);
    return true;
}

bool SearchEngine::findPrevious(MarkoffDocument *doc, Session *sess)
{
    if (!doc || !sess) return false;
    const auto ms = matchesInOrder(sess, doc);
    if (ms.isEmpty()) return false;
    const quint32 cur = doc->resolveTextAnchor(sess->primarySelection().anchor);
    for (auto it = ms.crbegin(); it != ms.crend(); ++it) {
        if (doc->resolveTextAnchor(it->active) < cur) {
            Selection p = *it; p.kind = Selection::Kind::Primary;
            sess->setPrimarySelection(p);
            return true;
        }
    }
    Selection p = ms.last(); p.kind = Selection::Kind::Primary;
    sess->setPrimarySelection(p);
    return true;
}
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
