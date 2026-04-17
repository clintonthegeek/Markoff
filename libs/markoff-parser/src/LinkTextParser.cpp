// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff-parser/LinkTextParser.h>

namespace Markoff {

LinkTarget parseLinktext(const QString &linktext)
{
    if (linktext.isEmpty())
        return {};

    const int hashIdx = linktext.indexOf(QLatin1Char('#'));
    if (hashIdx < 0)
        return {linktext, {}};

    return {linktext.left(hashIdx), linktext.mid(hashIdx)};
}

} // namespace Markoff
