// SPDX-License-Identifier: GPL-3.0-or-later
#include "Folding.h"

#include <algorithm>

#include <markoff/core/AttrNames.h>
#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffDocument.h>

#include "CalloutBlocks.h"

namespace Markoff::Canvas::Detail {

namespace {

int intAttr(const MarkoffDocument &doc, BlockId id, const AttrName &name, int fallback)
{
    const auto attrs = doc.blockAttrs(id);
    const auto it = attrs.constFind(name);
    if (it == attrs.cend())
        return fallback;
    const int *v = std::get_if<int>(&it.value());
    return v ? *v : fallback;
}

}  // namespace

FoldInfo resolveFoldable(const MarkoffDocument &doc, BlockId id)
{
    FoldInfo info;
    if (id.isNull())
        return info;

    const std::vector<BlockId> order = doc.iterateBlocks();
    const auto found = std::find(order.begin(), order.end(), id);
    if (found == order.end())
        return info;
    const size_t idx = size_t(std::distance(order.begin(), found));

    switch (doc.blockKind(id)) {
    case BlockKind::Heading: {
        const int level = std::clamp(intAttr(doc, id, AttrNames::Level, 1), 1, 6);
        QList<BlockId> body;
        for (size_t i = idx + 1; i < order.size(); ++i) {
            if (doc.blockKind(order[i]) == BlockKind::Heading) {
                const int otherLevel = std::clamp(intAttr(doc, order[i], AttrNames::Level, 1), 1, 6);
                if (otherLevel <= level)
                    break;
            }
            body << order[i];
        }
        if (!body.isEmpty()) {
            info.kind = FoldKind::Heading;
            info.body = std::move(body);
        }
        break;
    }

    case BlockKind::BlockQuote: {
        // Only the run's head paragraph (the one carrying the `[!type]`
        // marker) is ever a fold head — a continuation paragraph is never
        // callout-shaped per parseCallout's own contract, same reasoning
        // BlockPresentation::presentationFor already relies on.
        if (!parseCallout(QString::fromUtf8(doc.blockText(id))).isCallout)
            break;
        const int runId = intAttr(doc, id, AttrNames::BlockQuoteRunId, -1);
        if (runId < 0)
            break;
        QList<BlockId> body;
        for (size_t i = idx + 1; i < order.size(); ++i) {
            if (doc.blockKind(order[i]) != BlockKind::BlockQuote)
                break;
            if (intAttr(doc, order[i], AttrNames::BlockQuoteRunId, -1) != runId)
                break;
            body << order[i];
        }
        if (!body.isEmpty()) {
            info.kind = FoldKind::Callout;
            info.body = std::move(body);
        }
        break;
    }

    case BlockKind::ListItem: {
        const int indent = std::max(0, intAttr(doc, id, AttrNames::IndentLevel, 0));
        if (indent != 0)
            break;  // only a top-level item can be a fold head
        if (idx > 0 && doc.blockKind(order[idx - 1]) == BlockKind::ListItem)
            break;  // not the run's first item

        QList<BlockId> body;
        int topLevelCount = 1;  // counts id itself
        for (size_t i = idx + 1; i < order.size(); ++i) {
            if (doc.blockKind(order[i]) != BlockKind::ListItem)
                break;
            body << order[i];
            if (std::max(0, intAttr(doc, order[i], AttrNames::IndentLevel, 0)) == 0)
                ++topLevelCount;
        }
        if (!body.isEmpty() && topLevelCount >= kLongListFoldThreshold) {
            info.kind = FoldKind::LongList;
            info.body = std::move(body);
        }
        break;
    }

    default:
        break;
    }

    return info;
}

}  // namespace Markoff::Canvas::Detail
