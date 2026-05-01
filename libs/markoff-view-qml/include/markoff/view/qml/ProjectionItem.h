// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QTextCharFormat>

#include <markoff-foundation/BlockAnchor.h>
#include <markoff-foundation/TextAnchor.h>

namespace Markoff::View::Qml {

/// Value types for the LiveProjectionLayer.
///
/// The layer holds two item kinds — Predictions (latency-bridging; bytes are
/// already in source, view runs ahead of parser) and Holes (intent-holding;
/// source has no representation yet, view holds the intent). Each kind exists
/// at two granularities: block and inline.
///
/// Stage-1 scope: these are pure value types with no behavior. Producers
/// (InlineFormatHighlighter, LiveSpeculativeFenceController, the future
/// empty-paragraph hole creator) attach later in Stages 2-4 of the plan.

/// Block-level hole: source has bytes (e.g. trailing `\n\n`) but the parser
/// produces no block past them. The layer carries a synthetic row until the
/// user reifies it (printable char), abandons it (focus-out, undo, idle), or
/// a remote edit invalidates the anchor.
struct BlockHole {
    /// Where in source this hole projects from. Synthetic until reified —
    /// foundation translation APIs must refuse to translate synthetic
    /// projection anchors (spec §5 invariant 13).
    Markoff::BlockAnchor origin;

    /// Semantic block kind this hole stands in for ("paragraph", "list_item",
    /// "heading", ...).
    QString kind;

    /// If the hole was created paired with a real CRDT edit (e.g. the `\n\n`
    /// insert that paired with the empty-paragraph hole), this is the byte
    /// count of that edit. Used by undo coalescing (spec §6) so Ctrl+Z while
    /// the hole is unreified drops the hole AND triggers the CRDT undo for
    /// the paired edit, in one user-visible step.
    quint32 pairedSourceEditByteCount = 0;
};

/// Inline-level hole: a placeholder slot inside a row whose source can't yet
/// express the user's intent (e.g. the empty wikilink target inside `[[`).
struct InlineHole {
    /// Where inside the row this hole sits. Synthetic until reified.
    Markoff::TextAnchor origin;

    /// Semantic inline kind this hole stands in for ("wikilink_target",
    /// "link_target", ...).
    QString kind;
};

/// Inline-level prediction: a `QTextCharFormat` to apply over a byte range of
/// a row's source, ahead of parser confirmation (e.g. open `**` styling
/// before the closing `**` arrives).
struct InlinePrediction {
    /// Row in the LiveBlockModel this prediction applies to.
    int row = -1;

    /// Range inside the row's text (UTF-16 character offsets, [start, end)).
    /// These match `QSyntaxHighlighter::setFormat` arguments directly.
    int charStart = 0;
    int charEnd = 0;

    /// Format to apply. Highlighter consumers paint this on top of plain
    /// text in `formatBlock`.
    QTextCharFormat format;
};

/// Block-level kind prediction: speculatively flips a row's `kind` (e.g.
/// paragraph → code_block when `` ``` `` is typed at the start of the row)
/// pending parser confirmation.
struct BlockKindPrediction {
    /// Row whose kind is being flipped.
    int row = -1;

    /// The parser-confirmed kind we are speculating away from. If parser
    /// truth comes back contradicting the speculation, the row snaps back
    /// to this kind.
    QString originalKind;

    /// The speculative kind the layer presents while waiting for parse
    /// confirmation.
    QString speculativeKind;
};

}  // namespace Markoff::View::Qml
