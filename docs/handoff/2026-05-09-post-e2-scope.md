# Post-E2 scope — editing affordances, theming wire-up, speculative paths

**Date:** 2026-05-09
**Status:** Scope capture only — no specs drafted yet. Three new E-arc phases
proposed (E2.5 / E2.6 / E2.7) to land between E2 and E3.
**Authoritative for:** what work is queued. Spec/plan docs come later.

This document exists because the E-arc framing (E1–E6) covered the
"render maximalism" axis but did not cover editing affordances, theming
wire-up, or zoom — and the retired live-editing plan
(`docs/archive/...` / `docs/plans/2026-04-30-live-editing.md`) had a
chunk of obviously-needed work that never made it into the foundation
rebuild. Captured here so we don't lose the thread again.

---

## 1. The audit (2026-05-09, after E2 dogfood pass)

### 1.1 Items from the retired live-editing plan that never landed in the foundation

| Plan task | Status in foundation | Why it matters |
|---|---|---|
| Task 10 — `LiveClipboardController`: Cut, Copy, Paste of Markdown source | **Missing.** Today's commit `94056f9` patched cross-block *copy* (off-screen rows were being lost because QML walked `ListView.itemAtIndex`). No cut. No paste path at all. No clipboard MIME-type discipline. No multi-block paste structure preservation. | Dogfood-blocking. Users cannot move text between documents or paste anything. |
| Task 10 — Right-click menu Cut/Copy/Paste | **Missing.** `LiveContextMenu.qml` exists but is undo/redo only. | UX expectation. |
| QAction surface for the editor | **Missing.** No action collection. `Ctrl+C` is wired ad-hoc in `LiveView.qml`'s `Keys.onPressed`. | Required for menu bars, toolbars, and consistent shortcut handling across host apps. |
| Task 8 — `LiveSpeculativeFenceController`: type ` ``` ` and immediately get a code-block delegate | **Missing.** Today the parser converts the kind via `inferBlockKind` after the d2 cycle. Visible lag. | Editor-feel polish. Dogfood will tell us how badly the lag bothers users; lower priority than clipboard. |
| Task 9 — Speculative inline open-delimiter styling: type `**` and immediately render bold on the run-up to the closing `**` | **Missing.** | Same family as Task 8. |
| Task 13 — HR / Image click routing to neighbour text delegate | **Partial.** Click selects the HR/Image (`BlockSelected`); does not route to a neighbour for typing. | Inconsistent with how other read-only blocks should behave. |
| Task 14 — Save / dirty / window title | **Missing.** `MarkoffDocument::d2EditSequence()` is exposed for dirty tracking, but no consumer wires it to the test app's window title or to a save action. | Dogfood-blocking. App is effectively read-once-edit-don't-save. |
| Task 15 — Empty-doc focus + first-keystroke materialisation | **Untested.** Probably has edge cases. | Regression-prone area. |
| Format toggles (Ctrl+B / Ctrl+I / Ctrl+K) | **Out of scope in old plan, still missing.** | Word-processor table-stakes for any markdown editor. |
| Task 12 — IME composition handling | **Done.** `LiveEditBinding::composing` + `flushPendingComposition` is wired through delegates. | — |
| Cross-view session state (selection, scroll, folds) — Source ↔ Live | **Half-wired — corrected 2026-05-09 per user pushback.** `Markoff::Session` in core has the full shape: `primarySelection` (TextAnchor-typed, CRDT-stable), `secondarySelections`, `setTopVisible(anchor, fraction)` for scroll, `foldedRegions`, `primarySelectionChanged` signal. Source side **bidirectional** — `SourceTextDocumentBinding` reads + writes. Live side **unidirectional** — `LiveSelectionView::setSession` + `syncToSession` writes only; no subscription to `primarySelectionChanged`. No app harness creates a Session; `markoff-live-app` never instantiates one. Scroll/folds: Session supports them; no view pushes or pulls. | Dogfood-friction depends on whether anyone runs Source + Live concurrently. Folded into **E2.5** below — Live-side `primarySelectionChanged` subscription is small; the test-app harness work pairs naturally with save/dirty wiring. |

### 1.2 Theming — half-wired

`Markoff::Theme` (in `markoff-core`) has the right shape:
- Per-slot colors (`BoldEmphasis`, `ItalicEmphasis`, `InlineCode`, `Highlight`, `Link`, `WikiLink`, `Tag`, `HiddenMarker`, `CodeBlockBackground`, …).
- Three font roles (`Body`, `Monospace`, `Heading`).
- Per-slot `bold` / `italic` / `fontSizeMultiplier`.
- `defaultLight()` / `defaultDark()` factories.
- JSON I/O via `toJson` / `fromJson`.

What's actually wired:

- `Markoff::Live::InlineHighlighter::formatFor(span)` uses `Theme::charFormat`, `Theme::color`, and `Theme::font(Monospace)` for code spans. ✓
- `LiveListModelBinding::theme()` is exposed to QML and reaches `InlineHighlighterAttached`. ✓

What's NOT wired:

- **Every delegate hardcodes its base font size and family.**
  - `ParagraphDelegate.qml:38` → `font.pixelSize: 14`.
  - `BlockquoteDelegate.qml:41` → `font.pixelSize: 14; font.italic: true`.
  - `CodeBlockDelegate.qml:38–39` → `font.family: "monospace"; font.pixelSize: 13`.
  - `MathDelegate.qml:42–43` and `:73–74` → `font.family: "monospace"; font.pixelSize: 13`.
  - `ListItemDelegate.qml:60, :84` → `font.pixelSize: 14`.
  - `HeadingDelegate.qml:38–43` → switch on `headingLevel` returning literal pixel sizes 28 / 24 / 20 / 18 / 16 / 14, no Theme reference.
- Theme's `Body` / `Heading` font roles are read in **zero** QML files.
- Theme's `fontSizeMultiplier(Slot)` getter is **defined but never called by any consumer** — the per-slot size scaling never happens.
- No theme-switching UI. No way to load a custom theme at runtime. No way to even toggle light/dark.

Net effect today: the visual presentation is whatever the QML hardcodes
say it is; the `Markoff::Theme` object is, in practice, an inline-format
styling decoration only.

### 1.3 Zoom — zero infrastructure

No `zoom` / `scale` / `fontScale` property anywhere in `markoff-live`. No
`Ctrl+=` / `Ctrl+-` / `Ctrl+wheel` handlers. No pinch handlers. No way to
grow or shrink the rendered text. Adding this naturally couples with the
theme wire-up — both are "where do delegates get their effective font
sizes from" — so they should be done in the same phase.

---

## 2. Proposed phase split

Numbering uses `E2.5 / E2.6 / E2.7` so that E3+ as currently planned
(wikilinks/embeds/tags/callouts; tables/frontmatter/footnotes;
math/mermaid; distillation) keeps its numbering.

### E2.5 — Editing affordances pack

**Purpose:** Move the live editor from "you can type into it" to "you can
actually use it as an editor." Save/copy/paste are dogfood-blocking; the
others are table-stakes.

**Scope (subject to spec-time refinement):**

- `LiveClipboardController.{h,cpp}` — Cut, Copy, Paste of Markdown source bytes.
  - Single-block partial selection: trivial.
  - Cross-block selection: must route through `LiveBlockModel::recordAt` (model-driven, not delegate-driven — see commit `94056f9`).
  - Paste: routes through `MarkoffDocument::applyFlatEdit` so multi-block paste re-derives kinds via `inferBlockKind` automatically. No special-casing.
  - Clipboard MIME types: at minimum `text/plain`. `text/markdown` if/when we're sure of the content shape.
- QAction surface — single action collection per `LiveListModelBinding` (or sibling controller). Actions for: Cut, Copy, Paste, Undo, Redo, SelectAll, Delete. Exposed for menu bar, toolbar, and `Keys.onPressed` consumption.
- Right-click menu (`LiveContextMenu.qml`): Cut / Copy / Paste / separator / Undo / Redo / separator / "Undo in this block" (existing).
- Format toggles: Ctrl+B (bold), Ctrl+I (italic), Ctrl+K (link). Wraps selection with appropriate delimiters; idempotent; works against multi-block selections by per-block sub-application.
- Save / dirty / window title for `markoff-live-app`:
  - `Ctrl+S` triggers save; passes through any host-provided save callback.
  - Window title shows `*` (or the platform-native modified marker) when `d2EditSequence()` differs from last-saved sequence.
- Cross-view session state — Live side bidirectional:
  - `LiveSelectionView` subscribes to `Session::primarySelectionChanged` and applies remote-changed selections (clamps to text length, projects per-block ranges via existing `rangeForBlock` machinery).
  - `LiveListModelBinding::setSession()` exposed to QML so app code can hand a Session in. `markoff-live-app` instantiates a Session via `MarkoffDocument::createSession` and binds it.
  - Smoke test: two `LiveListModelBinding`s on the same doc + same Session → selection in instance A is observable in instance B. (No real Source widget needed for this test; the contract is Session-driven, view-agnostic.)
  - Scroll-position + folded-regions handoff explicitly **deferred** — folding isn't implemented in Live yet, and scroll-restore on view-swap isn't dogfood-blocking. Adds to §4 open questions if it bites in dogfood.
- Empty-doc focus + first-keystroke materialisation: explicit test pass; fix any edge cases that surface.

**Out of scope for E2.5 (deferred to E2.6 / E2.7):** theming, zoom, speculative paths.

**Risk surface:** paste of large multi-block markdown (1000+ lines) — needs
a perf gate; the d2 cycle was tuned for keystroke-rate edits, not bulk
inserts. May need batching or an `applyFlatEdit` fast path.

### E2.6 — Theme wire-up + zoom

**Purpose:** Route the half-wired `Markoff::Theme` all the way to the
delegates' base fonts, and add the user-controllable scale that no
existing infrastructure supports.

**Scope:**

- Every delegate's `font.family`, `font.pixelSize`, `font.bold`, `font.italic` derived from `Markoff::Theme` via `LiveListModelBinding::theme()`.
- Heading sizes computed as `theme.font(Heading).pointSizeF() * theme.fontSizeMultiplier(HeadingN)` — replace the literal `28 / 24 / 20 / 18 / 16 / 14` switch.
- `LiveListModelBinding::fontScale` Q_PROPERTY (default 1.0). Every delegate's effective pixel size = `themeSize * fontScale`.
- Bindings for `Ctrl+=` / `Ctrl+-` / `Ctrl+0` (reset) / `Ctrl+wheel`. Possibly pinch on touch.
- Theme-switch action: at least light/dark toggle. Custom theme load via `Theme::fromJson` — UI deferred or minimal.
- Round-trip tests: change theme → all delegates re-render with new fonts/colors; change `fontScale` → all delegates resize; reset to defaults restores baseline.

**Risk surface:** layout reflow during zoom — `ListView` row heights
re-derive from `implicitHeight = edit.implicitHeight`; need to verify
the ListView re-positions correctly without scroll-position loss.

### E2.7 — Speculative paths

**Purpose:** Reduce the visible parse-cycle lag for delimiter-typing
flows.

**Scope:**

- Speculative open-delimiter styling: type `**` and the styling for "between the asterisks" appears immediately, before the closing `**` arrives. Old plan Task 9.
- Speculative code-fence: type ` ``` ` on its own line and the block immediately renders as a CodeBlock delegate, before the parser confirms via `inferBlockKind`. Old plan Task 8.

