# Theme color wiring — E2.6 extension

**Date:** 2026-05-17
**Branch:** `exploration/new-foundation`
**Phase:** E2.6 (post-dogfood extension)
**Status:** spec-approved

## Provenance

E2.6 dogfood (2026-05-17) found the Ctrl+Shift+D dark-mode toggle fired the
action chain correctly but produced no visible change in the editor. Root
cause: the `Markoff::Theme` infrastructure ships color values for ~40 slots
but only two consumer paths actually read color from those slots:

1. `InlineHighlighter::charFormat` — paints inline spans (bold, italic,
   inline-code, links, wikilinks, tags, highlight).
2. (None for whole-block surfaces.)

Every other color in the live-render UI — window background, default text
color, heading text colors, code-block background, blockquote accent, HR
line, list-marker text, image-placeholder borders, selection backdrops —
reads from QtQuick Controls' `palette.*` namespace (OS palette) or a
hardcoded hex literal. Toggling `Markoff::Theme`'s color slots between
`defaultLight()` and `defaultDark()` therefore has almost nothing to land
on visually.

The companion fix landed in commit `0aef0f2` (buffer-alternation in
`LiveListModelBinding::setTheme` so QML re-propagates) was *necessary* for
this spec's work but not *sufficient* — propagation works, but the
delegates don't consume the propagated colors.

## Goal

Make `Markoff::Theme` the sole authority for editor color in
`markoff-live`. Every visible surface that renders a color reads from a
Theme slot via the binding's Q_INVOKABLE proxies. Ctrl+Shift+D visibly
inverts the editor (background, default text, headings, code-block, HR,
blockquote, selection — the lot).

## Non-goals (deferred)

- New `Theme::Slot` enum values for surfaces the Theme doesn't already
  represent. Use existing slots even where the mapping is approximate
  (HR uses `Quote`; image-placeholder uses `CodeBlockBackground`); a future
  spec splits these if E3+ shows the consolidation is too aggressive.
- User-customizable themes / theme file persistence.
- Search-match color wiring (no search UI consumer in markoff-live yet).
- Token-level code highlighting (E5 or its own phase).
- System-palette tracking — this spec moves *away* from
  `QGuiApplication::palette()` as a color source.

## Current state audit

`grep -rn 'palette\.\|color:.*\(rgba\|"#' libs/markoff-live/qml/` produces
the full inventory. Summary by file:

- **`Main.qml`** — `ApplicationWindow` has no `color:` property set; window
  background is OS-driven.
- **`UnifiedInlineTextDelegate.qml`** — blockquote bar (`palette.highlight`,
  opacity 0.6), list-marker selection backdrop (`palette.highlight`), list
  marker text color (`palette.highlightedText`/`palette.text`), TextEdit
  `color` (`palette.text`).
- **`CodeBlockDelegate.qml`** — root background (`Qt.rgba(0, 0, 0, 0.05)`),
  TextEdit `color` (`palette.text`), language label (`palette.mid`),
  reference TextEdit (`palette.text`).
- **`ImageDelegate.qml`** — placeholder background (`palette.alternateBase`),
  placeholder border + icon + caption (`palette.mid`), selected border
  (`palette.highlight`).
- **`HorizontalRuleDelegate.qml`** — rule line (`palette.mid` or
  `palette.highlight` when selected), selected border (`palette.highlight`).
- **`MathDelegate.qml`** — placeholder muted (`palette.mid`), body
  (`palette.text`), selected border (`palette.highlight`).
- **`RemoteCursorOverlay.qml`** — collab cursor color comes from the remote
  cursor model, *not* Theme. Out of scope.

The two QtQuick Controls fallback colors used most are `palette.text` (default
text color) and `palette.highlight` (selection/accent). `palette.mid` covers
"muted secondary accent." All three are surveyed below.

## Approach

Three options were considered for the API surface; **Option A — Q_INVOKABLE
color proxies** is chosen for consistency with the existing
`themePixelSizeFor` / `themeFamilyFor` / `themeIsBold` / `themeIsItalic`
pattern that the font wiring already uses.

