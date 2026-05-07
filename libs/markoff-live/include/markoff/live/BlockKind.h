// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <QString>

namespace Markoff::Live {

/// String constants for the built-in block kinds. String-keyed
/// (not a closed enum) so plugin-registered kinds don't require
/// recompiling this library — they call BlockKindRegistry::register_.
namespace BlockKind {
    MARKOFF_LIVE_EXPORT extern const QString Paragraph;       // "paragraph"
    MARKOFF_LIVE_EXPORT extern const QString Heading;         // "heading"
    MARKOFF_LIVE_EXPORT extern const QString CodeBlock;       // "code-block"
    MARKOFF_LIVE_EXPORT extern const QString HorizontalRule;  // "hr"
    MARKOFF_LIVE_EXPORT extern const QString Image;           // "image"
    MARKOFF_LIVE_EXPORT extern const QString ListItem;        // "list-item"
    MARKOFF_LIVE_EXPORT extern const QString Blockquote;      // "blockquote"
    MARKOFF_LIVE_EXPORT extern const QString Math;            // "math"
}

}  // namespace Markoff::Live
