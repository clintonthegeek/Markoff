// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QTextLayout>

#include "ProjectionMap.h"

namespace Markoff {
class Theme;
class SyntaxHighlightService;
}

namespace Markoff::Canvas::Detail {

/// Parsed shape of a fenced code block's buffer (P4.6). The buffer keeps its
/// fences inline per the core marker convention (markoff-core/CLAUDE.md
/// "Block buffer convention": CodeBlock is "Yes — fences inline"), so this
/// is a pure string-scan over `blockText()` — no parser/AST spans involved,
/// same "canvas-local rule" precedent as `delimiterShouldHide`'s
/// `isCodeBlockFence` case (fenced_code_block_delimiter/info_string/
/// language spans never carry a parent range, InlineFormatting.cpp).
struct CodeFenceInfo {
    /// The info string's first whitespace-delimited token (e.g. "cpp" out
    /// of "```cpp foo=bar"), passed to SyntaxHighlightService::highlight
    /// verbatim — no alias table. Empty when the block has no recognizable
    /// opening fence (e.g. an IndentedCodeBlock, whose buffer never carries
    /// backticks/tildes at all) or no info string was given.
    QString language;
    /// Byte range of the code content within `blockText()` (this block's
    /// own coordinate space, C4): [contentStart, contentEnd). Excludes both
    /// fence lines and the newline that separates the opening fence from
    /// the content. Empty ([contentStart, contentStart)) for a fence with
    /// no content yet (e.g. "```cpp\n```").
    int contentStart = 0;
    int contentEnd   = 0;
};

/// Scan a CodeBlock's buffer for its opening ```/~~~ fence's language token
/// and the byte range of its code content. `blockText` is the block's own
/// text (already fence-inclusive per the buffer convention). Returns a
/// default (empty language, empty range at offset 0) if `blockText` does
/// not start with a recognizable fence — the caller's job is to treat that
/// as a service miss (plain monospace, spec's own fallback wording).
CodeFenceInfo parseCodeFence(const QByteArray &blockText);

/// Build QTextLayout format ranges coloring one fenced code block's tokens
/// (spec P4.6): parses the fence, asks `service` for `CodeSpan`s over the
/// content bytes, and maps each span's content-relative UTF-8 byte offset
/// into layout QChar space via `projection` — the same mapping
/// InlineFormatting.cpp's spans go through, so the result is ready to
/// concatenate onto that function's own range list before the ONE
/// `QTextLayout::setFormats()` call in `BlockLayoutCache::rebuildInline`
/// (the T7-sanctioned atomic path: this function never touches a
/// QTextLayout directly, only produces data for the caller's single call).
///
/// Empty for: a non-fenced block (`parseCodeFence` returns no language), a
/// language the service does not recognize (`highlight()` returns no
/// spans — the "service miss renders plain monospace" fallback), or a
/// theme with no color defined for a token's slot (`Theme::colorForCodeToken`
/// returns an invalid QColor — skipped rather than forcing a color).
QList<QTextLayout::FormatRange> codeTokenFormatRanges(
    const QByteArray &blockText, const Markoff::SyntaxHighlightService &service,
    const Markoff::Theme &theme, const ProjectionMap &projection);

}  // namespace Markoff::Canvas::Detail
