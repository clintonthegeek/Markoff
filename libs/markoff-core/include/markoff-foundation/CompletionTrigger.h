// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <markoff-foundation/MarkoffFoundationExport.h>

namespace Markoff {

enum class CompletionTrigger {
    None,
    WikiLink,
    WikiLinkAnchor,
    Tag,
    Footnote,
    Emoji,
    Mention,
    SlashCommand,
    LinkPath,
    ImagePath,
};

}  // namespace Markoff
