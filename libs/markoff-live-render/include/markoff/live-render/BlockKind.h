// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <QString>

namespace Markoff::LiveRender {

/// String constants for the five built-in block kinds. String-keyed
/// (not a closed enum) so plugin-registered kinds don't require
/// recompiling this library — they call BlockKindRegistry::register_.
namespace BlockKind {
    MARKOFF_LIVE_RENDER_EXPORT extern const QString Paragraph;       // "paragraph"
    MARKOFF_LIVE_RENDER_EXPORT extern const QString Heading;         // "heading"
    MARKOFF_LIVE_RENDER_EXPORT extern const QString CodeBlock;       // "code-block"
    MARKOFF_LIVE_RENDER_EXPORT extern const QString HorizontalRule;  // "hr"
    MARKOFF_LIVE_RENDER_EXPORT extern const QString Image;           // "image"
}

}  // namespace Markoff::LiveRender
