// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <markoff/core/Theme.h>

namespace Markoff::Canvas::Detail {

/// Parsed shape of a `BlockKind::BlockQuote` block's buffer (P5.5):
/// Obsidian-style callout syntax `[!type] Title...`. `KindInference`/the
/// load-time blockquote split (markoff-core/CLAUDE.md § "Buffer
/// convention") give every quoted paragraph the plain `BlockKind::
/// BlockQuote` kind with no further distinction — there is no parser- or
/// core-level concept of a callout at all — so this leaf reparses the
/// buffer itself to recognize the `[!type]` marker, same "canvas-local
/// rule" precedent as `MediaBlocks::parseImageBlock` (P5.4) and
/// `CodeHighlighting::parseCodeFence` (P4.6).
///
/// **Known information loss**: a BlockQuote block's buffer has already had
/// every internal '\n' collapsed to a space at load time (the B1/soft-break
/// canonicalisation markoff-core/CLAUDE.md documents for Paragraph/
/// ListItem/BlockQuote/setext-Heading kinds) — a multi-line callout like
/// `> [!note] My Title\n> body text` loses the line break between the
/// title and the body before this parser ever sees it, so a custom title
/// cannot be reliably distinguished from the following body prose. This
/// parser therefore does NOT attempt to recover a custom title: the
/// rendered header uses the fixed, capitalized type name (e.g. "Note"),
/// and everything after the `[!type]` marker — title text included, if the
/// source had one — renders as the indented body. See the P5.5 findings
/// log entry.
struct CalloutInfo {
    bool isCallout = false;
    /// Lowercased type key exactly as written inside the brackets (e.g.
    /// "note", "warning"). Empty when `isCallout` is false.
    QString typeKey;
    /// Display slot for the header icon/label/quote-bar color. Falls back
    /// to `Theme::Slot::CalloutNote` for a bracket type this leaf doesn't
    /// recognize (still a callout — just an unstyled/default-styled one),
    /// matching `CodeHighlighting`'s "service miss renders plain" fallback
    /// shape rather than refusing to treat it as a callout at all.
    Theme::Slot slot = Theme::Slot::CalloutNote;
    /// Fixed, capitalized display label for the header row (see the class
    /// doc comment on why this is the type name, not a recovered title).
    QString label;
    /// Single-glyph icon painted before the label.
    QString icon;
};

/// Recognizes the five Obsidian callout types this leaf has a Theme slot
/// for (note/tip/warning/important/caution). Returns a default
/// (`isCallout=false`) result for a buffer that isn't callout-shaped —
/// every other BlockQuote renders exactly as before this task.
CalloutInfo parseCallout(const QString &blockText);

}  // namespace Markoff::Canvas::Detail
