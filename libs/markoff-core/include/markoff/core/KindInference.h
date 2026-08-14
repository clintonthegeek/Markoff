// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <markoff/core/BlockKind.h>
#include <markoff/core/MarkoffCoreExport.h>

namespace Markoff {

/// Result of inferring a block's kind from its buffer text.
///
/// `headingLevel` and `setextHeading` are meaningful only when
/// `kind == BlockKind::Heading`; `mathDisplay` only when
/// `kind == BlockKind::Math`. All three carry their inert defaults
/// otherwise, so a caller may write them unconditionally as long as it
/// writes the kind's own attrs alongside.
struct KindInference {
    BlockKind kind = BlockKind::Paragraph;
    /// 1–6 for a Heading (ATX hash count, or 1/2 for a setext underline);
    /// 0 for every other kind.
    int headingLevel = 0;
    /// True when the Heading was matched by its setext underline rather
    /// than an ATX `#` prefix — i.e. AttrNames::HeadingForm is "setext".
    bool setextHeading = false;
    /// True for `$$` display math, false for `$` inline math.
    bool mathDisplay = false;
};

/// Infer the BlockKind for a block's source text. Rules are applied in a
/// fixed order and the first match wins:
///
///   empty → Paragraph; ATX heading; fenced code; setext heading;
///   single-line horizontal rule; image; math; list item; blockquote;
///   otherwise Paragraph.
///
/// This is the one copy of the view-driven kind-transition rules; both
/// markoff-live (through its string-keyed adapter in `KindTransition.h`)
/// and markoff-canvas consume it. Pure — no document, no view state.
///
/// The text passed in is the block's *buffer*, whose marker convention is
/// per-kind (see markoff-core/CLAUDE.md § "Buffer conventions"): Heading
/// and CodeBlock buffers keep their matched marker, ListItem and
/// BlockQuote buffers are content-narrowed. Consequently callers must only
/// run inference on blocks whose stored kind is Paragraph (or on the
/// marker-keeping kinds), never on a content-narrowed buffer.
MARKOFF_CORE_EXPORT KindInference inferBlockKind(const QString &text);

/// Count leading '#' characters before a space/EOL. Returns 0 if not an
/// ATX heading.
MARKOFF_CORE_EXPORT int countLeadingHashes(const QString &text);

/// Returns 1 if `text` ends with a `===`-form setext H1 underline (preceded
/// by a non-blank line), 2 if `---`-form H2, 0 otherwise. Underline must be
/// the last line of `text`; the line directly above it must be non-blank
/// (CommonMark setext rules).
MARKOFF_CORE_EXPORT int matchesSetextShape(const QString &text);

}  // namespace Markoff
