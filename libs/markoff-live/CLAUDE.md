# markoff-live — library guide

The active QML view leaf. D2's foundation (per-block CRDT buffers, sibling
causal-LWW maps) is consumed here through `LiveListModelBinding` /
`LiveBlockModel` / `LiveCursorState` / `LiveStructuralKeyHandler` /
`LiveEditBinding` / `BlockKindRegistry`.

## Status

- D2 (foundation reshape): consumed.
- D3 (view-layer adaptation): implemented and dogfooded for non-ListItem
  block kinds. ListItem path is mid-correction — see the active spec.

## Engineering discipline — this library is the seam

This library is where the focus/caret/block-change regressions
documented in the post-mortems live. The eight invariants in
[`../../docs/INVARIANTS.md`](../../docs/INVARIANTS.md) apply
directly to every file under `src/` and `qml/`. Read that file
before any non-trivial change here.

This library is also the **reference implementation** for the
cross-cutting view-seam concerns catalogued in
[`../../docs/VIEW-IMPLEMENTORS-GUIDE.md`](../../docs/VIEW-IMPLEMENTORS-GUIDE.md)
— `LiveCursorState::establishFocus`, the diff-driven model, kind
transition. When you change how this leaf solves one of those
concerns, update the guide's status line for it.

Of particular relevance:

- **L4 is not yet decided in writing** (invariant 2). Until it is,
  do not add a new path that asserts authority over block content
  on either side — model or delegate. If you must, your spec
  states which side wins. This is the precondition for any refactor
  of `LiveCursorState`, `LiveEditBinding`, `LiveListModelBinding`,
  or `Connections { onCursorChanged }` / `Component.onCompleted`
  blocks in delegates.
- **You will encounter `Qt.callLater` and re-entrance guards** in
  this directory (current count: 2 `Qt.callLater` sites —
  `MathDelegate.qml:141`, focus deferral during BlockInternalEdit;
  `TableDelegate.qml:133`, cell-focus restore after the binding
  cascade settles (E4 C3, tracked in `docs/queue.md`) — and
  re-entrance guards `m_applyingTextUpdate` in `LiveEditBinding` +
  `TableEditBinding`, `m_applyingSelectionEmit` in `LiveCursorState`). Each
  one is a vote for the seam being unsettled. Log them as you encounter
  them (invariant 8). Do not silently extend their lineage.
- **The QML integration harness exists** as of commit `0c2e72d`
  (`LiveRealisticInputHarness` wired into `tst_live_render_qml_integration`).
  This is the fixture for invariant 4. Use it for any test that
  must protect a QML-reached production path.

## Read before editing

In order:

1. [`../../docs/INVARIANTS.md`](../../docs/INVARIANTS.md) — engineering
   discipline; the eight invariants apply to every file under this library
2. `../../docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation
3. `../../docs/d-arc/d-arc-status.md` — live status
4. `../../docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` — original
   D3 spec; non-ListItem sections still authoritative
5. `../../docs/specs/2026-05-06-per-item-listitem-blocks-design.md` —
   **active corrective spec for ListItem.** Do not patch
   `LiveStructuralKeyHandler.cpp`'s ListItem section without reading this
   first. Commits `cc62280`, `799eb94`, `21b2ce3` are over-fits to be
   replaced; do not extend their lineage.

## Architecture

The layer stack (post-D3, per `2026-05-05-d3-view-layer-adaptation-design.md`
§2):

```
L8  Interactive blocks   (Math + BlockInternalEdit)
L7  Structured text      (ListItem, Blockquote)
L6  Other text blocks    (Heading, CodeBlock, HR, Image)
L5  Structural keys      (LiveStructuralKeyHandler dispatch)
L4  Block editing        (LiveEditBinding)
L3  Cursor + selection   (Shape 1 discriminated cursor; unified in LiveCursorState, D2 2026-05-19)
L2  Diff-driven model    (Myers over (kind, BlockId))
L1  Read-only render     (ListView + delegates)
L0  Coordinate primitives
```

**Cursor model.** Three variants (`TextCaret | BlockSelected |
BlockInternalEdit`) — see `markoff/live-render/Cursor.h` and the C-spec
section §3 carry-forward in the D3 spec.

**Cursor delivery.** `LiveCursorState::establishFocus(anchor, qtPos)` is
the chokepoint for all structural-event cursor placement. Pending
requests resolve when the target delegate registers via
`delegateAvailable` (typically at the end of the `onD2Changed`
cascade, signalled via `endStructuralCascade`). The earlier
binding-side `structuralRowsInserted` / `structuralRowRemoved` signal
path + `m_pendingRow` slot were retired in tier 4b
(`docs/specs/2026-05-16-tier-4b-pending-slot-consolidation-design.md`).
The parse-cycle path is gone.

**Kind transition.** View-driven, in `KindTransition.cpp`'s
`inferBlockKind`. Runs against each Equal-op block's text in
`LiveListModelBinding::onD2Changed`; calls `Cmd::changeKind` on mismatch.
Hardcoded prefix rules. The ListItem prefix detection becomes simpler
under per-item granularity (single-line text, not multi-line).

**Block kind dispatch.** `BlockKindRegistry` (per-document, not global)
implements `Markoff::BlockSerializerRegistry`. `BlockKindDescriptor`
declares per-kind `delegateUrl`, `consumedStructuralKeys`,
`supportedCursorVariants`, `serializer`. `LiveView.qml`'s
`DelegateChooser` picks the QML delegate by `model.kind` string.

## Building

Within the worktree:

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev --target markoff_live -j 8
cmake --build build-dev --target markoff-live-app -j 8
```

