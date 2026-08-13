// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QColor>
#include <QList>
#include <QTextLayout>

#include <markoff/parser/SourceSpan.h>

namespace Markoff {
class Theme;
}

namespace Markoff::Canvas::Detail {

/// Build QTextLayout format ranges for one block's inline spans (spec T7):
/// bold/italic/inline-code styling, plus delimiter-visibility for emphasis/
/// strong markers and the block-level markers that live inline in the
/// buffer for this leaf's kinds (ATX '# ' prefixes, code fences — spec §9's
/// T1 finding: those two kinds keep their marker bytes in-buffer, unlike
/// ListItem/BlockQuote, which the parser already narrows to content). One
/// mechanism covers all three delimiter classes, per the plan's T7 note.
///
/// Ranges are in QChar space directly: SourceSpan::charOffset/charLength
/// are already block-relative QString indices (MarkoffDocument::
/// inlineSpansFor rebases them off buildUtf8ToCharMap), and the layout
/// text's only substitution ('\n' -> QChar::LineSeparator) is 1 QChar for
/// 1 QChar, so no byte<->QChar conversion is needed for span ranges
/// themselves — only the caret's byte offset needs the one helper, done by
/// the caller before this is invoked.
///
/// caretQCharOrNegative: the caret's QChar position within THIS block, or
/// -1 if the caret is not in this block. A delimiter reveals when the
/// caret touches its parent span's range ([parentCharStart-1,
/// parentCharEnd+1]) — same rule as the live leaf's
/// InlineHighlighter::delimiterShouldHide, kept for parity even though
/// isTag/isListMarker/isBlockquoteMarker spans do not occur in this leaf's
/// content-only ListItem/BlockQuote buffers.
///
/// invisibleColor: foreground painted over a hidden delimiter run so it
/// still occupies its width (spec T7: "invisible but occupying width" was
/// picked over an elide-and-remap layout string, per spec §9). Pass the
/// block's own background (or the editor background, if the block paints
/// none) so the delimiter glyph blends in rather than reading as a gap.
QList<QTextLayout::FormatRange> inlineFormatRanges(
    const QList<Markoff::SourceSpan> &spans, int caretQCharOrNegative,
    const Markoff::Theme &theme, const QColor &invisibleColor);

}  // namespace Markoff::Canvas::Detail
