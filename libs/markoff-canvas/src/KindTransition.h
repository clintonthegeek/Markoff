// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

#include <markoff/core/BlockKind.h>

namespace Markoff::Canvas::Detail {

/// Infer the BlockKind for a block's source text. Rules applied in order;
/// first match wins. Ported from Markoff::Live::inferBlockKind
/// (libs/markoff-live/src/KindTransition.cpp) — that copy is leaf-internal
/// and this leaf may not link markoff-live (constitution C3's link-line
/// half), so the *rules* are duplicated rather than shared. Plan T6 notes
/// this as spike-throwaway: the real leaf should promote the helper into
/// markoff-core so both leaves consume one copy.
///
/// Unlike the live version, Math's `$`-vs-`$$` display-mode distinction is
/// not surfaced (no out-param) — T6's scope is the Paragraph-to-Heading
/// path (exit E5); wiring the Math display-mode attr is left for whoever
/// extends this beyond the spike.
Markoff::BlockKind inferBlockKind(const QString &text);

/// Count leading '#' characters before a space/EOL. Returns 0 if not a heading.
int countLeadingHashes(const QString &text);

/// Returns 1 if `text` ends with a `===`-form setext H1 underline (preceded
/// by a non-blank line), 2 if `---`-form H2, 0 otherwise. Underline must be
/// the last line of `text`; the line directly above it must be non-blank
/// (CommonMark setext rules).
int matchesSetextShape(const QString &text);

}  // namespace Markoff::Canvas::Detail