Rejected alternatives:

- **Option B — NOTIFY-able color Q_PROPERTYies per slot.** Heavier; forces
  a per-slot mapping decision baked into the binding's Q_OBJECT surface;
  many new signals.
- **Option C — Expose Theme as a value-typed Q_GADGET property.** Requires
  resolving the `QVariant(const Theme*)` dispatch limitation called out in
  `LiveListModelBinding.h:118-122`. Biggest refactor; touches the existing
  font proxy pattern too.

## API surface

One addition to `LiveListModelBinding`:

```cpp
Q_INVOKABLE QColor themeColorFor(int slot) const;
```

Reads from `themeBuffers[activeThemeIdx]` (the same buffer the font proxies
read from). Returns `QColor()` (invalid) for unknown slot indices.

Plus one QML-registration line in `LiveListModelBinding`'s module init (or
the markoff_live module's QML registration site, wherever that lives in the
build), making the `Markoff::Theme::Slot` enum reachable from QML as
`Theme.Slot.<name>` or — preferably — `Theme.<name>` via short-form
registration:

```cpp
qmlRegisterUncreatableMetaObject(
    Markoff::Theme::staticMetaObject,
    "org.markoff.live", 1, 0,
    "Theme",
    "Theme is a value type; do not instantiate from QML"
);
```

QML callers then write `themeColorFor(Theme.EditorBackground)` instead of
`themeColorFor(25)`.

The existing numeric callsites in `themePixelSizeFor(8 /* CodeBlock */)` and
peers are migrated opportunistically in the same files being touched for
color wiring. Not a separate sweep — touched-files-only.

## Propagation correctness

The buffer-alternation fix in commit `0aef0f2` is load-bearing.
`LiveListModelBinding::setTheme` alternates between two internal Theme
buffers so `theme()` returns a different pointer on every call. QML's
value-equality optimization sees a real change and re-evaluates all
expressions reading `binding.theme`.

The convention for every binding in this spec:

```qml
color: (root.liveBinding && root.liveBinding.theme)
       ? root.liveBinding.themeColorFor(Theme.TextDefault)
       : "#000000"
```

The `root.liveBinding.theme` read on the LHS gives QML a dependency on the
Theme Q_PROPERTY. When `themeChanged` fires, the LHS re-evaluates, the
pointer differs, the expression re-evaluates, `themeColorFor` reads from
the now-active buffer.

**Fallback rule:** every binding has an explicit hex literal fallback for
the transient `!root.liveBinding` or `!theme` case (object construction
ordering). Fallbacks are readable on the light theme — `"#000000"` for
foregrounds, `"#ffffff"` for backgrounds, `"#cccccc"` for muted accents.
This convention is documented in `libs/markoff-live/CLAUDE.md`.

**TextEdit-property bindings:** `QQuickTextEdit::setSelectionColor` and
`setSelectedTextColor` call `update()` unconditionally, so the same
dependency-on-`theme` pattern propagates correctly to TextEdit-internal
selection rendering.

## Slot mapping table

For each site identified in §"Current state audit":

