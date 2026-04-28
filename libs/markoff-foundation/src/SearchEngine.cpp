// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-foundation/SearchEngine.h>

#include <QRegularExpression>

#include <markoff-foundation/MarkoffDocument.h>
#include <markoff-foundation/Selection.h>
#include <markoff-foundation/Session.h>

#include <crdt/Anchor.h>

namespace Markoff {

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
            x.anchor = doc->anchorAt(bs, CollabText::Crdt::Bias::Left);
            x.active = doc->anchorAt(be, CollabText::Crdt::Bias::Right);
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
            x.anchor = doc->anchorAt(bs, CollabText::Crdt::Bias::Left);
            x.active = doc->anchorAt(be, CollabText::Crdt::Bias::Right);
            matches << x;
            from = u16end;
            if (needle.isEmpty()) ++from;
        }
    }
    for (const Selection &x : matches) sess->addSecondarySelection(x);
    return matches.size();
}

bool SearchEngine::findNext(MarkoffDocument *, Session *) { return false; }
bool SearchEngine::findPrevious(MarkoffDocument *, Session *) { return false; }
void SearchEngine::clearMatches(Session *sess)
{
    if (sess) sess->clearSecondarySelectionsOfKind(Selection::Kind::SearchMatch);
}

}  // namespace Markoff
