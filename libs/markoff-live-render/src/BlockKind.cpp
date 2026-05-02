// SPDX-License-Identifier: GPL-3.0-or-later
#include <markoff/live-render/BlockKind.h>

namespace Markoff::LiveRender::BlockKind {

const QString Paragraph      = QStringLiteral("paragraph");
const QString Heading        = QStringLiteral("heading");
const QString CodeBlock      = QStringLiteral("code-block");
const QString HorizontalRule = QStringLiteral("hr");
const QString Image          = QStringLiteral("image");

}  // namespace Markoff::LiveRender::BlockKind
