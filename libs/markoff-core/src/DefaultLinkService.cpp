// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/core/DefaultLinkService.h>

namespace Markoff {

DefaultLinkService::DefaultLinkService(QObject *parent) : LinkService(parent) {}

LinkKind DefaultLinkService::classify(const QString &t) const
{
    if (t.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))   return LinkKind::External;
    if (t.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))  return LinkKind::External;
    if (t.startsWith(QStringLiteral("mailto:"), Qt::CaseInsensitive))   return LinkKind::External;
    return LinkKind::Unknown;
}

QUrl DefaultLinkService::resolve(const QString &t, const QString &) const
{
    return QUrl(t);
}

}  // namespace Markoff
