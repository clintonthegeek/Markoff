# markoff-styled

Plain-jane QWidget Markoff editor on `markoff-core`. Third view leaf
alongside `markoff-source` and `markoff-live`. No QML, no KF6.

> **Required reading before seam work:**
> [`../../docs/VIEW-IMPLEMENTORS-GUIDE.md`](../../docs/VIEW-IMPLEMENTORS-GUIDE.md)
> — the cross-cutting view↔model concerns and contracts.
>
> §A (text-sync) and §B.1/§B.3 (Enter/merge caret authority) are **solved**
> (2026-05-27, spec
> `../../docs/specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`).
> §B.2/§B.4 are partials (collab/undo caret); see the guide.

## Public surface
- `Markoff::Styled::Editor` — `Markoff::MarkdownView` subclass composing
  a `QTextEdit`. Setters: `setDocument`, `setSession`, `setTheme`,
  `setLinkService`, `setFromContext`, `setFontScale`.

## Internal
- `Markoff::Styled::StyleApplier` — subscribes to
  `MarkoffDocument::d2DocumentChanged`; applies `QTextBlockFormat` +
  `QTextCharFormat` from `iterateBlocks()` + `inlineSpansFor()`.
- `Markoff::Styled::DocHighlighter` — whole-doc `QSyntaxHighlighter`
  stub. v0 inert; v0.1 owns cursor-aware delimiter visibility.
- `Markoff::Styled::LinkInteraction` — event-filter on the
  `QTextEdit`'s viewport. Routes mouse press / move / leave through the
  configured `Markoff::LinkService`.

## Dependencies
- Qt6 Core / Gui / Widgets
- `markoff-core` (transitive `markoff-parser`)

No KF6, no QML, no `markoff-live`.

## Conventions
- C++20, Qt6.8+, CMake 3.19+.
- SPDX `GPL-3.0-or-later` on every file.
- `tr()` for user-visible strings.
- Tests prefix `tst_styled_*`. All test binaries run under
  `QT_QPA_PLATFORM=offscreen`.

## v0.1 invariants

- **Per-block hash gating.** `StyleApplier::applyFormats` skips blocks
  whose `(kind, text, spans, fontScale)` hash is unchanged. Test:
  `tst_styled_dogfood_invariants::hash_gate_skips_unchanged_blocks`.
  When adding new format inputs (e.g., a new `SourceSpan` flag), extend
  the bit-pack in `computeBlockHash` to include it, or risk a missed
  restyle on the change.
- **Kind transition via `Cmd::changeKind`.** Prefix-rule kind
  inference (Heading via leading `#`, BlockQuote via `> `, ListItem
  via list-marker regex) runs inside the block walk; on disagreement
  with the stored kind, `Cmd::changeKind` is queued for deferred
  dispatch via `QTimer::singleShot(0)` to avoid synchronous re-entry
  into `d2DocumentChanged`. CodeBlock and HorizontalRule are NOT
  inferred (fence-state matching; left to the CRDT load path until
  v0.2).
- **Scroll position preserve.** In-place edits (no block added/removed)
  preserve `verticalScrollBar()->value()`. Capture happens via
  `StyleApplier::captureScrollBeforeEdit` connected to
  `d2DocumentChanged` BEFORE the binding's `onD2DocumentChanged`
  (Qt's FIFO connection delivery guarantees order); restore via
  `QTimer::singleShot(0)` AFTER `endEditBlock` so Qt's layout signals
  settle first. Structural edits let Qt's natural "ensure cursor
  visible" behavior position the viewport.
- **D2-broken core APIs to avoid.** `MarkoffDocument::blockAt(TextAnchor)`
  and `MarkoffDocument::blockByteRange(BlockId)` both depend on
  `latestBlockRanges`, which is NEVER populated by the D2 load path —
  they return `std::nullopt` for any D2-loaded document. The styled
  leaf works around both: `StyleApplier::applyFormats` reconstructs
  byte ranges by walking `blockText(id).size() + interBlockSeparator`
  for each block; `LinkInteraction::resolveLinkAt` uses
  `textAnchorAt(byteOffset).block()` to extract the containing
  BlockId from the anchor itself. When in doubt about a core API,
  search `latestBlockRanges` first.
- **WP unification (2026-05-28).** The Editor's QTextDocument is seeded
  from `widgetFlatView()` (single-`\n` separator). Each model block →
  one QTextBlock; the visible inter-paragraph gap comes from per-kind
  margins, not from extra `\n`s in the flat text. An empty model block
  renders as an empty QTextBlock whose margins contribute the "extra
  gap" signal of one Enter. Spec
  `../../docs/specs/2026-05-28-flat-view-wp-unification-design.md`.
- **Em-based spacing (2026-05-29).** All per-kind block margins, list
  indent, code-block left-margin, body font size, and
  `QTextDocument::indentWidth` are computed from `kBaseBodyPt × fontScale`.
  Zoom in/out scales spacing proportionally with text. Named multiplier
  helpers (`paragraphMarginPt`, `listItemMarginPt`, `docIndentWidthPx`,
  `headingTopMarginPt`, etc.) live at the top of `StyleApplier.cpp` —
  tuning is a one-line constant change. Targets: ~1em between paragraphs,
  ~0.36em between list items, ~1.15em above headings.
- **ListItem marker rendering (2026-05-29).** `applyListItem` reads
  `MarkerStyle`, `IndentLevel`, and (for tasks) `Checked` from the block
  attrs — depth is no longer derived from buffer leading whitespace
  (which is always 0 post-marker anyway). Task-list checkboxes use the
  native `QTextBlockFormat::MarkerType::{Unchecked,Checked}`. Bullets
  and decimals come from a per-item `QTextList` (`ListDisc` for
  minus/plus/star markers; `ListDecimal` for dot/paren). Single-item
  lists render the marker correctly but ordered items always read `1.`;
  sibling-grouping is a v0.2 follow-up (see queue #8).
- **Hash gate is text-only (caveat).** `computeBlockHash` covers
  `(kind, text, spans, fontScale)` — **not** attrs. Attr-only mutations
  (`toggleListItemChecked`, `IndentLevel` rewrite, marker-style flip)
  leave the hash unchanged and skip the format reapplication. Add attrs
  to the hash before relying on attr changes restyling automatically.

## Resolved binding bugs (2026-05-27)

The class of bug reported during 2026-05-27 dogfood — boundary drift on
separator-spanning edits + `setPlainText`-wipe on every model change —
has been resolved at the `markoff-core` binding layer, not in the styled
leaf itself. The fix lives in `SourceTextDocumentBinding`'s forward path
(sep-view dispatch + direct D2 merge primitives for cross-block deletes)
and reverse path (incremental prefix/suffix text-diff instead of
`setPlainText`). Reference spec:
`docs/specs/2026-05-27-markoff-core-binding-robustness-design.md`.

The existing **D2-broken core APIs** caveat below still applies (`blockAt`
/ `blockByteRange` → `latestBlockRanges` not populated by D2 load path).

## Known v0 gaps (track in `docs/queue.md`)
- **Delimiter visibility** is v0.1 work (`DocHighlighter` currently
  inert).
- **Find bar** + `FindController` integration is v0.1.
- **Math / image / table / callout** rendering is v0.2+.

## Spec
`docs/specs/2026-05-26-markoff-styled-leaf-design.md`

## Plan
`docs/plans/2026-05-26-markoff-styled-leaf.md`
