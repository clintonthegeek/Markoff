// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace Markoff::Canvas::Detail {

/// Parsed shape of a `BlockKind::Paragraph` block's buffer (P5.5):
/// Markdown footnote-definition syntax `[^label]: content`.
///
/// **Discovery**: unlike frontmatter (which `markoff-parser`'s
/// `Document::extract` genuinely strips out of `extracted.body` before the
/// body is parsed into blocks), a footnote-definition line is only
/// *copied* into `extracted.footnotes` for numbering — `extract()` does
/// NOT remove it from `extracted.body` (`Document.cpp`: `out.body =
/// std::move(markdown)`, the same string the footnote regex ran over
/// in-place). A `[^1]: text` line therefore remains a completely ordinary
/// `BlockKind::Paragraph` block in `IdList`, with a real `BlockId` — this
/// task's plan wording ("Footnote definitions render at their block") was
/// right the first time; the initial approach here (a non-block trailing
/// band, mirroring frontmatter) was wrong and was replaced by this
/// per-block reparse once a test caught the two-blocks-not-one surprise.
/// Same "canvas-local rule" precedent as `CalloutBlocks`/`MediaBlocks`:
/// core has no `BlockKind` or parser concept of a footnote definition
/// either, so this leaf reparses the Paragraph buffer to recognize it.
struct FootnoteDefInfo {
    bool isFootnoteDef = false;
    /// The label inside the brackets (e.g. "1" for `[^1]: ...`). Empty
    /// when `isFootnoteDef` is false.
    QString label;
};

/// Recognizes `[^label]: ...` at the very start of `blockText` (a
/// `BlockKind::Paragraph` buffer). Mirrors markoff-parser's own
/// `Document::extract` regex (`^\[\^([^\]]+)\]:\s*(.+)$`) closely enough
/// for display purposes — this is presentation-only, never used to decide
/// what enters `FootnoteDefMap` (that stays core's job at load time).
FootnoteDefInfo parseFootnoteDef(const QString &blockText);

}  // namespace Markoff::Canvas::Detail
