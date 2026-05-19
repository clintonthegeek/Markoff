// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live/MarkoffLiveExport.h>
#include <markoff/core/BlockAnchor.h>
#include <markoff/core/BlockAttrsMap.h>
#include <markoff/parser/SourceSpan.h>

#include <QString>
#include <QHash>
#include <QList>

namespace Markoff::Live {

/// A snapshot of one top-level block from the parsed document.
/// Source-faithful: `text` holds the raw markdown bytes for the block's
/// [byteStart, byteEnd) range as they appear in the body buffer.
/// Kind-specific extras (headingLevel, codeLanguage) are populated where
/// the parser surfaces them.
///
/// `inlineSpans` is the pre-baked inline formatting data from R1B's
/// TopLevelBlock::inlineSpans. Populated by BlockWalker. Read-only in R2;
/// consumed by InlineFormatHighlighter in R6.
///
/// `blockAnchor` is populated from `doc->iterateBlocks()` in
/// `LiveListModelBinding::onD2Changed` during each parse update.
struct MARKOFF_LIVE_EXPORT BlockRecord {
    QString              kind;
    QString              delegateClass;     ///< derived from kind; see Markoff::Live::delegateClassFor.
    QString              text;              ///< Source-faithful markdown for this block.
    int                  headingLevel = 0;  ///< 1–6 if kind=="heading"; else 0.
    QString              codeLanguage;      ///< Fence info-string if kind=="code-block".
    QString              headingForm;       ///< "atx" / "setext" if kind=="heading"; else empty.
    Markoff::BlockAnchor blockAnchor;       ///< CRDT-stable identity (block's first byte).
    /// Per-block inline-format spans (bold/italic/code/link/wikilink/tag/
    /// strikethrough/highlight) populated from the parsed AST and consumed
    /// by `InlineHighlighter` (E1). Load-bearing — do not strip.
    QList<Markoff::SourceSpan> inlineSpans;
    QHash<Markoff::AttrName, Markoff::AttrValue> attrs; ///< Block-kind attributes (e.g. level, infoString).

    bool operator==(const BlockRecord &o) const noexcept {
        // inlineSpans excluded from equality: diff identity is (kind, anchor).
        // Text changes update Equal rows via dataChanged without re-keying.
        // attrs IS included so that MarkerNumber/IndentLevel/etc. changes
        // (from renumber or Tab ops) trigger dataChanged without a kind change.
        return kind == o.kind && text == o.text
            && headingLevel == o.headingLevel
            && codeLanguage == o.codeLanguage
            && headingForm == o.headingForm
            && blockAnchor == o.blockAnchor
            && attrs == o.attrs;
    }
    bool operator!=(const BlockRecord &o) const noexcept { return !(*this == o); }
};

/// Diff identity key. Two blocks with the same delegateClass+anchor are
/// "the same row" across parses: within-class kind changes (paragraph→
/// heading) keep the row alive (delegate persists, TextEdit persists)
/// and propagate via dataChanged role hints. Cross-class kind changes
/// (paragraph→hr) produce Delete+Insert. See Markoff::Live::delegateClassFor.
struct MARKOFF_LIVE_EXPORT BlockKey {
    QString              delegateClass;
    Markoff::BlockAnchor anchor;
    bool operator==(const BlockKey &o) const noexcept {
        return delegateClass == o.delegateClass && anchor == o.anchor;
    }
    bool operator!=(const BlockKey &o) const noexcept { return !(*this == o); }
};

}  // namespace Markoff::Live
