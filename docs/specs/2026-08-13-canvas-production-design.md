# markoff-canvas production design — D5 arc, part 1 (feature-complete leaf)

**Date:** 2026-08-13
**Status:** ACTIVE — this is the normative spec for the canvas
production arc.
**Supersedes:** nothing. The spike spec
([`2026-08-13-markoff-canvas-spike-design.md`](2026-08-13-markoff-canvas-spike-design.md))
stays on file as the record of the PASS verdict; its findings (§9) and
the queue's #18 are absorbed here as requirements.
**Decision record:** [`2026-08-13-view-authority-direction-decision.md`](2026-08-13-view-authority-direction-decision.md)
— the pass consequence (§6, row 1) is now being exercised by explicit
user instruction (2026-08-13): build the canvas leaf out to feature
parity. The **contingent retirement of markoff-live/styled remains a
user decision** taken at this arc's final gate (§8), not before.
**Plan:** [`../plans/2026-08-13-canvas-production-plan.md`](../plans/2026-08-13-canvas-production-plan.md)

---

## 1. Goal and non-goals

**Goal.** Take `libs/markoff-canvas` from spike skeleton to the
feature-complete primary editing leaf for Corbomite's LivePreview
mode: full `MarkdownView` contract-v2 conformance, parity with what
the live/styled leaves give Corbomite today, parity "within reason"
with Obsidian's Live Preview (the product benchmark), and first-class
collaboration surface (multi-cursor rendering over `Session` +
collabtext presence) — the thing the projection architecture was
chosen for.

**Non-goals for this arc:**

- Retiring markoff-live/styled. Prepared for, gated at §8, decided by
  the user.
- Touching `markoff-source` (stays indefinitely, decision record §5.1).
- collabtext transport/sync work. The leaf renders what `Session` and
  the presence surface hand it; wire-level sync lives in collabtext.
- A public API freeze (still deferred per the 2026-05-07 pivot doc §4.6).

## 2. Authority model (unchanged, restated once)

Everything the spike spec §2 decided carries forward verbatim: the
document wins totally; the view's layouts are a derived cache; caret =
`{BlockId, byteOffset}` (+ optional anchor); undo = `UndoLog`; kind =
document via inference. Two bounded view-state exceptions are on
record (`m_selectionAnchor`, `m_preeditText` — T5/T8 findings) and any
new one must be justified the same way in its task's findings entry.

## 3. Constitution — promoted from spike gate to permanent law

C1–C4 are **permanent** for `libs/markoff-canvas/`.
`tests/check-constitution.sh` stays in CI forever; the honest-review
half (a renamed guard passes grep) becomes part of every phase-close
audit.

**C4 amendment (the one substantive change).** The delimiter-reflow
requirement (§5.1) introduces a per-entry **projection map**: the
layout string omits delimiter runs, so the leaf maintains, per
realized block (and per table cell), a kept-run list mapping
`layout QChar index ↔ buffer byte offset`. Ruling: this is
**layout-local presentation state, not a document coordinate space**
— it never leaves the layout boundary, never sums across blocks, and
is rebuilt from `blockText()` + spans on every realize/restyle. C4
still forbids: flat/global byte offsets, cross-block byte arithmetic,
`applyFlatEdit`/`flatView`/`widgetFlatView` callers. The projection
map is the *only* sanctioned second index space, and `Coordinates`/
`ProjectionMap` must remain the only files that construct one.

## 4. Architecture deltas

### 4.1 EditorWidget wrapper (contract seam)

The spike's `Markoff::Canvas::View : QAbstractScrollArea` stays the
rendering/input engine. A new
`Markoff::Canvas::EditorWidget : Markoff::MarkdownView` wraps it —
the same shape as `Markoff::Live::EditorWidget` wrapping the QML view
— and implements the full contract-v2 table (see the live leaf's
CLAUDE.md §"Public surface" for the reference semantics, including
the attach-window contract). Corbomite's `NoteEditorWidget` swaps
leaves by `MarkdownView*`; this wrapper is the adoption seam and its
conformance is tested by the same `ViewContractChecks.h` harness the
other leaves use.

### 4.2 Projection map (delimiter reflow — queue #18.1)

Delimiter hiding becomes **omission from the layout string**, not
background-colored glyphs. Consequences, all decided here:

- `restyleInline()` changes contract: a caret move that changes
  delimiter visibility is a *layout text* change → full per-block
  rebuild (formats + lines) — cheap, one block. The T7 finding's
  "formats-then-lines is atomic" rule continues to hold and now
  covers text too.
