# Plan — markoff-canvas production arc (D5 part 1)

**Spec (normative — read §2–§5 before Task P1.1):**
[`../specs/2026-08-13-canvas-production-design.md`](../specs/2026-08-13-canvas-production-design.md)
**Spike record (findings you will need; do not re-derive):**
[`../specs/2026-08-13-markoff-canvas-spike-design.md`](../specs/2026-08-13-markoff-canvas-spike-design.md) §9
**Contract reference:** `libs/markoff-live/CLAUDE.md` §"Public surface"
(the semantics every wrapper override must match) and
`docs/VIEW-IMPLEMENTORS-GUIDE.md` (§B especially).

This plan is written for **consecutive fresh agent sessions**. Each
task is sized for one session. Do the topmost unchecked task in the
current phase. Do not skip ahead across a phase boundary — phase-close
tasks (marked ⏸) and user gates (G1–G3) are hard stops.

---

## Session protocol (every session, every model)

**Start:**
1. Read this file top to bottom, then spec §3 (constitution) and §4
   (architecture deltas).
2. `git pull`, build, run the tests:
   ```bash
   cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
   cmake --build build-dev -j 4          # never more than -j 4
   scripts/run-tests.sh -R canvas
   ```
3. Confirm the previous task's checklist state matches reality. If it
   doesn't, fixing that IS your session.

**During:**
- Scope: `libs/markoff-canvas/` is open; `libs/markoff-core/` is open
  **only where your task explicitly names a core seam**; live/styled
  are bug-fix-only; source is untouched. Anything else you think you
  need: stop, log a finding, end the session.
- `libs/markoff-canvas/tests/check-constitution.sh` must pass before
  every commit. C1–C4 are permanent law now (spec §3). The projection
  map is the one sanctioned second index space; it lives only in
  `ProjectionMap`/`Coordinates` files.
- Falsification protocol for every functional test (unchanged from
  the spike): make it pass → plant a break in a throwaway commit →
  watch it fail → revert → record both SHAs. Perf/audit tasks exempt.
- Run the **full** suite (`scripts/run-tests.sh`) before commit. The
  baseline only ratchets up (288/288 at arc open).

**End:** tick the checkbox, fill SHAs, append surprises to the
**findings log at the bottom of this file** (one line minimum; the
spike spec §9 is closed). Commit `canvas(P<n>.<m>): <summary>`; core
promotion commits use `core(P1.<m>): <summary>`. Push.

**Decision rules:**
- Decide yourself + log: rendering details, class shape, test
  mechanics, anything invisible outside the leaf.
- Stop + log + end session: anything needing a C1–C4 violation, a
  core change your task didn't name, or a weakening of a done-when.
- Ask the user: scope changes, the G1–G3 gates, `--direct` runs.

---

## API cheat sheet — additions beyond the spike plan's

(The spike plan's cheat sheet still applies for document read/write,
`StructuralKeyHandler`, and the layout-boundary rule.)

```cpp
// Contract base:            <markoff/core/MarkdownView.h>   (virtuals + signals)
// Conformance harness:      libs/markoff-core/tests/ViewContractChecks.h
// Format verbs backend:     <markoff/core/FormatOps.h>
// Actions enum:             <markoff/core/ActionId.h>
// Find:                     <markoff/core/FindController.h> (matchesChanged,
//                           currentMatchChanged, navigationRequested(Match))
// Links:                    <markoff/core/LinkService.h>, DefaultLinkService,
//                           LinkActivation
// Code highlighting:        <markoff/core/Kf6SyntaxHighlightService.h>
// Session (collab/fold):    <markoff/core/Session.h> — primarySelection,
//                           secondarySelections, foldedRegions, toJson/fromJson
// Anchors:                  <markoff/core/TextAnchor.h>, BlockAnchor;
//                           doc.textAnchorAt / resolveTextAnchor
// Math:                     libs/jkqtmathtext (vendored)
// Presence (reference only): ~/dev/collabtext app/collabedit/CollabPane.*
//                           — the leaf NEVER links presence/transport
```

Reference implementations to read (never link, never copy wholesale):
`Markoff::Live::EditorWidget` (contract semantics),
`LiveActionController` (QAction shape), `InlineHighlighter` (8-kind
slot mapping), `qwidgettextcontrol.cpp` in `~/src/qtbase` (input edge
cases; license rule in the spike plan applies to any copied snippet).