**Lower priority than E2.5/E2.6.** Dogfood after E2.5/E2.6 will tell us
whether the lag is bothersome enough to justify the speculation
machinery.

**Risk surface:** speculation has to be reverted if the user types
something that breaks the prediction (e.g., `*` then space, never
closing). The old plan had a design for this; it'll need refresh.

---

## 3. Order and rough sizing

E2.5 → E2.6 → E2.7 → E3.

Rough sizings (will be refined at spec time):
- E2.5: ~3 weeks (largest; touches a lot of surface).
- E2.6: ~1 week.
- E2.7: ~1 week.

---

## 4. Open questions for spec time

These are decisions to defer until each phase enters brainstorm:

- **E2.5 paste path.** Does paste route through `applyFlatEdit` (single
  flat-text edit, kinds re-derived) or through a structure-aware
  per-block path (preserves kinds explicitly)? `applyFlatEdit` is
  simpler; structure-aware preserves things like a single-block paste's
  identity if user hits Ctrl+V into the same place they cut from. Spec
  time decides.
- **E2.5 format toggle behavior on multi-block selection.** Wrap each
  block's selection independently? Wrap the whole selection across
  blocks (which Markdown can't really represent)? Decide at spec time.
- **E2.6 theme-switch UI.** Menu item, settings dialog, hotkey, all of
  the above? Lower priority — start with a programmatic API + a single
  light/dark toggle action.
- **E2.6 fontScale persistence.** Does the scale survive across app
  restarts? If yes, where is it stored (`QSettings`)? Or is it a
  per-document setting (in frontmatter or a sidecar)?
- **E2.7 speculation revert.** When does a speculation get torn down,
  and does that trigger a paint flicker? Old plan had answers; need to
  re-derive in current architecture.

---

## 5. Cross-references

- E-arc framing: `docs/specs/2026-05-08-e-arc-framing.md`
- E-arc roadmap: `docs/e-arc/2026-05-08-e-arc-roadmap.md` (phase board updated 2026-05-09 to include E2.5/E2.6/E2.7).
- E-arc status: `docs/e-arc/e-arc-status.md` (phase board updated 2026-05-09).
- Retired live-editing plan: `docs/plans/2026-04-30-live-editing.md` — Tasks 8 / 9 / 10 / 13 / 14 / 15 are the source of E2.5–E2.7's scope.
- Today's clipboard fix commit: `94056f9` — model-driven copy.
- E-arc framing §5 invariant on subtractability: each E2.x deliverable must publish a subtractability note saying how a view that doesn't need cut/paste / zoom / speculation can avoid the runtime cost.

---

## 6. Update protocol

- When a phase enters brainstorm: add a "Spec status" line to its
  section here, link the spec doc.
- When a phase enters plan: link the plan doc.
- When a phase ships: mark complete in §2; copy a one-line entry to
  `docs/e-arc/e-arc-status.md`'s recent-changes log; do not edit the
  phase's section here (it's a frozen scope record).
- If new scope is discovered that should slot into one of these phases,
  amend the relevant §2.x bullet here AND log the amendment with date.
- If new scope is discovered that doesn't fit any of these phases, add
  a §2.x.1 / §2.x.2 etc. or create E2.8.
