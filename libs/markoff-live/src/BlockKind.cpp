// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live/BlockKind.h>

namespace Markoff::Live::BlockKind {

const QString Paragraph      = QStringLiteral("paragraph");
const QString Heading        = QStringLiteral("heading");
const QString CodeBlock      = QStringLiteral("code-block");
const QString HorizontalRule = QStringLiteral("hr");
const QString Image          = QStringLiteral("image");
const QString ListItem       = QStringLiteral("list-item");
const QString Blockquote     = QStringLiteral("blockquote");
const QString Math           = QStringLiteral("math");

}  // namespace Markoff::Live::BlockKind