| File | Surface | Current | Target slot |
|---|---|---|---|
| `Main.qml` | ApplicationWindow background | (OS default) | `EditorBackground` |
| `UnifiedInlineTextDelegate.qml:89` | Blockquote left bar | `palette.highlight` op 0.6 | `Quote` |
| `UnifiedInlineTextDelegate.qml:97` | List-marker selection backdrop | `palette.highlight` | `SelectionBackground` |
| `UnifiedInlineTextDelegate.qml:118` | List marker text (normal) | `palette.text` | `themeColorFor(markerSlot)` (`TextDefault`) |
| `UnifiedInlineTextDelegate.qml:118` | List marker text (selected) | `palette.highlightedText` | `EditorBackground` (inverse contrast on selection bg) |
| `UnifiedInlineTextDelegate.qml:175` | TextEdit text color | `palette.text` | `themeColorFor(themeSlot)` — per-kind via existing dispatch |
| `UnifiedInlineTextDelegate.qml` (new) | TextEdit `selectionColor` | (default OS) | `SelectionBackground` |
| `UnifiedInlineTextDelegate.qml` (new) | TextEdit `selectedTextColor` | (default OS) | `EditorBackground` |
| `CodeBlockDelegate.qml:11` | Root background | `Qt.rgba(0, 0, 0, 0.05)` | `CodeBlockBackground` |
| `CodeBlockDelegate.qml:47, 166` | TextEdit text color | `palette.text` | `CodeBlock` |
| `CodeBlockDelegate.qml:147` | Language label | `palette.mid` | `Quote` |
| `CodeBlockDelegate.qml` (new) | TextEdit selection/selectedText | (default OS) | `SelectionBackground` / `EditorBackground` |
| `ImageDelegate.qml:46` | Placeholder background | `palette.alternateBase` | `CodeBlockBackground` |
| `ImageDelegate.qml:47, 51, 60` | Placeholder border + icon + muted | `palette.mid` | `Quote` |
| `ImageDelegate.qml:96` | Selected border | `palette.highlight` | `SelectionBackground` |
| `HorizontalRuleDelegate.qml:13` | Rule line, normal | `palette.mid` | `Quote` |
| `HorizontalRuleDelegate.qml:13` | Rule line, selected | `palette.highlight` | `SelectionBackground` |
| `HorizontalRuleDelegate.qml:22` | Selected border | `palette.highlight` | `SelectionBackground` |
| `MathDelegate.qml:49` | Placeholder muted | `palette.mid` | `Quote` |
| `MathDelegate.qml:84` | Body text | `palette.text` | `TextDefault` (math-specific theming deferred to E5) |
| `MathDelegate.qml:101` | Selected border | `palette.highlight` | `SelectionBackground` |

Two slots are reused as multi-purpose accents per the no-new-slots
non-goal:

- **`Quote`** — blockquote accent + HR line + placeholder borders + muted
  secondary text. Intentional consolidation. If E3's callout work shows
  these conflate too aggressively, the slot is split in a separate spec.
- **`CodeBlockBackground`** — code-block root surface + image placeholder
  surface. Same rationale.

## Implementation phases

Five small phases. Each lands as one or two commits with `scripts/run-tests.sh`
green between them.

### P1 — API and registration

- Register `Markoff::Theme` Q_GADGET as a QML uncreatable type. Verify
  `Theme.EditorBackground` resolves in QML.
- Add `themeColorFor(int slot) -> QColor` Q_INVOKABLE to
  `LiveListModelBinding`. Implementation reads from the active theme
  buffer.
- Extend `tst_live_render_theme_toggle_propagation` with a
  `themeColorFor_reflects_active_buffer` slot — asserts the proxy returns
  `defaultLight().EditorBackground` initially and `defaultDark().EditorBackground`
  after `applyDefaultTheme(true)`.
- Falsifiability: revert the buffer-alternation in setTheme → the new test
  must fail.

### P2 — Window background and TextEdit selection colors

- `Main.qml` ApplicationWindow.color binds to `EditorBackground`.
- Every text-bearing delegate's TextEdit gains `selectionColor:` and
  `selectedTextColor:` bindings per §"Slot mapping table."
- QML integration test: `dark_toggle_inverts_window_background` — load doc,
  capture window `color` property, trigger `applyDefaultTheme(true)`,
  assert the QColor differs and matches `defaultDark.EditorBackground`.

### P3 — UnifiedInlineTextDelegate color migration

- Migrate sites at lines 89, 97, 118, 175 per §"Slot mapping table."
- Per-kind text color dispatch via the existing `themeSlot` property
  (already used for fonts).
- QML integration test: `dark_toggle_changes_textedit_color_per_kind` —
  load a doc with paragraph + heading + blockquote + list-item, capture
  each delegate's TextEdit `color`, toggle, assert all four colors
  changed and match the expected slot values.

### P4 — CodeBlock, Image, Math, HR delegates

- Migrate each remaining delegate's color sites.
- QML integration test per delegate type.

