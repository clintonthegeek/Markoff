// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

namespace Markoff::Live {

/// String-keyed adapter over `Markoff::inferBlockKind`
/// (`<markoff/core/KindInference.h>`), which owns the rules since P1.1.
/// This leaf's model is string-keyed (`Markoff::Live::BlockKind`, open to
/// plugin-registered kinds), so the enum result is mapped back to the
/// matching kind string here; kinds inference never returns map to
/// `BlockKind::Paragraph`.
///
/// If displayMode is non-null and the inferred kind is Math,
/// *displayMode is set to true for $$ prefix, false for $.
QString inferBlockKind(const QString &text, bool *displayMode = nullptr);

/// Count leading '#' characters before a space/EOL. Returns 0 if not a heading.
/// Forwards to `Markoff::countLeadingHashes`.
int countLeadingHashes(const QString &text);

/// Returns 1 if `text` ends with a `===`-form setext H1 underline (preceded
/// by a non-blank line), 2 if `---`-form H2, 0 otherwise.
/// Forwards to `Markoff::matchesSetextShape`.
int matchesSetextShape(const QString &text);

}  // namespace Markoff::Live
