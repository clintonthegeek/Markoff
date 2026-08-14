// SPDX-License-Identifier: GPL-3.0-or-later
#include "MediaBlocks.h"

namespace Markoff::Canvas::Detail {

ImageBlockInfo parseImageBlock(const QByteArray &blockTextBytes)
{
    ImageBlockInfo info;
    const QString text = QString::fromUtf8(blockTextBytes).trimmed();

    if (text.startsWith(QStringLiteral("![["))) {
        info.isEmbed = true;
        const int close = text.indexOf(QStringLiteral("]]"));
        const QString inner = close > 3 ? text.mid(3, close - 3) : QString();
        const int pipe = inner.indexOf(QLatin1Char('|'));
        if (pipe >= 0) {
            info.target     = inner.left(pipe).trimmed();
            info.altOrAlias = inner.mid(pipe + 1).trimmed();
        } else {
            info.target = inner.trimmed();
        }
        return info;
    }

    if (!text.startsWith(QStringLiteral("![")))
        return info;  // Not actually image-shaped — defensive default.

    const int altEnd = text.indexOf(QLatin1Char(']'), 2);
    if (altEnd < 2)
        return info;
    info.altOrAlias = text.mid(2, altEnd - 2);

    const int parenStart = text.indexOf(QLatin1Char('('), altEnd);
    if (parenStart < 0)
        return info;
    const int parenEnd = text.indexOf(QLatin1Char(')'), parenStart);
    if (parenEnd < 0)
        return info;

    QString inside = text.mid(parenStart + 1, parenEnd - parenStart - 1);
    // Strip an optional ` "title"` suffix — target is the URL/path only.
    const int titleQuote = inside.indexOf(QStringLiteral(" \""));
    if (titleQuote >= 0)
        inside = inside.left(titleQuote);
    info.target = inside.trimmed();
    return info;
}

}  // namespace Markoff::Canvas::Detail