- Mapping a byte inside an omitted run is undefined by construction.
  **Snap rule:** any byte→layout query landing inside an omitted run
  snaps to the *nearest kept boundary in the direction of the
  operation* (selection start snaps left, selection end snaps right,
  caret queries snap toward the caret's motion direction; when no
  direction exists, snap left). The caret's own block always reveals
  the caret's span, so the caret itself never needs the snap.
- All three spike delimiter classes (emphasis/strong, ATX `#` prefix,
  code fences) plus the new inline kinds (§5.2: link URLs, wikilink
  brackets, highlight/strike markers, footnote-ref brackets) route
  through the same `SourceSpan::isDelimiter`-driven omission — one
  mechanism, no per-kind special cases.
- Table cells get their own per-cell projection maps (prerequisite
  for #18.2, selection across tables).

### 4.3 Core promotions (standstill lift, named seams only)

The arc opens `markoff-core` for exactly the debts the spike verdict
listed; everything else in core stays bug-fix-only:

1. `KindTransition::inferBlockKind` → core, **with the heading/setext
   level carried in the return** (fixes #18.3 in the same move); both
   live and canvas consume the one copy.
2. `Coordinates` byte↔QChar helper → core (pure, leaf-agnostic).
3. `Theme::color()` undefined-slot fallback: return an *invalid*
   color for background-class slots instead of `TextDefault` (the
   black-on-black trap, T1 finding), and define the missing slots.
4. `loadFromMarkdown` marker-convention asymmetry: **decision — the
   canvas convention ("buffer keeps the matched marker" for
   Heading/CodeBlock; ListItem/BlockQuote stay content-narrowed) is
   canonized as documented core behavior.** The fix is documentation
   + the `listItemDisplayMarker()` doc-comment correction, not a
   buffer-format migration; a migration would churn every leaf for
   zero user-visible gain.

### 4.4 Collaboration rendering (D5 core premise)

- Local caret/selection pushed **to** `Session::setPrimarySelection`
  (anchor-typed, via `textAnchorAt`) on every caret/selection change;
  re-resolved from the Session after model changes. This closes the
  guide's B.2/B.4 partials for this leaf natively.
- Remote participants arrive as `Session::secondarySelections()` (+
  identity metadata from collabtext's `PresenceManager`/
  `IdentityProjector`, delivered by the consumer — the leaf takes a
  `QList<RemotePresence>{selection, displayName, color}` surface and
  paints carets/selection tints + name flags; it does not touch
  presence files or transport).
- A remote edit arriving mid-IME-composition must not clobber the
  composition (the audit-L7 known failure of the old stack): the
  preedit lives only in the layout, the remote op lands in the CRDT,
  and the next restyle re-splices preedit over the new text. This is
  the single most important falsifiable test of the collab phase.

## 5. Feature scope (the parity contract)

### 5.1 Already built (spike) — carried forward

Typing/IME/undo/selection/clipboard/kind-promotion/lazy layout/
light-dark theme/minimal table, per exit criteria E1–E10.

### 5.2 Parity floor — what Corbomite consumes from the old leaves today

From `NoteEditorWidget` + the live leaf's contract table; all
**required**:

| Area | Content |
|---|---|
| Contract v2 | `setDocument` (+Session auto-create), `cursorPosition`/`setCursorPosition` (flat visual line), `scrollPositionVisualLine`± , `setReadOnly` + six-gate semantics, `hasCursor/hasEditing`, `caretRect` (completion popups), attach/detach `FindController`, `undo/redo`, `theme`/`fontScale`, format verbs, `contextChanged`/`cursorPositionChanged`/`scrollPositionChanged` signals |
| Find | match highlighting (all + current), navigate-to-match scroll, replace via consumer |
| Format verbs | bold/italic/strike/inline-code toggles, `insertLink`, `setHeadingLevel(0–6)` — implemented over core `FormatOps`, exposed as QActions (`CanvasActionController`, mirror of `LiveActionController`) so Corbomite can bind KF6 shortcuts/KActions |
| Links | render `[text](url)` with URL hidden (delimiter omission), wikilinks, tags; Ctrl+click / click activation → `LinkService::linkActivated`; hover signal for `HoverPopover` |
| Inline kinds | the full 8-kind set the live highlighter renders: bold, italic, strikethrough, code, highlight (`==`), link, wikilink, tag — plus footnote refs |
| Context menu | standard edit menu (cut/copy/paste/select-all + format section), extensible by consumer |
| Ephemeral state | save/restore scroll + cursor + fold as JSON (Corbomite `EphemeralState` round-trip) |
| Word wrap | adjustable content-column policy: full-width or fixed readable line length, centered (Obsidian "readable line length"); live-resizable. Obsidian calibration (F1): `--file-line-width: 700px` default, centered, user-toggleable; full viewport width when off; tables and code blocks may exceed the column with their own horizontal scroll |
| Inline title | **optional** leading title band showing the file name (Obsidian's `.inline-title`), editable, edits reported to the consumer for the rename. It is **not a document block** — Obsidian writes no H1 and neither do we. It is therefore excluded from `cursorPosition()` flat-line coordinates, find, selection-copy and serialization, and participates in caret motion only at the seam: Down/Enter from the title enters block 0; Backspace at document start does not consume it. Shares the content column. User-directed 2026-08-13; off by default |
| Mermaid/math/images | injection seams: `MermaidRenderer`, jkqtmathtext for Math blocks, image blocks via consumer resource provider |

### 5.3 Obsidian Live Preview parity — within reason

Benchmark features to match (audit task **F1** verified this list
against the `~/src/codemirror` checkout on 2026-08-13; its gap table and
proposed edits to this section are in the plan's findings log, awaiting
the user's decision — in particular an **editing-command floor** and
**local multi-cursor**, neither of which this section currently names):
checkbox rendering + click-to-toggle for task lists; ordered/
bullet list markers styled; code-block syntax highlighting
(`Kf6SyntaxHighlightService`, already in core); blockquote styling
(done) + callout rendering (type/icon/fold header); horizontal rules;
inline + display math; footnote refs superscripted; frontmatter
rendered as a properties-style header block (read-only in-canvas is
acceptable); full table editing (in-cell wrap, Tab/arrow cell nav,
row/col insert/delete via context menu, alignment); heading/list/
callout **folding** (`Session::foldedRegions`); drag-drop text +
files; middle-click paste (X11 primary selection).

**Explicitly deferred out of this arc** (recorded so absence is a
decision): vim mode, PDF export/printing, embedded-note transclusion
rendering (embed *seam* only), RTL/bidi *testing* beyond what
QTextLayout gives for free, smooth/kinetic scrolling polish.

### 5.4 Accessibility

`QAccessibleTextInterface` remains the largest unpriced item (spike
§5). It is **its own phase with its own user gate** (§8): before P7
starts, the user decides scope (full text-interface vs. basic
focus/name/role). The plan carries the phase; nothing else depends on
it, so it can move without reordering the arc.

## 6. Testing and discipline

- Every functional task keeps the spike's falsification protocol
  (plant/revert, SHAs recorded in the plan checklist). Perf and audit
  tasks are exempt per the T10/T11 precedent.
- Baseline ratchets: 288/288 today; each task raises the count, never
  lowers it. Full-suite green before every commit.
- Perf: E9's four budgets re-run at every phase close against
  `build-perf` (RelWithDebInfo); a projection-map or highlighting
  regression that blows the 16 ms p95 keystroke budget blocks the
  phase.
- Contract conformance: the canvas `EditorWidget` enrolls in the
  shared `ViewContractChecks.h` suite alongside live/styled/source.
- Offscreen only (`scripts/run-tests.sh`); `MARKOFF_CANVAS_GRAB`
  stays the eyeball loop; `--direct` needs per-task user permission.

## 7. Standstill status after this spec lands

- `libs/markoff-canvas/` — **active workfront.**
- `libs/markoff-core/` — open **only** for the §4.3 promotions and
  API seams the plan names task-by-task; anything else is a finding,
  not a fix.
- `libs/markoff-live/`, `libs/markoff-styled/` — bug-fix-only until
  the §8 retirement gate.
- `libs/markoff-source/` — untouched, permanent.

## 8. Gates (user decisions, in plan order)

| Gate | When | Question |
|---|---|---|
| G1 — a11y scope | before P7 | full `QAccessibleTextInterface` vs. basic role/name |
| G2 — Corbomite adoption | after P6 | flip Corbomite LivePreview to canvas (behind a setting first) |
| G3 — retirement | after G2 dogfood | retire markoff-live; fold markoff-styled's Reading mode into canvas read-only, or keep styled |

G3 is the decision-record §5.3 contingency; its retirement plan is
written only when the gate opens, per invariant 3, inside a successor
spec that cites this one.
