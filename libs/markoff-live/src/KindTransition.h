// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

namespace Markoff::Live {

/// Infer the BlockKind string for a block's source text.
/// Rules applied in order; first match wins.
/// If displayMode is non-null and the inferred kind is Math,
/// *displayMode is set to true for $$ prefix, false for $.
QString inferBlockKind(const QString &text, bool *displayMode = nullptr);

/// Count leading '#' characters before a space/EOL. Returns 0 if not a heading.
int countLeadingHashes(const QString &text);

/// Returns 1 if `text` ends with a `===`-form setext H1 underline (preceded
/// by a non-blank line), 2 if `---`-form H2, 0 otherwise. Underline must be
/// the last line of `text`; the line directly above it must be non-blank
/// (CommonMark setext rules).
int matchesSetextShape(const QString &text);

}  // namespace Markoff::Live
