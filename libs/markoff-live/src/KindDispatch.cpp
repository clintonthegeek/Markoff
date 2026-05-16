// SPDX-License-Identifier: GPL-3.0-or-later
#include "KindDispatch.h"

namespace Markoff::Live {

QString delegateClassFor(const QString &kind)
{
    if (kind == QStringLiteral("code-block")) return QStringLiteral("code-block");
    if (kind == QStringLiteral("math"))       return QStringLiteral("math");
    if (kind == QStringLiteral("hr"))         return QStringLiteral("hr");
    if (kind == QStringLiteral("image"))      return QStringLiteral("image");
    // paragraph, heading, blockquote, list-item, and unknown (future plugin
    // kinds) fall into the text-inline bucket.
    return QStringLiteral("text-inline");
}

}  // namespace Markoff::Live
