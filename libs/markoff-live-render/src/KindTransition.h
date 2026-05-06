// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

namespace Markoff::LiveRender {

/// Infer the BlockKind string for a block's source text.
/// Rules applied in order; first match wins.
/// If displayMode is non-null and the inferred kind is Math,
/// *displayMode is set to true for $$ prefix, false for $.
QString inferBlockKind(const QString &text, bool *displayMode = nullptr);

/// Count leading '#' characters before a space/EOL. Returns 0 if not a heading.
int countLeadingHashes(const QString &text);

}  // namespace Markoff::LiveRender
