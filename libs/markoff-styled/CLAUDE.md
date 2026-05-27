# markoff-styled

Plain-jane QWidget Markoff editor on `markoff-core`. Third view leaf
alongside `markoff-source` and `markoff-live`. No QML, no KF6.

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

## Known v0 gaps (track in `docs/queue.md`)
- **Kind transition on `applyFlatEdit`**: editing a block prefix (e.g.
  Paragraph → Heading by typing `## `) does NOT re-infer `blockKind()`.
  `markoff-live` runs `KindTransition::inferBlockKind` after every
  `d2DocumentChanged`; `markoff-styled` does not. The
  `remote_edit_replays_text_and_restyles` slot in
  `tst_styled_d2_integration.cpp` is marked `QEXPECT_FAIL` for this.
  Fix candidates: (a) replicate kind inference in `StyleApplier`; (b)
  move kind inference into `MarkoffDocument::applyFlatEdit` so all
  consumers benefit. Track via a future micro-spec.
- **Delimiter visibility** is v0.1 work (`DocHighlighter` currently
  inert).
- **Find bar** + `FindController` integration is v0.1.
- **Math / image / table / callout** rendering is v0.2+.

## Spec
`docs/specs/2026-05-26-markoff-styled-leaf-design.md`

## Plan
`docs/plans/2026-05-26-markoff-styled-leaf.md`
