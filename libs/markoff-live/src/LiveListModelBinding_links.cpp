// SPDX-License-Identifier: GPL-3.0-or-later
#include "LiveListModelBinding_links.h"

#include <markoff/core/LinkKind.h>
#include <markoff/core/LinkService.h>
#include <markoff/core/MarkoffDocument.h>

#include <QLatin1Char>
#include <QString>
#include <QUrl>

namespace Markoff::LiveInternal {

LinkHit findLinkSpanAt(Markoff::MarkoffDocument *doc,
                       Markoff::BlockId blockId, int qtPos)
{
    if (!doc) return {};
    const QList<Markoff::SourceSpan> spans = doc->inlineSpansFor(blockId);
    for (const auto &s : spans) {
        if (!(s.isLink || s.isWikilink)) continue;
        if (qtPos >= s.charOffset && qtPos < s.charOffset + s.charLength)
            return { true, s };
    }
    return {};
}

Markoff::LinkActivation buildActivation(const Markoff::SourceSpan &span,
                                        Qt::KeyboardModifiers mods,
                                        const QString &fromContext,
                                        Markoff::LinkService *service)
{
    Markoff::LinkActivation a;
    a.kind        = span.isWikilink ? Markoff::LinkKind::WikiLink
                                    : (service ? service->classify(span.linkTarget.url)
                                               : Markoff::LinkKind::Unknown);
    a.page        = span.linkTarget.page;
    a.section     = span.linkTarget.section;
    a.blockRef    = span.linkTarget.blockRef;
    a.alias       = span.linkTarget.alias;
    a.anchorHint  = a.section;
    a.modifiers   = mods;
    a.fromContext = fromContext;

    if (span.isWikilink) {
        QString inner = a.page;
        if (!a.section.isEmpty())  inner += QLatin1Char('#') + a.section;
        if (!a.blockRef.isEmpty()) inner += QStringLiteral("#^") + a.blockRef;
        if (!a.alias.isEmpty())    inner += QLatin1Char('|') + a.alias;
        a.rawText        = QStringLiteral("[[%1]]").arg(inner);
        a.resolvedTarget = service ? service->resolve(a.rawText, fromContext) : QUrl{};
    } else {
        a.rawText        = span.linkTarget.url;
        a.resolvedTarget = service ? service->resolve(a.rawText, fromContext)
                                   : QUrl(a.rawText);
    }
    return a;
}

}  // namespace Markoff::LiveInternal