### P5 — Cleanup and convention

- Sweep: `grep -rn 'palette\.' libs/markoff-live/qml/` — every remaining
  reference is reviewed and either replaced or explicitly documented as
  intentional (e.g., text-cursor cosmetics where the OS-driven blink color
  is desired).
- Update `libs/markoff-live/CLAUDE.md` with the color-binding convention
  (the `(root.liveBinding && root.liveBinding.theme) ? proxy : fallback`
  pattern, the fallback-color rule, the "no `palette.*` for editor colors"
  rule).

## Testing strategy

### Unit-level

- `tst_live_render_theme_toggle_propagation::themeColorFor_reflects_active_buffer`
  (P1). Pins that `themeColorFor` reads from the active buffer.
- The existing three slots in this binary stay green throughout the spec's
  work.

### QML integration-level (new tests in `tst_live_render_qml_integration`)

- `dark_toggle_inverts_window_background` (P2).
- `dark_toggle_changes_textedit_color_per_kind` (P3) — parameterized via
  `QTest::addColumn` over paragraph / H1 / blockquote / list-item.
- `dark_toggle_changes_codeblock_background` (P4).
- `dark_toggle_changes_hr_color` (P4).
- `dark_toggle_changes_image_placeholder_colors` (P4).

### Falsifiability proofs

For each new integration test, prove falsifiability by reverting the target
delegate's color binding to its old `palette.*` form in a throwaway commit,
running the test, confirming it fails, then reverting the revert. The test
binary must be 100% green pre- and post-falsifiability stub.

The discipline is logged because the seam is *adjacent* to the
focus/caret/block-change seam (delegate QML files); even though INVARIANTS
§"Scope and exceptions" doesn't formally require it, the falsifiability
pass costs nothing and the next agent who reads `git log` benefits.

## Risks

- **R1 — `selectedTextColor = EditorBackground` low-contrast on light theme.**
  The light theme's `SelectionBackground` is `#b0d0ff` (pale blue);
  `EditorBackground` is `#ffffff`. White text on pale blue has poor
  contrast. If dogfood surfaces this, switch `selectedTextColor` to
  `TextDefault` (which gives dark text on the pale-blue selection bg). The
  dark-theme case has high contrast already (`EditorBackground` = `#1e1e1e`,
  `SelectionBackground` = `#264070`). Flag in P2's commit message; revisit
  on re-dogfood.
- **R2 — `Quote` slot doing too much work.** Listed in §"Slot mapping
  table" — accepted consolidation per non-goal. Watch for unintended
  side effects in E3 (callout coloring will want secondary accents).
- **R3 — `qmlRegisterUncreatableMetaObject` with Q_GADGET.** Per Qt docs
  this works for Q_GADGETs since Qt 5.8; verified in Qt 6.x via a P1 smoke
  test. If it doesn't, fall back to a separate `MarkoffLiveSlot`
  Q_OBJECT enum re-export — adds one file, doesn't block the spec.
- **R4 — TextEdit selection rendering on Linux + offscreen.** The offscreen
  QPA platform may not fully render selection backdrops in integration
  tests; the assertion is on the bound *property* value, not on rendered
  pixels, so this isn't a test problem — but worth keeping in mind during
  manual dogfood.

## Definition of done

- All 19 sites in §"Slot mapping table" wired through `themeColorFor`.
- Zero `palette.*` references for color (text-cursor cosmetic palette use
  documented if any remain).
- 4 new QML integration tests pass + falsifiability proofs verified.
- `libs/markoff-live/CLAUDE.md` convention section updated.
- E2.6 dogfood re-pass: Ctrl+Shift+D visibly inverts the editor; Ctrl+wheel
  still works; fonts still scale; nothing regressed.
- Tag `v0.7.0-e2.6` releases when re-dogfood signs off.

## Companion plan

Implementation plan to follow at
`docs/plans/2026-05-17-theme-color-wiring.md` (one phase per §"Implementation
phases" entry, TDD per task, build cap `-j 8` always).