---

## Task checklist

| Task | Done | Commit | Fals. |
|---|---|---|---|
| **P1 — core promotions & carried findings** | | | |
| P1.1 KindTransition → core, with heading level (#18.3, #18.4) | ☑ | `72f446e0` | `c1e21740` / `01734f3f` |
| P1.2 Coordinates byte↔QChar → core | ☐ | | |
| P1.3 Theme background-slot fallback + missing slots | ☐ | | |
| P1.4 Marker-convention canonization (docs + doc-comment) | ☐ | | |
| P1.5 ⏸ phase close: full suite + constitution + findings sweep | ☐ | | n/a |
| **P2 — projection map (delimiter reflow)** | | | |
| P2.1 ProjectionMap + omission for emphasis/strong | ☐ | | |
| P2.2 Omission for heading prefix + code fences | ☐ | | |
| P2.3 Per-cell maps + cross-table selection (#18.2) | ☐ | | |
| P2.4 ⏸ perf re-baseline (E9 budgets, build-perf) | ☐ | | n/a |
| **P3 — MarkdownView contract v2** | | | |
| P3.1 EditorWidget wrapper + setDocument/Session + contract harness | ☐ | | |
| P3.2 Cursor/scroll position mapping + signals | ☐ | | |
| P3.3 Read-only gates + caretRect | ☐ | | |
| P3.4 FindController: highlight + navigate | ☐ | | |
| P3.5 EditorContext + theme/fontScale through the wrapper | ☐ | | |
| P3.6 Ephemeral state JSON round-trip | ☐ | | |
| P3.7 ⏸ phase close | ☐ | | n/a |
| **P4 — inline/text parity** | | | |
| P4.1 Full inline kind set (highlight/strike/link/wikilink/tag/footnote-ref) | ☐ | | |
| P4.2 Link activation + hover | ☐ | | |
| P4.3 FormatOps verbs + CanvasActionController | ☐ | | |
| P4.4 Context menu | ☐ | | |
| P4.5 Readable-line-width policy + resize | ☐ | | |
| P4.6 Code-block syntax highlighting | ☐ | | |
| P4.7 Task-list checkboxes (render + toggle) | ☐ | | |
| P4.8 ⏸ phase close | ☐ | | n/a |
| **P5 — block parity** | | | |
| P5.1 Table: in-cell wrap + cell navigation | ☐ | | |
| P5.2 Table: row/col ops + alignment | ☐ | | |
| P5.3 Math blocks (jkqtmathtext) | ☐ | | |
| P5.4 Images + Mermaid/embed seams | ☐ | | |
| P5.5 Callouts + frontmatter + footnote defs | ☐ | | |
| P5.6 Folding via Session | ☐ | | |
| P5.7 ⏸ phase close | ☐ | | n/a |
| **P6 — collaboration surface** | | | |
| P6.1 Caret/selection ↔ Session (B.2/B.4 closure) | ☐ | | |
| P6.2 Remote presence rendering (carets, tints, flags) | ☐ | | |
| P6.3 Remote-edit-mid-IME + concurrency torture tests | ☐ | | |
| P6.4 ⏸ phase close | ☐ | | n/a |
| **G1 — user gate: accessibility scope** | ☐ | — | — |
| **P7 — polish + a11y** | | | |
| P7.1 Accessibility (per G1) | ☐ | | |
| P7.2 Drag-drop + middle-click paste | ☐ | | |
| P7.3 ⏸ arc close: Obsidian parity audit + full audit | ☐ | | n/a |
| **G2 — user gate: Corbomite adoption** (work lands in Corbomite repo) | ☐ | — | — |
| **G3 — user gate: retirement decision** (successor spec) | ☐ | — | — |

Floating task (do in any session once `~/src/codemirror` exists):
**F1 — CodeMirror parity audit.** Diff spec §5.3's benchmark list
against `@codemirror/*` + Obsidian-observable behavior; append
gaps/corrections to the findings log; propose spec §5.3 edits rather
than silently implementing extras.

---

## Phase 1 — core promotions & carried findings

Core is open ONLY for the seams named below. Each task also updates
`docs/VIEW-IMPLEMENTORS-GUIDE.md` / lib CLAUDE.md lines it invalidates.

### P1.1 — KindTransition → core, with level
Move the inference rules into `markoff-core` (new
`<markoff/core/KindInference.h>`; suggested return
`struct KindInference { BlockKind kind; int headingLevel; bool mathDisplay; }`).
Port live's string-keyed consumption via a thin adapter inside
markoff-live (bug-fix-budget-sized; if it balloons, stop and log).
Canvas: `promoteCaretBlockKind()` sets the `level` attr in the same
`UndoLog::Transaction` as the kind (typed `### x` renders H3; setext
`=`/`-` gives 1/2). Wire `mathDisplay` to `d2SetBlockAttr` (T6
finding). Delete both leaf-local copies.
**Tests:** canvas kind-transition test grows: `##`–`######`, setext
via Shift+Enter (#18.4), math display attr. Live suite must stay
green untouched.
**Falsify:** drop the level write; heading-level assertions must fail.

### P1.2 — Coordinates → core
Move the byte↔QChar helper to
`<markoff/core/TextUnits.h>` (pure functions, no Qt-widget deps);
live + canvas consume it; delete both copies. No behavior change —
the existing tests are the net.
**Falsify:** off-by-one the promoted helper in a throwaway; canvas
typing + live coords tests must both fail.

### P1.3 — Theme fallback
`Theme::color()` on an undefined slot: background-class slots return
an invalid `QColor` (callers treat as "paint nothing") instead of
`TextDefault`; define `QuoteBackground` (+ any other slots the canvas
presentation reads) in `defaultLight()`/`defaultDark()`. Remove the
canvas-local `backgroundOrNone` workaround. Check styled/live for
accidental reliance on the old fallback before changing it — if any
call site depends on it, log and preserve via an explicit slot
definition, not the fallback.
**Falsify:** re-introduce the TextDefault fallback for
`QuoteBackground`; a new black-on-black render assertion must fail.

### P1.4 — Marker-convention canonization
Documentation task (no buffer migration — spec §4.3.4): correct
`listItemDisplayMarker()`'s doc comment, write the per-kind
buffer-convention table into `markoff-core/CLAUDE.md` and the
implementor's guide §1, citing the T1/T6 findings. No falsification
(docs).

### P1.5 ⏸ — phase close
Full suite green; constitution script + honest C1 read of files
touched this phase; findings log sweep; update `docs/STATUS.md`
baseline count.

---

## Phase 2 — projection map

Read the post-spike finding (spike spec §9 "Post-spike") in full
before P2.1 — the four decision points there are settled by spec §4.2;
implement, don't re-litigate.

### P2.1 — ProjectionMap + emphasis/strong omission
New `src/ProjectionMap.{h,cpp}`: built per realized entry from
`blockText()` + spans + reveal state; kept-run list; byte↔layoutQChar
both ways; snap rule per spec §4.2. `layoutTextFor` omits hidden
delimiter runs (`\n`→U+2028 substitution folds into the same pass).
Route every `View.cpp` conversion call site through the entry's map
(~20 sites — mechanical, total; grep for the old helper and leave no
direct caller). `restyleInline()` becomes a full per-block rebuild on
reveal-state change. Delete the background-color hiding path.
**Tests:** reflow is real: with caret outside `a **b** c`, layout
width < revealed width AND `lineAt(0).naturalTextWidth()` changes on
caret entry; Left/Right step over hidden runs in one press; selection
endpoint in a hidden run of a non-caret block snaps per rule;
existing E1–E8 tests all still green (they are the real net here).
**Falsify:** break the map's byte→QChar for blocks with one hidden
run; typing-after-span test must fail.

### P2.2 — heading prefix + code fences omitted
Same mechanism, two more delimiter classes: `# ` prefix hidden unless
caret in block (Obsidian behavior: heading text without markers);
fence lines + info string hidden likewise, code content keeps
monospace + background. Reveal granularity is per-block here (not
per-span) — matches Obsidian.
**Falsify:** pin heading-prefix visibility on; hide-assertions fail.

### P2.3 — table cells + cross-table selection (#18.2)
Per-cell ProjectionMaps. Give Table entries a **cell-ordered linear
position sequence** (row-major: cell 0 byte-range, cell 1, …) so
block-level selection/copy walks a table like any block: selection
crossing a table tints the covered cells; Ctrl+C serializes covered
cells pipe-separated (row-major, `\n` between rows). Caret motion
in/out of tables at top/bottom edges lands in the nearest cell.
**Falsify:** misorder the linear sequence; cross-table copy test fails.

### P2.4 ⏸ — perf re-baseline
Re-run `tst_canvas_perf_500` in `build-perf`; all four E9 budgets
must hold with projection active. Add a 200-keystroke run *inside a
formatted paragraph* (spans present, reveal toggling) — p95 < 16 ms.
Record numbers in the findings log. Over budget = phase does not
close; profile and fix before proceeding.

---

## Phase 3 — MarkdownView contract v2

### P3.1 — EditorWidget wrapper
`Markoff::Canvas::EditorWidget : MarkdownView` composing `View` (new
public header; `View` stays public for tests/demo). `setDocument`
auto-creates a `Session` (live's pattern: destroyed in the dtor,
detach on `setDocument(nullptr)` without touching content). Enroll in
`ViewContractChecks.h`; make the attach-window checks pass (write
issued in the same call stack as `setDocument` must not be clobbered
— the canvas has no QML seed race, so this should be free; assert it).
App/demo switches to constructing `EditorWidget`.
**Falsify:** re-order setDocument teardown to drop a queued caret
write; attach-window check fails.

### P3.2 — cursor/scroll mapping + signals
`cursorPosition()`/`setCursorPosition(CursorPos)`: flat visual line ↔
`{BlockId, byte}` walk (O(blocks), uncached — a cache is a second
cursor store). `scrollPositionVisualLine()`± over the scrollbar with
estimated-height correction; `cursorPositionChanged` /
`scrollPositionChanged` emissions. Out-of-range set clamps, never
no-ops.
**Falsify:** skip the clamp; contract check fails.

### P3.3 — read-only + caretRect
`setReadOnly` gates every mutation ingress in `View` (printables,
structural keys, IME commit, paste/cut, checkbox toggles, format
verbs via action enabled-state) while navigation/selection/copy/find
keep working — mirror the live leaf's six-gate table. `caretRect()`
in EditorWidget coordinates (completion popup anchor).
**Falsify:** leave the IME-commit gate open; read-only IME test fails.

### P3.4 — find integration
`attachFindController`: paint all-matches + current-match highlights
(draw-time `FormatRange`s like selection — never `setFormats`);
subscribe `matchesChanged`/`currentMatchChanged`;
`navigationRequested` scrolls the match visible + places the caret.
Detach cleans all paint state.
**Falsify:** ignore `currentMatchChanged`; navigate test fails.

### P3.5 — EditorContext + theme/fontScale
Emit `contextChanged` (kind, heading level, inTable/row/col) on caret
move + kind change, change-gated. `setTheme`/`setFontScale` through
the wrapper: base store first, then full relayout (font scale changes
every layout width/height — invalidate all entries, keep scroll
anchored to the top visible block, not the pixel offset).
**Falsify:** skip the relayout on fontScale; scaled-height test fails.

### P3.6 — ephemeral state
`saveEphemeralState()`/`restoreEphemeralState()` JSON: scroll
(top-visible block index + fraction), cursor (block index + byte),
fold list (empty until P5.6; the key exists now so the schema is
stable). Round-trip test through detach/reattach.

### P3.7 ⏸ — phase close (as P1.5).

---

## Phase 4 — inline/text parity

### P4.1 — full inline kind set
Extend `InlineFormatting` to the live highlighter's 8 kinds + footnote
refs, same `Theme::Slot` mapping (see live CLAUDE.md §"Inline-format
highlighter"). Delimiters of the new kinds (==, ~~, `[[ ]]`, `[ ](url)`
with the URL part, `[^n]` brackets) join the omission mechanism.
Character-by-character merge for overlapping spans, as live does.
**Falsify:** drop the wikilink slot mapping; per-kind test fails.

### P4.2 — link activation + hover
Click (read-only) / Ctrl+click (editing) on a link/wikilink/tag hit
→ `LinkService::activate` → `linkActivated`; hover tracking emits a
hover signal (target + global rect) for Corbomite's `HoverPopover`,
pointer cursor over links (cache the shape — see the styled smell).
**Falsify:** off-by-one the hit-test span range; activation test fails.

### P4.3 — format verbs + actions
Implement `toggleBold/Italic/Strikethrough/InlineCode`, `insertLink`,
`setHeadingLevel` over core `FormatOps` (selection-aware; caret-only
toggles insert paired markers + place caret inside).
`CanvasActionController`: QActions for the `ActionId` set relevant
now (formatting + headings + lists + link; table ops arrive P5.2),
enabled-state wired to read-only/selection/undo-depth. Corbomite
binds its KF6 shortcuts to these QActions.
**Falsify:** invert the read-only enabled-state; gating test fails.

### P4.4 — context menu
`contextMenuEvent`: cut/copy/paste/select-all + format section from
the ActionController + "Copy link target" when over a link. A
protected virtual (`buildContextMenu(QMenu&)`) lets consumers extend.
**Falsify:** show paste enabled while read-only; menu test fails.

### P4.5 — readable line width
`setContentWidthPolicy(FullWidth | FixedColumn{px})`, centered column
when fixed (Obsidian "readable line length"). Layout width flows from
policy; resize relayouts realized entries (estimates for the rest);
scroll stays anchored to top visible block. Tables/code may exceed
the column with horizontal pan inside their own rect (Obsidian
behavior) — if that's oversized for one session, land policy +
paragraphs first and log the table half.
**Falsify:** ignore policy in `layoutWidthFor`; column test fails.

### P4.6 — code-block syntax highlighting
`Kf6SyntaxHighlightService` keyed by the fence info string; token
runs → draw-time or restyle formats (decide against the T7
`setFormats` trap — go through `restyleInline`'s atomic path).
Highlighting is per-block and cached with the entry; a service miss
renders plain monospace.
**Falsify:** feed the wrong language key; token-color test fails.

### P4.7 — checkboxes
`- [ ]`/`- [x]` list items render a checkbox glyph (marker
decoration, like list bullets); click toggles the `x` byte via
`d2ApplyBufferEdit` in one transaction (works in LivePreview, gated
read-only). Caret/typing in the item text unaffected.
**Falsify:** toggle writes to the wrong byte; neighbor-item test fails.

### P4.8 ⏸ — phase close (as P1.5; re-run perf).

---

## Phase 5 — block parity

### P5.1 — table cell wrap + navigation
Cells get wrapping layouts (width budget per column policy; row
height = max wrapped cell height). Tab/Shift+Tab next/previous cell
(last cell Tab appends a row — Obsidian behavior); Up/Down at cell
edge moves rows, then exits the table.
### P5.2 — table row/col ops + alignment
Context-menu + ActionIds (`InsertTable`, `DeleteRow`, `DeleteColumn`,
add row/col above/below/left/right, alignment tri-state writing the
delimiter row) — all as byte edits to the table block's buffer in one
transaction each, via `TableGeometry`.
### P5.3 — math
jkqtmathtext renders Math blocks (display) and `$…$` spans (inline,
as an inline object at layout time — `QTextLayout` format with
object replacement is NOT available without a document; render inline
math as a styled span in-line (code-like) and display math as a
painted block; log if inline visual parity demands more).
Caret-in-block reveals source (same reveal rule as code fences).
### P5.4 — images + renderer seams
Image blocks paint via a consumer-provided resource lookup (seam on
EditorWidget; placeholder box otherwise). `setMermaidRenderer`
injection: mermaid code blocks paint the renderer's pixmap when
caret-outside, source when caret-inside. Embed *seam* only
(`EmbedRegistry` consumption, placeholder rendering).
### P5.5 — callouts + frontmatter + footnote defs
Callout blockquotes (`> [!note]`): typed header (icon + title, Theme
slots), body indent; fold arrives with P5.6. Frontmatter block
renders as a properties-style header (read-only render; caret-inside
reveals raw YAML). Footnote definitions render at their block with
back-reference styling.
### P5.6 — folding
`Session::foldedRegions` (`FoldRef`) → heading sections, long lists,
callouts collapse: folded blocks' entries are skipped in y-layout
(height 0, not unrealized), a fold affordance paints in the gutter
margin, caret motion skips folded ranges, ephemeral state now
round-trips real folds.
### P5.7 ⏸ — phase close (perf re-run mandatory — folding and tables
both touch the y-position walk).

Falsification for each P5 task follows the standard protocol; pick
the seam named in the task's test when you write it.

---

## Phase 6 — collaboration surface

### P6.1 — Session caret authority closure
Every local caret/selection change → `Session::setPrimarySelection`
(anchor-typed). After any model change the view re-resolves from the
Session anchor (this replaces nothing — `clampCaret` stays as the
fallback when the anchor's block vanished). Undo/redo: assert the
Session-restored caret lands at the pre-edit position when core
provides it; where core doesn't repopulate the Session yet (guide
B.4's known partial), log the exact gap — do not patch core
unprompted.
**Falsify:** stop pushing to the Session; the external-reader test
(reads `session->primarySelection()` after typing) fails.

### P6.2 — remote presence rendering
`EditorWidget::setRemotePresences(QList<RemotePresence>)` —
`{Selection, displayName, QColor}`. Paint: remote caret bar +
name flag (top of caret, fading like collabedit's), selection tint
per participant color, all draw-time `FormatRange`s (never cached
state). Stale/departed filtering is the consumer's job (collabtext
`PresenceManager::is_live` — reference `app/collabedit`, do not link).
**Falsify:** paint remote selection with the local selection color;
distinct-color assertion fails.

### P6.3 — concurrency tests
The load-bearing one: open an IME composition, apply a remote
`d2ApplyBufferEdit` to the same block, assert the composition
survives (preedit re-splices over new text) and commit lands at the
right anchor. Plus a gremlin test (collabedit's pattern): scripted
random remote edits during scripted local typing, N iterations,
assert convergence + no caret stranding + no constitution-tempting
workaround needed. This test failing structurally (needing a guard)
would falsify the D5 premise — treat as stop-and-report, not
fix-quietly.

### P6.4 ⏸ — phase close.

---

## Phase 7 — polish + a11y (after gate G1)

### P7.1 — accessibility per G1 decision
(scope set by the user at G1; budget order-weeks if full
`QAccessibleTextInterface`).
### P7.2 — drag-drop + middle-click paste
Text drag in/out (plain text + `text/markdown`), file-drop → signal
to consumer (Corbomite decides embed vs link), X11 primary-selection
paste. Reference `QWidgetTextControl` for event ordering.
### P7.3 ⏸ — arc close
Full suite; perf; constitution honest read of every file touched
since P1; F1 parity audit done or explicitly waived by the user;
update STATUS/CLAUDE/queue; hand the G2 adoption question to the
user with a one-page summary of what's proven.

---

## Findings log (append here; spike spec §9 is closed)

> One line minimum per surprise: constraints that bit, Qt quirks,
> core gaps discovered, perf numbers at phase closes.

**P1.1 (2026-08-13).**

- **Heading levels were never reachable by typing.** The first `#`
  satisfies `countLeadingHashes` on its own, so the block promotes to
  Heading at one keystroke and the promote path — guarded on "Paragraph
  only" — never sees `##`…`######`. Canvas grew
  `updateCaretHeadingLevel()` (form-aware, raise-only; demotion stays
  with the structural-key path), mirroring live's same-kind level branch.
  Consequence before the fix: *every* typed heading serialized as `# `,
  because `serializeHeading` reads the `level` attr and canvas wrote none.
- **Setext is the one promotion that must edit the buffer.** T6's "keep
  the matched marker" rule exists so a typed block matches a *loaded*
  one — and a loaded setext heading's buffer is content-only (load drops
  the underline; `serializeHeading` rebuilds it from `level`). Keeping the
  typed underline made `Hello\n=` save as `Hello\n=\n=====`. Trimmed in
  the promoting transaction; the caret then sits past the new end, and
  nothing clamps it afterwards (the promote runs *inside* the
  document-changed pass), so the clamp is explicit at the end of
  `promoteCaretBlockKind`.
- **Display math is unreachable by typing, in both leaves.** `$`
  promotes to Math before the second `$` arrives, so `mathDisplay` is
  always false for typed math. The rule distinguishes the two and the
  attr is written in the promoting transaction; re-inference *within* a
  Math block is left for whoever needs display math (P5.3), recorded here
  rather than fixed unprompted.
- Test count unchanged at **288/288** — P1.1 grew an existing executable
  (`tst_canvas_kind_transition`, 1 → 13 assertions) rather than adding one.
