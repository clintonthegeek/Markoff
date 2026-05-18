// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MARKOFF_LIVE_INTERNAL_LINK_HITTEST_H
#define MARKOFF_LIVE_INTERNAL_LINK_HITTEST_H

#include <markoff/core/BlockId.h>
#include <markoff/core/LinkActivation.h>
#include <markoff/parser/SourceSpan.h>

#include <Qt>

namespace Markoff {
class MarkoffDocument;
class LinkService;
}

namespace Markoff::LiveInternal {

struct LinkHit {
    bool found = false;
    Markoff::SourceSpan span;
};

/// Find the link-or-wikilink span containing qtPos in the block's inline spans.
/// Returns { true, span } on hit; { false, {} } if no link span covers qtPos.
LinkHit findLinkSpanAt(Markoff::MarkoffDocument *doc,
                       Markoff::BlockId blockId, int qtPos);

/// Build a LinkActivation from a hit span.
Markoff::LinkActivation buildActivation(const Markoff::SourceSpan &span,
                                        Qt::KeyboardModifiers mods,
                                        const QString &fromContext,
                                        Markoff::LinkService *service);

}  // namespace Markoff::LiveInternal

#endif  // MARKOFF_LIVE_INTERNAL_LINK_HITTEST_H
