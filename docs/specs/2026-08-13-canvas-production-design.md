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

### 4.5 Inline object replacement (Qt ≥ 6.12 — the sanctioned path)

Investigated 2026-08-14 (findings log, same date) after P5.3 logged
"inline `$...$` is styled text, not glyphs" as a permanent gap. It is
not permanent. **Standalone `QTextLayout` supports inline objects
from Qt 6.12** — qtbase commit `be73ca50a34` ("QTextLayout: Support
inline objects for standalone layouts", merged 2026-05-12, in
v6.12.0-beta1; closes QTBUG-112717 et al.). Mechanism, normative for
this leaf when the gate below opens:

- The layout text carries `QChar::ObjectReplacementCharacter`
  (U+FFFC) at the object position; the same `setFormats()` call
  `rebuildInline()` already makes applies a **`QTextImageFormat`**
  with explicit `width`/`height` (+ `verticalAlignment`) over that
  one QChar. `QTextEngine` then itemizes it as an Object item and
  reserves exactly that box in wrapping, line height, `cursorToX`,
  and eliding. **No `QTextDocument`, no handler registration — C3 is
  not touched.** `QTextImageFormat` is a plain `QTextCharFormat`
  subclass; "image" names only the geometry carrier, not the content.
- Qt does **not** paint the object. `View::paintEvent` paints
  whatever the span means (jkqtmathtext pixmap, image, video frame,
  tag pill …) at the reserved rect (`lineForTextPosition` +
  `cursorToX`) — the identical paint-time-substitution pattern P5.3/
  P5.4 already use at block level, moved to span level.
- The U+FFFC substitution is a **projection-map omission** like any
  other (§4.2): buffer bytes of the span map to one layout QChar when
  the caret is outside the span, to raw source when inside. Caret
  motion, hit-testing, and selection fall out of the projection;
  nothing new crosses the layout boundary.
- **Gate:** `QT_VERSION_CHECK(6, 12, 0)`. Below it, keep the current
  styled-text fallback (P5.3). **Do not** build a pre-6.12 shim
  (letter-spacing width reservation à la Kate's InlineNoteProvider) —
  rejected 2026-08-14 as exactly the hack genre this arc exists to
  end. The gated task in the plan carries the implementation steps.

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
| Editing-command floor (F1 #1, P7.2b) | CodeMirror `defaultKeymap` subset Obsidian binds: word-wise motion + selection (Ctrl+Left/Right, Ctrl+Shift+Left/Right — `QTextBoundaryFinder`, not hand-rolled), word-wise delete (Ctrl+Backspace/Delete), document start/end (Ctrl+Home/End, Ctrl+Shift+Home/End extends), delete-line (Ctrl+Shift+K — clears the block's content, does not remove the block), move-line up/down (Alt+Up/Alt+Down — a content swap between adjacent `BlockId`s; core has no IdList reorder primitive), select-line (Alt+L), Esc simplifies an active selection to a bare caret (skipped while IME composing) |

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
files; middle-click paste (X11 primary selection); **auto-pairing /
wrap-selection** (P7.2c, F1 #4) — typing `(`, `[`, `"`, `` ` ``, or the
Markdown-specific bold marker `**` with an active single-block
selection wraps it (the wrapped text stays selected, matching
CodeMirror); the same keys with no selection auto-insert both halves
of the pair, caret between; typing the matching closer immediately
after types through instead of inserting a redundant one; Backspace
immediately between a pair auto-inserted by the PRECEDING keystroke
deletes both halves as one op (a manually-typed/pasted `()` does
neither). Freshness is tracked as view-local state
(`View::m_autoPairedClose`, valid for exactly one keystroke) — see
that field's own doc comment for the spec §2 view-state justification.
Multi-block-selection wrap is a logged scope cut (falls through to
plain replace); only these 5 pairs are in scope, no `{}`/`''`. **Highlight
other occurrences of the current selection** (P7.2e, F1 #7) — a
non-trivial, non-whitespace-only selection (min length 2, case-
sensitive, no whole-word requirement — `View::recomputeOccurrenceHighlights`'s
own doc comment, matching CM `highlightSelectionMatches`' defaults)
gets every OTHER exact-text occurrence within the realized entries
painted with `Theme::Slot::SelectionOccurrenceBackground`, distinct
from both the active selection and find-match highlights; the active
selection's own span is never counted as an occurrence of itself.
**Scroll past end, empty-document placeholder, bracket-match
highlight, drag drop-cursor indicator** (P7.2f, F1 #8/#10) — 4 small,
independent additions. Scroll-past-end: `updateScrollRange()` adds
bottom padding (viewport height minus one line) once the document
already needs scrolling, so the last line can reach the top of the
viewport instead of stopping the moment it merely becomes visible at
the bottom edge. Empty-document placeholder: `tr("Start typing…")`
painted in the reused `Theme::Slot::Quote` when the document has no
blocks or exactly one empty Paragraph block, gated on document
emptiness alone — **not** view focus (CM's own `placeholder.ts` gates
purely on document length; confirmed by reading it). Bracket-match
highlight: the caret-adjacent bracket and its nesting-aware match
(scoped to `()`/`[]`, one block only per C4) painted in the new
`Theme::Slot::BracketMatchBackground`. Drag drop-cursor indicator: a
dashed vertical bar in `Theme::Slot::CursorPrimary`, hit-tested fresh
on every `dragEnterEvent`/`dragMoveEvent`, cleared on
`dragLeaveEvent`/`dropEvent`. **Invisible/control-character rendering**
(P7.2g, F1 #9, last of the 7 F1 gap-closure sub-tasks) — C0 controls
(excluding `\t`/`\n`) + DEL get their Unicode Control Picture glyph
(U+2400+code, DEL→U+2421); C1 controls, soft hyphen, ZWSP, LRM/RLM,
BOM, and the safety-relevant bidi override/isolate controls
(U+202D/E, U+2066–9) get a Private-Use sentinel (U+E000) substituted
1 QChar for 1 QChar at layout-build time, with a hex-labeled box
painted over its (transparent) glyph slot in
`Theme::Slot::InvisibleCharBox`. The bidi subset is **neutralized at
the substitution point**, not merely covered with paint — U+E000
defaults to bidi class L (strong LTR), so the dangerous character
never reaches `QTextLayout`'s own bidi algorithm.

**Explicitly deferred out of this arc** (recorded so absence is a
decision): vim mode, PDF export/printing, embedded-note transclusion
rendering (embed *seam* only), RTL/bidi *testing* beyond what
QTextLayout gives for free, smooth/kinetic scrolling polish.

**Local multi-cursor editing** — deferred to its own arc and its own
spec by user decision (2026-08-13), *after* a feasibility check rather
than on assumption. Core already models it (`Selection::Kind::Secondary`
is distinct from `Kind::Presence`; `Session` stores and persists both)
and per-caret survivability rides the block-scoped `TextAnchor` round
trip that P6.1 builds anyway. Nothing in P1–P7 blocks it; three tasks
carry a "multi-cursor readiness" constraint so this arc does not write
itself into a corner — see the plan's finding **F1a** for the evidence
table, the canvas-side cost, and the two accepted limitations (IME
preedit is primary-caret-only; undo coalescing breaks across blocks).
Per invariant 3 the retirement of that deferral is written when the arc
opens, not now.

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
