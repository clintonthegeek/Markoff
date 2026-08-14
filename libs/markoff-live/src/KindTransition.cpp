// SPDX-License-Identifier: GPL-3.0-or-later
#include "KindTransition.h"

#include <markoff/core/KindInference.h>
#include <markoff/live/BlockKind.h>

namespace Markoff::Live {

int countLeadingHashes(const QString &text)
{
    return Markoff::countLeadingHashes(text);
}

int matchesSetextShape(const QString &text)
{
    return Markoff::matchesSetextShape(text);
}

QString inferBlockKind(const QString &text, bool *displayMode)
{
    const Markoff::KindInference r = Markoff::inferBlockKind(text);

    if (r.kind == Markoff::BlockKind::Math && displayMode)
        *displayMode = r.mathDisplay;

    switch (r.kind) {
    case Markoff::BlockKind::Heading:        return BlockKind::Heading;
    case Markoff::BlockKind::CodeBlock:      return BlockKind::CodeBlock;
    case Markoff::BlockKind::HorizontalRule: return BlockKind::HorizontalRule;
    case Markoff::BlockKind::Image:          return BlockKind::Image;
    case Markoff::BlockKind::Math:           return BlockKind::Math;
    case Markoff::BlockKind::ListItem:       return BlockKind::ListItem;
    case Markoff::BlockKind::BlockQuote:     return BlockKind::Blockquote;
    case Markoff::BlockKind::Table:          return BlockKind::Table;
    // Inference never returns these; they have no live kind string.
    case Markoff::BlockKind::Mermaid:
    case Markoff::BlockKind::HtmlBlock:
    case Markoff::BlockKind::Paragraph:      break;
    }
    return BlockKind::Paragraph;
}

}  // namespace Markoff::Live