Run the test app:

```bash
./build-dev/bin/markoff-live-app <markdown-file>
```

## Testing

```bash
ctest --test-dir build-dev -R '^tst_live_render_' --output-on-failure
```

Live-render test executables: `tst_live_render_{coords, registry,
block_model, cursor, structural, kind_transition, paragraph_edit,
context_menu, skeleton}`. Per-layer per-test contract per the C spec
(`Lₙ` tests `L_{k<n}` as real, `L_{k>n}` absent).

## QML module + import URI

- Library URI: `org.markoff.live 1.0`
- App URI: `org.markoff.live.app 1.0` (private to `app/`).

## Inline-format highlighter (E1)

`Markoff::Live::InlineHighlighter` (header `<markoff/live/InlineHighlighter.h>`)
is a per-delegate `QSyntaxHighlighter` painting `QTextCharFormat` ranges
from `BlockRecord::inlineSpans`. The QML shim `InlineHighlighterAttached`
wraps it for delegate-side property bindings.

8 inline kinds rendered via existing `Markoff::Theme::Slot` tokens:
`bold` → `BoldEmphasis`, `italic` → `ItalicEmphasis`, `strikethrough` →
`StrikeEmphasis`, `code` → `InlineCode`, `highlight` → `Highlight`,
`isLink` → `Link`, `isWikilink` → `WikiLink`, `isTag` → `Tag`.

`highlightBlock` iterates character-by-character (not span-by-span) to
correctly merge overlapping spans without erasure.

