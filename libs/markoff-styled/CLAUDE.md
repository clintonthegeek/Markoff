# markoff-styled

Plain-jane QWidget Markoff editor on `markoff-core`. Third view leaf
alongside `markoff-source` and `markoff-live`. No QML, no KF6.

> **Required reading before seam work:**
> [`../../docs/VIEW-IMPLEMENTORS-GUIDE.md`](../../docs/VIEW-IMPLEMENTORS-GUIDE.md)
> — the cross-cutting view↔model concerns and contracts. This leaf has
> §A (text-sync) solved and §B (cursor authority) **open** — the §B.1
> "Enter jumps to end of next paragraph" bug is the active frontier; the
> guide's §B is the design reference for the fix.

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
