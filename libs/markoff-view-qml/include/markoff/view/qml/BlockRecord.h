// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

#include <markoff-foundation/BlockAnchor.h>

namespace Markoff::View::Qml {

/// Kind-tagged record for a single AST block. As of Stage C-2/C-3 the
/// `text` and `source` fields are both source-faithful — they hold the
/// raw markdown bytes for the block's `[byteStart, byteEnd)` range
/// exactly as they appear in the document (including markers like
/// `## ` for headings or the surrounding fences for fenced code).
///
/// The source-faithful invariant is load-bearing for the live editor:
/// `LiveEditBinding::onContentsChange` translates a delegate-local
/// `qtPos` to a global byte offset as `blockStartQtPos + qtPos`, which
/// only works if the delegate's text matches the source bytes for the
/// block. Display-stripped variants (e.g. heading body without markers)
/// previously caused the cursor arithmetic to write characters before
/// the markers, silently corrupting the markup.
///
/// Kind-specific extras (`headingLevel`, `codeLanguage`, `imageSrc`/
/// `imageAlt`/`imageTitle`) are populated where applicable but are no
/// longer authoritative for rendering — delegates render `text`. They
/// remain for backward-compat consumers (e.g. ImageDelegate's `<Image>`
/// source binding) and may be empty when the producer doesn't surface
/// them. `codeText` is no longer populated; CodeBlockDelegate renders
/// the source-faithful `text` (which includes the fences).
struct BlockRecord {
    QString kind;          ///< One of BlockKind::*
    QString source;        ///< Raw markdown source for [byteStart, byteEnd) — equals `text`.
    QString text;          ///< Source-faithful renderable text — equals `source`.

    int     headingLevel = 0;   ///< 1..6 if kind==Heading; else 0
    QString imageSrc;           ///< URL/path if kind==Image (legacy; may be empty)
    QString imageAlt;           ///< Alt text if kind==Image (legacy; may be empty)
    QString imageTitle;         ///< Optional title if kind==Image (legacy; may be empty)
    QString codeLanguage;       ///< Fence info-string if kind==CodeBlock
    QString codeText;           ///< Legacy: body without fences. No longer populated; CodeBlockDelegate renders `text`.

    /// CRDT-stable identity for the block's first byte. Populated by
    /// LiveListModelBinding so LiveEditBinding can translate block-relative
    /// TextEdit positions to document-global byte offsets without a
    /// separate lookup. Default-constructed anchor is sentinel (invalid)
    /// and LiveEditBinding bails if it resolves to nothing.
    Markoff::BlockAnchor blockAnchor;

    bool operator==(const BlockRecord &o) const noexcept {
        return kind == o.kind && source == o.source && text == o.text
            && headingLevel == o.headingLevel
            && imageSrc == o.imageSrc && imageAlt == o.imageAlt && imageTitle == o.imageTitle
            && codeLanguage == o.codeLanguage && codeText == o.codeText
            && blockAnchor == o.blockAnchor;
    }
    bool operator!=(const BlockRecord &o) const noexcept { return !(*this == o); }
};

/// Minimal identity key used by AstBlockDiff. Two blocks with the same kind
/// and BlockAnchor are "the same block" from the diff's perspective.
/// BlockAnchor equality is CRDT-identity based (Lamport clock), so a block
/// that has been content-edited but not split/merged will still compare Equal
/// across parses — preventing spurious delegate destruction.
struct BlockKey {
    QString              kind;
    Markoff::BlockAnchor anchor;
    bool operator==(const BlockKey &o) const noexcept {
        return kind == o.kind && anchor == o.anchor;
    }
    bool operator!=(const BlockKey &o) const noexcept { return !(*this == o); }
};

}  // namespace Markoff::View::Qml
