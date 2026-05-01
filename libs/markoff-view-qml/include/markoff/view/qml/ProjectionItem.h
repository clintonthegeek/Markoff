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

/// Block-level hole: a transient projection-layer row holding the user's
/// intent ("I'm now in a new paragraph") for which the source rope cannot
/// yet produce a block (e.g. an empty paragraph after Enter at end-of-block).
///
/// v1 semantics: the hole behaves like an IME preedit buffer. Source is NOT
/// written when the hole is created — `bufferText` accumulates locally as
/// the user types, and only on commit does the layer emit a single
/// `applyLocalEdit("\n\n" + bufferText)` at `reifyOffset`. Until commit, the
/// delegate stays alive and editable; abandon (focus-out, escape) is purely
/// non-destructive view-state cleanup. There is no `origin` BlockAnchor (the
/// hole has no real position in source) and no paired source edit.
///
/// v1 invariant: at most one block hole is pending at a time. The layer
/// enforces this — calling `createBlockHole` while one is pending first
/// commits-or-abandons the prior hole.
struct BlockHole {
    /// Monotonic id, assigned by the layer when the hole is created. Callers
    /// pass `id == 0` to mean "unassigned". The id is the handle used for
    /// `setBlockHoleBuffer` / `commitBlockHole` / `dropBlockHole` so that
    /// late callbacks against a long-gone hole can be safely no-op'd.
    quint64 id = 0;

    /// Semantic block kind this hole stands in for. v1 only uses
    /// "paragraph"; future kinds (e.g. "list_item", "code_block") are
    /// reserved.
    QString kind;

    /// Byte index in source where commit will land. The committed edit is
    /// `applyLocalEdit({ oldStart=reifyOffset, oldEnd=reifyOffset,
    /// newText="\n\n"+bufferText })`. Stable: source is not mutated while
    /// the hole is pending, so `reifyOffset` does not need updating.
    quint32 reifyOffset = 0;

    /// Local preedit content; empty until the user types into the hole.
    /// The model's `TextRole` for the hole row mirrors this string.
    QString bufferText;

    /// The hole occupies `viewRow = afterParsedRow + 1` in the model. A
    /// value of `-1` places the hole at row 0 (the empty-document case).
    int afterParsedRow = -1;
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