Markers (`**`, `_`, `~~`, `` ` ``, `==`, `[]()`, `[[]]`, `#`) render
visible in E1; auto-hide is E2's work via `SourceSpan::isDelimiter`.

Tests: `tst_live_render_inline_per_kind`, `tst_live_render_inline_combined`,
`tst_live_render_inline_cross_delegate`, `tst_live_render_inline_edge_cases`,
`tst_live_render_inline_typing_perf`.

Known edge case: tight-list + fenced code block (no surrounding blank
lines) — parser misclassifies fence content as list-item text; tracked
for a future parser fix.

## Color binding convention (E2.6)

Every visible color in a delegate reads from `Markoff::Theme` via
`LiveListModelBinding`'s Q_INVOKABLE proxies. The pattern:

```qml
color: (root.liveBinding && root.liveBinding.theme)
       ? root.liveBinding.themeColorFor(Theme.TextDefault)
       : "#222222"
```

- The `root.liveBinding.theme` LHS read gives QML a NOTIFY dependency
  on the Theme Q_PROPERTY. Without this anchor, `themeColorFor` calls
  would not re-evaluate on dark toggle.
- The `"#xxxxxx"` fallback is the corresponding `defaultLight()`
  color, applied during the transient construction state before
  `liveBinding` is wired.
- Slot names spell symbolically via `Theme.<Name>` — the
  `Markoff::Theme` Q_GADGET is exposed via `QML_FOREIGN` in
  `ThemeForeign.h`.

Do **not** use `palette.text` / `palette.highlight` / `palette.mid` /
`palette.alternateBase` for editor colors. The QtQuick Controls palette
is OS-driven and won't follow our Theme. The only intentional surviving
palette usage in delegates is documented inline with
`// palette intentional — <reason>`.

Two slots are deliberately reused as multi-purpose accents:

- **`Quote`** — blockquote text + HR + placeholder borders + muted
  secondary accent. If E3+ callout coloring needs separation, the slot
  splits in its own spec.
- **`CodeBlockBackground`** — code-block surface + image-placeholder
  surface.

Spec: `docs/specs/2026-05-17-theme-color-wiring-design.md`.

## Public surface — MarkdownView contract overrides (contract-v2 arc, 2026-06-09)

`Markoff::Live::EditorWidget` implements the full base contract:

| Override | Notes |
|---|---|
| `setDocument` | wires `LiveListModelBinding` + auto-creates a `Session`; session destroyed in `~EditorWidget` |
| `cursorPosition()` | reads `LiveCursorState::currentTextCaret()`; maps (block row, qtPos) to flat-visual-line `CursorPos`; O(blocks); uncached (a cache would be a second cursor store — INVARIANTS #3) |
| `setCursorPosition()` | reverse maps to (row, qtPos); routes through `LiveCursorState::requestTextCaretAtRow`; out-of-range positions clamp to last block/line-end (never a no-op) |
| `scrollPositionVisualLine()` / `setScrollPositionVisualLine()` | reads/sets QML ListView contentY/contentHeight ratio; returns 0.0 before QML root loads |
| `setReadOnly(bool)` / `isReadOnly()` | base store + push to `LiveListModelBinding::readOnly` flag; the single authority all six mutation-ingress gates read |
| `hasCursor()` | returns `true` |
| `hasEditing()` | returns `!isReadOnly()` |
| `attachFindController` / `detachFindController` | forwarded to `LiveListModelBinding`'s attach hook; controller is consumer-owned; attach after `setDocument` |
| `undo()` / `redo()` | inherited from base (→ `doc->undoD2/redoD2`); no-op while read-only |
| `setTheme` | base store + signal; then forwards a widget-owned copy's address to the binding (two internal copy-buffers — no pointer-equality short-circuit; every call notifies QML) |
| `setFontScale` | base clamp + store + signal; then forwards the canonical base value to the binding |
| `toggleBold` / `toggleItalic` / `toggleStrikethrough` / `toggleInlineCode` | delegate to `LiveActionController` QAction triggers; respects read-only gating, undo/redo, and selection requirements via QAction enabled-state |
| `insertLink` | delegates to `LiveActionController` QAction trigger |
| `setHeadingLevel(int)` | delegates to `heading<N>Action()` trigger; levels 0..6; out-of-range: no-op |
| `contextChanged` signal | recomputed on `LiveCursorState::cursorChanged` AND model `dataChanged` kind-transition; change-gated; live does NOT have the source/styled staleness gap |
| `cursorPositionChanged` signal | emitted on every `LiveCursorState::cursorChanged` |
| `scrollPositionChanged` signal | emitted from QML ListView contentYChanged NOTIFY |

**Read-only mutation gates (spec §4.2):**
`binding()->readOnly` is the single authority. Six gates early-return on it:
`LiveEditBinding::onContentsChange`, `LiveStructuralKeyHandler::tryHandle`,
`LiveClipboardController::paste/pasteText/pastePrimary/cut/deleteSelection`,
`TableEditBinding::applyCellEdit`, `LiveActionController` (disables
cut/paste/delete/undo/redo/format/heading actions). Navigation, selection,
copy, link activation, zoom, and find keep working when read-only.

**Leaf-specific accessor:** `binding()` returns the raw `LiveListModelBinding*`
for leaf-specific wiring (e.g. direct `cursorState()` access). Stop using
`binding()->setTheme()` and `binding()->fontScale(...)` directly — route
through `EditorWidget::setTheme`/`setFontScale` instead.

## Conventions

- C++20, Qt 6.8+.
- `// SPDX-License-Identifier: GPL-3.0-or-later` on every file.
- C++ namespace: `Markoff::Live`.
- Test prefix `tst_live_render_*`.
- Public headers under `include/markoff/live/`; consumers include
  via `#include <markoff/live/HeaderName.h>`.
- No `Corbomite`-named types in the public API.
