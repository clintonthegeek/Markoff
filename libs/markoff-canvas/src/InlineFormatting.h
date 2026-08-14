// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QList>
#include <QTextLayout>

#include <markoff/parser/SourceSpan.h>

#include "ProjectionMap.h"

namespace Markoff {
class Theme;
}

namespace Markoff::Canvas::Detail {

/// Whether `span` — a delimiter span — is currently hidden (spec §4.2/T7):
/// mirrors the live leaf's InlineHighlighter::delimiterShouldHide, minus the
/// selection-range reveal (no selection-touches-delimiter criterion exists
/// for this leaf). `cursorsInBlock` is every cursor's QChar position within
/// THIS block (today at most one — the caret, if it is in this block); a
/// span reveals if it is touched by *any* of them (F1a multi-cursor
/// readiness: one predicate over the set, not a repeated single-caret
/// check at each call site).
///
/// isTag/isListMarker/isBlockquoteMarker spans are always shown regardless
/// of cursor position (parity with live, though those flags never occur in
/// this leaf's content-only ListItem/BlockQuote buffers today).
bool delimiterShouldHide(const Markoff::SourceSpan &span, const QList<int> &cursorsInBlock);

/// Every isDelimiter span currently hidden, as QChar ranges in "full" space
/// (SourceSpan::charOffset/charLength's own space — pre-omission, and valid
/// against post-\n-substitution text too since that substitution never
/// shifts a QChar index). Feeds ProjectionMap::build directly.
QList<std::pair<int, int>> omittedDelimiterRanges(
    const QList<Markoff::SourceSpan> &spans, const QList<int> &cursorsInBlock);

/// Build QTextLayout format ranges for one block's inline spans (spec T7,
/// reworked for P2.1): bold/italic/inline-code styling. Hidden delimiter
/// spans are omitted entirely — they have already been removed from the
/// layout text by `projection`, so they need no format range at all (the
/// old "invisible foreground colour over a same-width run" mechanism is
/// gone). A shown delimiter span (e.g. a code-span backtick with the caret
/// inside its parent) still gets its own styling exactly like plain
/// content, same as before.
///
/// Ranges come back in LAYOUT QChar space (`projection`'s), ready to hand
/// straight to QTextLayout::setFormats() — content spans never overlap an
/// omitted run (spans are disjoint from the delimiter runs that hide), so
/// each one's [charOffset, charOffset+charLength) resolves unambiguously to
/// a single kept run.
QList<QTextLayout::FormatRange> inlineFormatRanges(
    const QList<Markoff::SourceSpan> &spans, const QList<int> &cursorsInBlock,
    const Markoff::Theme &theme, const ProjectionMap &projection);

}  // namespace Markoff::Canvas::Detail
