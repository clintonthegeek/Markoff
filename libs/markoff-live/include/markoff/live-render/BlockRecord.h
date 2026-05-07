// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <markoff/live-render/MarkoffLiveRenderExport.h>
#include <markoff/core/BlockAnchor.h>
#include <markoff/core/BlockAttrsMap.h>
#include <markoff-parser/SourceSpan.h>

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
struct MARKOFF_LIVE_RENDER_EXPORT BlockRecord {
    QString              kind;
    QString              text;              ///< Source-faithful markdown for this block.
    int                  headingLevel = 0;  ///< 1–6 if kind=="heading"; else 0.
    QString              codeLanguage;      ///< Fence info-string if kind=="code-block".
    Markoff::BlockAnchor blockAnchor;       ///< CRDT-stable identity (block's first byte).
    QList<Markoff::SourceSpan> inlineSpans; ///< Pre-baked inline spans (R1B). Used in R6.
    QHash<Markoff::AttrName, Markoff::AttrValue> attrs; ///< Block-kind attributes (e.g. level, infoString).

    bool operator==(const BlockRecord &o) const noexcept {
        // inlineSpans excluded from equality: diff identity is (kind, anchor).
        // Text changes update Equal rows via dataChanged without re-keying.
        // attrs IS included so that MarkerNumber/IndentLevel/etc. changes
        // (from renumber or Tab ops) trigger dataChanged without a kind change.
        return kind == o.kind && text == o.text
            && headingLevel == o.headingLevel
            && codeLanguage == o.codeLanguage
            && blockAnchor == o.blockAnchor
            && attrs == o.attrs;
    }
    bool operator!=(const BlockRecord &o) const noexcept { return !(*this == o); }
};

/// Diff identity key. Two blocks with the same kind+anchor are "the same
/// block" across parses: content edits keep delegates alive; kind-changes
/// and structural edits (splits/merges) produce Delete+Insert pairs.
struct MARKOFF_LIVE_RENDER_EXPORT BlockKey {
    QString              kind;
    Markoff::BlockAnchor anchor;
    bool operator==(const BlockKey &o) const noexcept {
        return kind == o.kind && anchor == o.anchor;
    }
    bool operator!=(const BlockKey &o) const noexcept { return !(*this == o); }
};

}  // namespace Markoff::Live
