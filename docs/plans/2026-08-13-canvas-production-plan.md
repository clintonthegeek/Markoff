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
- **Test-run tier before commit** — full suite is not the default per
  task; it's reserved for tasks with real cross-leaf risk:
  - **Canvas-scoped only** (`scripts/run-tests.sh -R canvas`): the
    default. Any task whose diff stays inside
    `libs/markoff-canvas/` and doesn't change a core seam's observable
    behavior for other consumers.
  - **Full suite** (`scripts/run-tests.sh`): required when the task
    touches `libs/markoff-core/` (a promoted seam can regress live/
    styled/source silently) OR edits more than one leaf. Also required
    at every ⏸ phase-close task, regardless of what that task itself
    touches.
  - Unsure which tier a task lands in: run the full suite. The cost of
    a wrong guess only cuts one way.

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
| P1.2 Coordinates byte↔QChar → core | ☑ | `ac405aa6` | `cd2588f1` / `95385e7f` |
| P1.3 Theme background-slot fallback + missing slots | ☑ | `33c0fc72` | `a6891970` / `9e8e28b9` |
| P1.4 Marker-convention canonization (docs + doc-comment) | ☑ | `f6a28a5f` | n/a |
| P1.5 ⏸ phase close: full suite + constitution + findings sweep | ☑ | n/a | n/a |
| **P2 — projection map (delimiter reflow)** | | | |
| P2.1 ProjectionMap + omission for emphasis/strong | ☑ | `edb800c5` | `441fd827` / `d7de4364` |
| P2.2 Omission for heading prefix + code fences | ☑ | `24725a47` | `dcd62756` / `904edc08` |
| P2.3 Per-cell maps + cross-table selection (#18.2) | ☑ | `bfe0bb4e` | `e9d95a3c` / `0ba7ca4b` |
| P2.4 ⏸ perf re-baseline (E9 budgets, build-perf) | ☑ | n/a | n/a |
| **P3 — MarkdownView contract v2** | | | |
| P3.1 EditorWidget wrapper + setDocument/Session + contract harness | ☑ | `07b3301c` | `b65c8aae` / `b62b45ec` |
| P3.2 Cursor/scroll position mapping + signals | ☑ | `83adcc2d` | `8ba90e7a` / `a07e715c` |
| P3.3 Read-only gates + caretRect | ☑ | `21a020a2` | `6a5fefe9` / `fa9c1704` |
| P3.4 FindController: highlight + navigate | ☑ | `a26a0795` | `de9920ef` / `c5a22a58` |
| P3.5 EditorContext + theme/fontScale through the wrapper | ☑ | `44481d40` | `93795083` / `c4278364` |
| P3.6 Ephemeral state JSON round-trip | ☑ | `3b4247a0` | `0fdb7a85` / `45026f6c` |
| P3.7 ⏸ phase close | ☑ | n/a | n/a |
| **P4 — inline/text parity** | | | |
| P4.1 Full inline kind set (highlight/strike/link/wikilink/tag/footnote-ref) | ☑ | `a51917f8` | `81bb62fe` / `6c51bbe5` |
| P4.2 Link activation + hover | ☑ | `0d4b49b1` | `bf1fffb0` / `3cf92c23` |
| P4.3 FormatOps verbs + CanvasActionController | ☑ | `2c3f482a` / `a1fe576f` | `48545c18` / `fa11992e` |
| P4.4 Context menu | ☑ | `7944edc3` | `f3ce2870` / `f2fed72b` |
| P4.5 Readable-line-width policy + resize (Obsidian calibration: F1) | ☑ (reduced scope — see finding) | `d365be58` | `e4b40d0e` / `06b1c06c` |
| P4.9 Inline title band (user-directed; spec §5.2) | ☑ | `79db4dc2` | `dedab5ab` / `a22545ab` |
| P4.6 Code-block syntax highlighting | ☑ | `fb3bfa3e` | `512cb721` / `c66d577d` |
| P4.7 Task-list checkboxes (render + toggle) | ☑ | `31e5c138` | `81e92490` / `7d714198` |
| P4.8 ⏸ phase close | ☑ | n/a | n/a |
| **P5 — block parity** | | | |
| P5.1 Table: in-cell wrap + cell navigation | ☑ | `d6a0ce0a` | `3a7afd9e` / `70854dc5` |
| P5.2 Table: row/col ops + alignment | ☑ | `13ebc8fa` | `281fbc73` / `68557575` |
| P5.3 Math blocks (jkqtmathtext) | ☑ | `f482a4d0` | `6dea6ab6` / `58d373bb` |
| P5.4 Images + Mermaid/embed seams | ☑ | `f94bf525` | `1c4654ee` / `c851e604` |
| P5.5 Callouts + frontmatter + footnote defs | ☑ | `20949498` | `715b301b` / `c421810c` |
| P5.6 Folding via Session | ☑ (reduced scope — see finding) | `989c714d` | `1a4fd240` / `ef298d6a` |
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
**Multi-cursor readiness (F1a):** ask "is this entry/span revealed by
*any* cursor" through **one** predicate function taking the cursor set —
not `m_caret.block == entry.id` repeated across the restyle sites. The
set has one member today; this costs nothing now and is a sweep later.
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
**Multi-cursor readiness (F1a):** write the cursor key as a *list* of
one, not a scalar, so the deferred multi-cursor arc is not a schema
migration. `cursorPosition()` on the contract stays single — that is the
primary caret by definition.

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

### P4.9 — inline title band
Optional leading title band per spec §5.2 (user-directed 2026-08-13):
`setInlineTitle(QString)` / `setInlineTitleVisible(bool)` on
`EditorWidget`, an editing affordance, and a `titleEdited(QString)`
signal the consumer turns into a file rename. Rendered as a leading
non-document entry in the y-layout, sharing the content column (P4.5).
It is not a block: excluded from `cursorPosition()` flat lines, find,
selection-copy and serialization. Caret seam: Down/Enter from the title
lands at block 0 byte 0; Backspace at document start does not consume
the title.
**Falsify:** include the title in the flat-line walk; a
`cursorPosition()` round-trip assertion must fail.

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
**Multi-cursor readiness (F1a):** paint from a **kind-tagged** list
(`Selection::Kind` discriminates `Presence` from local `Secondary`)
rather than a remote-only list, so the deferred local multi-cursor arc
reuses this paint path instead of growing a second one.
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

**P5.6 (2026-08-14).**

- **Reduced scope vs. plan text**: the task named `Session::foldedRegions`
  (`FoldRef`) as the seam to consume. `FoldRef::start` is a raw
  `CollabText::Crdt::Anchor`, and the only public constructor
  (`MarkoffDocument::anchorAt`) is backed by the legacy flat buffer —
  documented elsewhere as stale/unreliable on a D2-loaded document,
  which canvas exclusively is. No public, D2-safe accessor exists to
  build an anchor from a `BlockId`+offset (the private per-block
  conversion lives in core's `src/`, not `include/`). Same wall P3.6
  hit for scroll/cursor, which already bypasses Session's anchor
  accessors via View's own block-index scheme — P5.6 follows that
  precedent instead of inventing a core API: fold state lives in
  `View::m_foldedHeads` (BlockId set), and ephemeral-state
  round-trips it via View's own block-index API, not Session.
  `Session::foldedRegions` remains genuinely unwired, same as its
  already-unused scroll/selection accessors. **Follow-up needed
  before this can close for real**: a D2-safe public
  `(BlockAnchor, offset) -> CollabText::Crdt::Anchor` accessor in
  core.
- New `Folding.{h,cpp}`: stateless `resolveFoldable(doc, id)` —
  canvas-local shape rules for the three foldable kinds (heading
  section, long list run, callout body — P5.5's `BlockQuoteRunId`
  needed zero adjustment to become foldable).
  "Long list" interpreted as: first item of a run of ≥6 consecutive
  top-level `ListItem` blocks; folding hides every subsequent item in
  the run at any indent (a judgment call, logged as such — the plan
  text didn't pin a threshold).
  `BlockLayoutCache::recomputePositions()` adds 0 height for a folded
  entry while its real `height` field is left untouched (queryable,
  not unrealized, per the task's literal wording). Caret Up/Down/
  Left/Right cross-block stepping routes through a new
  `nextVisibleEntryIndex()` so motion skips folded ranges; toggling a
  fold under the caret relocates it to the fold head.
  `kPageMargin` widened 16→28px so the new gutter fold-chevron
  doesn't collide with the existing marker/checkbox slot.
- Canvas tier green (`tst_canvas_folding` new, 13 cases); one
  pre-existing `tst_canvas_layout_width` threshold fixed after the
  margin bump; `check-constitution.sh` clean.

**P5.5 (2026-08-14).**

- Reparse-helper pattern (P4.6/P5.4 precedent) held for callout and
  frontmatter, not footnote defs:
  - **Callout** — no core concept; `BlockKind::BlockQuote` covers
    plain and typed alike. New `CalloutBlocks::parseCallout`
    (canvas-local). A custom callout title is unrecoverable: B1's
    soft-break `\n`→space collapse destroys the line break between a
    typed title and its body before this parser ever sees the
    buffer, so the header shows a fixed type label ("Note"/"Warning"/
    etc.), never a recovered custom title.
  - **Frontmatter** — genuinely not a block; `markoff-parser`'s
    `Document::extract` strips it from the body before block-parsing.
    Rendered as a leading non-block y-layout entry, same shape as
    P4.9's inline-title band (`FrontmatterBlock::parseFrontmatterProperties`,
    bounded flat-scalar YAML only). Click-to-toggle collapsed/raw
    stands in for "caret-inside reveals source" since there's no
    block for a real caret to enter.
  - **Footnote definitions** — turned out to be an ordinary
    `BlockKind::Paragraph` block with a real `BlockId`:
    `Document::extract` only *copies* footnote-def lines for
    numbering, it does not strip them from the body (unlike
    frontmatter). Handled via a `BlockPresentation::presentationFor`
    per-kind case (`FootnoteDefBlocks::parseFootnoteDef`) — no second
    y-layout entry, no new paint path.
- `Theme::Slot` enumerators `CalloutNote/Warning/Tip/Important/Caution`
  already existed (unused, no default colors) — same latent-slot gap
  as P1.3's `QuoteBackground` and P4.6's Code-token slots. Only
  `defaultLight()`/`defaultDark()` colors were added; no enum changes.
- Full suite 305/305 (required since `Theme.cpp` is core); canvas
  tier 28/28; `check-constitution.sh` clean over 63 files.

**P5.4 (2026-08-14).**

- `KindInference` maps both `![alt](src)` and `![[target]]` (embed) to
  the same `BlockKind::Image` — there's no parser-level distinction.
  New `src/MediaBlocks.{h,cpp}` (`Detail::parseImageBlock`) reparses
  the buffer to tell them apart, same "canvas-local rule" precedent
  as `CodeHighlighting::parseCodeFence` (P4.6 finding).
  `BlockKind::Mermaid` is a vestigial enumerator the load path never
  assigns — mermaid arrives as a plain fenced `CodeBlock`, so the
  mermaid seam hangs off `isCodeBlock` + fence-language detection
  (reusing `CodeHighlighting::parseCodeFence`), not that kind.
- `setImageResourceLookup` (`std::function<QPixmap(QString)>`),
  `setMermaidRenderer` (new canvas-local `MermaidRenderer` abstract —
  none existed anywhere in the tree), `setEmbedRegistry` (wires the
  existing unmodified core `Markoff::EmbedRegistry`, consulted for
  `hasExtension()` only, always placeholder — never mounts a
  `MarkdownRenderChild`, per the plan's explicit placeholder-only
  scope). Mermaid miss/no-renderer falls back to plain code-block
  source, matching P4.6's "service miss renders plain monospace."
- No core changes needed; `EmbedRegistry` used as-is.
- Canvas tier 27/27; `check-constitution.sh` clean over 56 files.

**P5.3 (2026-08-14).**

- Display math renders via a new plain-C++ `MathRendering.{h,cpp}`
  wrapper around vendored `jkqtmathtext` (linked privately into
  `markoff_canvas`); `BlockLayoutCache::rebuildInline` builds the
  pixmap only when the caret is outside the block, `View::paintEvent`
  swaps text-layout paint for the pixmap in that state. Text layout is
  always built regardless so hit-test/caret/selection stay correct.
  Caret-in-block reveals raw LaTeX, same per-block trigger as the
  code-fence mechanism (P2.2).
- Inline `$...$` renders as a styled monospace/code-like run, not real
  glyph rendering — `QTextLayout` has no inline-object-replacement
  path without a backing `QTextDocument`, which C3 forbids. This is
  the plan's own anticipated fallback ("log if inline visual parity
  demands more") — flagged as a real, intentional gap vs. Obsidian's
  inline-rendered math, not a bug to silently work around.
- **Parser gap found**: `latex_span`/`latex_block` delimiter spans
  never get `parentCharStart`/`parentCharEnd` populated —
  `collectParentRanges` checks for a `latex_span` node type the
  markoff-parser grammar never actually emits (both `$...$` and
  `$$...$$` parse as `latex_block`). Worked around with a per-block
  reveal rule (matching code fences) instead of opening the parser
  seam; revisit if per-span (not per-block) math reveal granularity
  is ever needed.
- Closed a carried P1.1 finding: typing `$$` could never reach display
  math (first `$` promotes to inline Math before the second `$`
  arrives). Added `View::updateCaretMathDisplayMode()` (raise-only,
  mirrors `updateCaretHeadingLevel`).
- markoff-live's `MathRenderer`/jkqtmathtext wiring was dead code —
  `MathDelegate.qml` never called it, always showed raw source.
  Canvas P5.3 is the first real rendering use of jkqtmathtext in the
  codebase.
- Canvas tier 26/26; `check-constitution.sh` clean over 52 files.

**P5.2 (2026-08-14).**

- `InsertTable`/`DeleteRow`/`DeleteColumn` `ActionId` enumerators
  existed since the original v1.0 enum but were never wired to
  anything until this task.
- The delimiter row's byte range was being discarded entirely by
  `TableGeometry` before this task (parsed then thrown away).
  Recovering it for mutation required a new parallel `lines` field
  (every physical line — header + delimiter + body) alongside the
  existing row-major `rows`, rather than folding it into `rows`'
  shape, to avoid disturbing P2.3/P5.1 callers.
- Column ops apply one buffer edit per physical line, computed
  bottom-to-top from a single frozen parse so earlier edits' byte
  offsets don't invalidate later ones within the same transaction.
- Full suite 302/302 (was 300/300 after P4.8); canvas tier 25/25;
  `check-constitution.sh` clean over 49 files. No C1-C4 issues, no
  core changes beyond the ActionId enumerators.

**P5.1 (2026-08-14).**

- `BlockLayoutCache::realizeTable` is now two-pass: pass 1 finalizes
  each column's width budget (240px cap / 40px min, unchanged), pass
  2 wraps every cell's `QTextLayout` to that width via the same
  `beginLayout`/`createLine`/`setLineWidth`/`setPosition` loop
  `rebuildInline()` uses elsewhere; row height is the max wrapped-cell
  height. Replaces the old `NoWrap` single-line cell layouts that
  silently overflowed the column cap.
  `View::tryTableTab` hops cells row-major; last-cell Tab calls new
  `View::appendTableRow` (one `d2ApplyBufferEdit` transaction).
  Because the cache's cell grid is only rebuilt lazily, the
  post-append caret is computed by re-parsing the fresh buffer via
  `TableGeometry::parseTableBlock` rather than trusting the
  momentarily-stale cache. `View::moveCaretVerticallyInTable` walks
  lines within the cell's own wrap first, then rows in the same
  column, then falls through to the existing cross-block exit path.
  `hitTestTable` now picks the line by y within the cell like
  `hitTest()` does for ordinary blocks, instead of always resolving
  `lineAt(0)`.
- No core changes needed or made.
- Shift+Tab at the first cell is a no-op (undecided by the plan;
  matches Obsidian's own behavior of just staying put).
- **Commit-message SHA correction**: `d6a0ce0a`'s message cites a
  fabricated falsification SHA (`c9e3729`) written before the
  falsification pass actually ran — a process mistake, left
  uncorrected per the no-amend policy. The real falsification pair is
  `3a7afd9e` (throwaway: disabled last-cell-Tab row-append, causing an
  out-of-bounds `tableCells` index / abort) / `70854dc5` (revert) —
  those are the SHAs recorded in the checklist row above; trust this
  log entry over that commit's own message.

**P4.8 (2026-08-14) — phase close.**

- Full suite: **300/300**, up from 293/293 at Phase 3 close.
  `check-constitution.sh`: clean (C1–C4) over 46 files.
- Seven new canvas test executables this phase
  (`tst_canvas_links`, `tst_canvas_format_ops`, `tst_canvas_context_menu`,
  `tst_canvas_layout_width`, `tst_canvas_inline_title`,
  `tst_canvas_code_highlight`, `tst_canvas_task_checkbox`) plus growth
  in `tst_canvas_inline_formatting` (P4.1) and core's `tst_format_ops`
  (+8 cases, P4.3).
- Two items carried open into `docs/STATUS.md` dormant list rather
  than closed this phase: table/code horizontal-pan-within-own-rect
  (P4.5 reduced scope) and the `Theme` Code*-token color-slot gap
  (P4.6 finding). Neither blocks Phase 5.
- Table ops (`P5.1`/`P5.2`) remain explicitly deferred, as the plan
  always intended — no format/action work was built for them this
  phase.
- One real core-seam decision this phase: P4.3's per-block `FormatOps`
  overloads, made by explicit user choice after the first attempt
  correctly stopped rather than violate C4. See P4.3's findings entry.
- `docs/STATUS.md` baseline and workfront pointer updated to Phase 5
  as next.

**P4.7 (2026-08-14).**

- **The task's literal "toggle the `x` byte via `d2ApplyBufferEdit`"
  wording doesn't match core's actual architecture** — per
  markoff-core's documented ListItem convention, a task marker
  (`[ ]`/`[x]`) is stripped from the buffer at load and is
  display-only, reconstructed from `MarkerStyle`/`Checked` attrs via
  `listItemDisplayMarker()`. There is no in-buffer "x byte" to touch.
  Used `MarkoffDocument::toggleListItemChecked(BlockId)` instead — the
  correct existing one-transaction primitive (`d2SetBlockAttr` inside
  one `UndoLog::Transaction`), same API markoff-live's QML delegate
  already calls. This is a plan-text wording fix to make, not an
  implementation gap.

**P4.6 (2026-08-14).**

- **`Theme::defaultLight()`/`defaultDark()` define no colors for any
  of the 16 `Code*` token slots** (`CodeKeyword`, `CodeControlFlow`,
  `CodeString`, …) — `Theme::colorForCodeToken()` returns invalid
  `QColor` for most kinds out of the box, so real-world code
  highlighting under the default theme currently shows no token
  differentiation even though the wiring is correct end to end (the
  test built its own `Theme` with explicit colors to exercise the
  mechanism). This is a `Theme` default-palette gap, not a wiring bug
  — a small, cleanly-scoped core follow-up (define the 16 slots),
  not urgent since nothing regresses, but worth doing before any
  visual/dogfood pass.
- Buffer keeps fences inline (per the P1.4 marker-convention), so
  `parseCodeFence` is a pure string scan over `blockText()` for the
  language token + content byte range — no AST spans needed, same
  "canvas-local rule" precedent as `isCodeBlockFence`.
- T7 avoidance: token-color ranges are appended to the same `ranges`
  list `InlineFormatting::inlineFormatRanges` already produces, before
  the one `setFormats()` call in `rebuildInline()` — no second
  out-of-band `setFormats()` call, everything rides the existing
  atomic formats-then-lines rebuild.
- `Kf6SyntaxHighlightService::definitionForName()` resolves common
  lowercase fence identifiers case-insensitively (`cpp`, `python`,
  `javascript`, `json`, `bash`, `rust` all hit; `shell`/`sh` do not).
- The P1.4 finding about `serializeCodeBlock`'s double-fence risk
  ("worth a look before anything edits CodeBlock buffers in canvas")
  still doesn't apply — P4.6 is read-only rendering, never edits
  CodeBlock buffers — remains open for whenever canvas code-block
  editing lands.

**P4.9 (2026-08-14).**

- Exclusion from `cursorPosition()`/find/selection-copy/serialization
  holds by construction, not by an added guard: `toCursorPos`/
  `fromCursorPos` walk `doc->iterateBlocks()` only, `selectedText()`
  walks cache entries only, and the title is never added to
  `BlockLayoutCache` — there was nothing to explicitly exclude. Only
  `hitTest()` needed an explicit vertical-extent gate (so drag-select/
  right-click can't resolve into the band).
- `titleBandHeight()` is the single derived y-offset applied at every
  y-consuming site (hitTest, paint, caretRect, blockRect,
  tableCellRect, scroll anchor, ensureCaretVisible, documentHeight,
  fontScale anchor capture) — one seam, not a scattered set of
  special cases.
- Spec names only a Down/Enter caret seam *into* block 0 from the
  title; no described keyboard path *back up* from block 0 into the
  title (mouse click only). Left as-is rather than invent one —
  worth a decision if Corbomite dogfooding wants it.
- Title band is fixed-height/no-wrap (unlike Obsidian, which wraps
  long titles) — deliberate simplification, logged as a decide-
  yourself call, not a gap.

**P4.5 (2026-08-14).**

- Landed reduced scope, per the plan's own escape hatch: content-width
  policy (`FullWidth`/`FixedColumn{px}`), centered column via
  `pageMargin()`, resize/policy-toggle relayout with scroll-anchor
  preservation (reusing P3.6's `scrollAnchor()`/`setScrollAnchor()`).
  **Not landed:** tables/code blocks exceeding the column with their
  own horizontal pan (Obsidian's actual overflow behavior) — they
  currently just take the (possibly narrowed) column width like any
  other block. Needs per-rect horizontal scroll-offset state that
  doesn't exist anywhere in `BlockLayoutCache`/`View` yet; a real
  sub-feature, carried open rather than rushed. A future session
  should pick this up before the Obsidian-parity audit (P7.3) if it's
  still open then.
- `layoutWidthFor()` becoming the single conversion point means
  `pageMargin()`, `textWidth()`, paint, hit-test, caret, `blockRect()`
  and table rects all center for free — no second code path needed
  for centering.
- `BlockLayoutCache::setTextWidth()` already dropped realized entries
  back to estimates on a width change (pre-existing lazy-realize
  machinery); P4.5 needed no changes there, only the anchor-preserving
  wrapper around the existing relayout trigger.

**P4.4 (2026-08-14).**

- Paste didn't exist in the leaf at all before this task (no
  `Key_V` handling anywhere in `View.cpp`) — P4.4 ended up building
  canvas's first real paste path (clipboard text insertion, multi-line
  split routed through the existing `tryStructuralKey` path), not just
  its context-menu affordance. This closes the gap the P3.3 findings
  entry flagged ("no Ctrl+V handling anywhere ... deferred to P4.4").
  Ctrl+V wired too, beyond the task's literal menu-only wording.
- `check-constitution.sh`'s C2 regex (`singleShot\(0`) false-positives
  on a legitimate `QTimer::singleShot(0, ...)` test pattern used to
  intercept `QMenu::exec()`'s nested event loop; worked around with
  `singleShot(20, ...)` + a comment. Worth tightening the script's
  regex if this recurs.
- `QMenu::exec()` genuinely blocks (nested event loop) — a guessed
  pixel click-point in the first draft of the e2e context-menu test
  hung the whole `-R canvas` run for 137s. Any future context-menu
  test should use the exact-QChar `pointForFullQChar` technique
  (`tst_canvas_links.cpp`) rather than guessed coordinates.

**P4.3 (2026-08-14).**

- **Coordinate-space fork, resolved by user decision.** Core
  `FormatOps` (`wrapToggle`/`insertLink`/`setHeadingLevel`) operates
  entirely over flat `widgetFlatView`/`QtRange` — a real C4 conflict
  for canvas, not a missing-feature gap; the P1.2 findings entry had
  flagged this coming ("worth a look when P4.3 puts canvas on
  FormatOps") but left it unresolved. First pass at this task
  correctly stopped rather than construct a flat view or hand-sum
  block byte offsets. User decision: add per-block, byte-offset
  overloads (`wrapToggleInBlock`/`insertLinkInBlock`/
  `setHeadingLevelInBlock`, new `ByteRange` type) to core `FormatOps`
  as a named seam — flat overloads untouched, live/source unaffected.
- `setHeadingLevelInBlock` needed no line-start search: B1 (no
  internal `\n` in a block buffer) makes block-start *is* line-start
  unconditionally, so the flat version's backward-`\n` scan collapses
  to a no-op in per-block space.
- Multi-block format-op application must snapshot the covered-block
  list *before* mutating: each `FormatOps` call flushes synchronously
  (C2), resyncing `BlockLayoutCache`'s entries vector via
  `onDocumentChanged()` — iterating that vector live while mutating
  through it would be a use-after-resync. Same discipline
  `collapseSelection`'s `middles` collection already established.
- No canvas verb exists yet for `Blockquote`/`BulletedList`/
  `NumberedList`/`TaskList`/`IndentMore`/`IndentLess` `ActionId`s
  (list/blockquote toggle only via typed-marker kind inference;
  indent only via real Tab through `StructuralKeyHandler`) — no
  QActions built for them rather than ship dead triggers, matching
  the plan's own table-ops deferral discipline.

**P4.2 (2026-08-14).**

- `LinkService::notifyHover`'s real signature is `(LinkActivation,
  QPoint)` — no `QRect`, despite the plan text's "target + global
  rect" wording. Core is read-only for this task, so passed the mouse
  event's global position as the anchor (same precedent as live/
  styled). If Corbomite's `HoverPopover` genuinely needs a rect, that
  is a core-seam task to name explicitly, not something to improvise
  here.
- Tags: live's `findLinkSpanAt` doesn't support tag activation at all
  (only checks `isLink || isWikilink`); canvas adds it per this
  task's explicit wording. A tag `SourceSpan` carries no `LinkTarget`
  — its own char range *is* the tag text including `#`.
- `QAbstractScrollArea::viewportEvent` doesn't forward Enter/Leave the
  way it forwards mouse-button/move — needed a `viewport()` event
  filter to close hover state on pointer-exit, same reason styled's
  `LinkInteraction` installs one.
- Cursor-shape caching (avoiding the styled smell of `setCursor`
  every mouse-move) gated on a bool tracking hover-state transitions,
  not per-move.

**P4.1 (2026-08-14).**

- Delimiter omission for the five new kinds (`==`, `~~`, `[[ ]]`,
  link-destination, `[^n]`) needed **zero new code** — the parser's
  `collectParentRanges` already emits parent ranges for
  `strikethrough`/`highlight`/`wiki_link`/`inline_link`/`shortcut_link`
  the same way it does for emphasis/strong/code_span, and
  `delimiterShouldHide` was already generic over any `isDelimiter` span.
  Mirrors the P2.2 heading-marker finding — one mechanism, no per-kind
  branching, exactly as spec §4.2 intends.
- No `Theme::Slot` exists for footnote refs; live's own
  `InlineHighlighter` leaves them out-of-scope too. Rendered
  superscript-only rather than opening a core seam this task didn't
  name. Revisit if footnote refs need a distinct color later.
- `isLink`/`isWikilink`/`isTag` are mutually exclusive in the parser's
  span data in practice — documented as an assumption rather than
  adding defensive precedence logic.

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

**P1.2 (2026-08-13).**

- Promotion was clean — the two forks were byte-identical apart from
  comments, so `<markoff/core/TextUnits.h>` is a straight lift (the
  UTF-8 lead-byte switch is now one shared `sequenceLength` helper).
- Live's `<markoff/live/Coordinates.h>` is **kept as a using-declaration
  alias** rather than deleted: it is a public exported header, live's
  ~14 call sites all reach it through a `coords::` namespace alias, and
  a consumer outside this repo may include it. Zero call-site edits;
  the implementation copy is gone, which is what the task was for.
- **Coverage now sits in the wrong suite.** The only tests of these
  functions are `tst_live_render_coords` — a *live* test exercising a
  *core* helper (through the alias). Recorded in `markoff-core/CLAUDE.md`
  too: if live retires at G3, that test **moves to core**, it does not
  retire with the leaf.
- **A third conversion helper exists in core and was left alone:**
  `SourceTextDocumentBinding::qtPosToByteOffset(QString, int)` — same
  idea, `QString` input, flat/D4 coordinate space, different
  surrogate-truncation convention; used by `FormatOps` and the source
  leaf. Consolidating it means touching the flat-space seam, which P1.2
  does not name. Worth a look when P4.3 puts canvas on `FormatOps`.

**P1.3 (2026-08-13).**

- The bug was real but narrower than "missing slots" implied: only
  `QuoteBackground` was ever undefined among the background-class slots
  canvas reads (`CodeBlockBackground` was already set in both palettes).
  `GutterBackground`/`ScrollbarThumb`/`FoldArrow` stay undefined — no
  caller reads them yet, so defining a color for them would be
  speculative.
- `isBackgroundSlot()` is a closed switch over the `*Background`-named
  slots, not a naming-convention guess — `CursorPrimary/Secondary/Presence`
  paint as a pen, not a fill, so they were deliberately excluded even
  though "Cursor" sits next to "Background" slots in the enum.
- Live/styled audit (task's explicit check): grepped both for every
  slot in the background list. Live never references `QuoteBackground`
  at all; the slots it does use (`EditorBackground`, `CodeBlockBackground`,
  `SelectionBackground`, `Search*MatchBackground`) were already defined,
  so none of them ever hit the old TextDefault fallback. No reliance to
  preserve.
- Falsification test lives directly against `presentationFor()`
  (`tst_canvas_render::blockquote_background_is_distinct_from_its_text_color`)
  rather than a pixel-diff render — cheaper and it is the actual
  contract (`BlockStyle::background`), a screenshot would only be
  testing the paint path on top of it.

**P1.4 (2026-08-13).**

- Pure documentation task, no code behavior touched: fixed
  `listItemDisplayMarker()`'s doc comment (it claimed ListItem was the
  only narrowed kind; BlockQuote is narrowed the same way — T1's
  finding), added the per-kind buffer-marker table to
  `markoff-core/CLAUDE.md`, and a pointer + summary in
  `VIEW-IMPLEMENTORS-GUIDE.md` §1.
- Filling in the table surfaced one more kind than T1's four-kind
  spot-check covered: `HorizontalRule`'s buffer is irrelevant —
  `serializeHorizontalRule` ignores content entirely and always emits
  `---`. Not a marker-convention question at all, but worth stating so
  nobody adds it to the "kept vs stripped" table by pattern-matching.
- Left uninvestigated, flagged instead: `serializeCodeBlock` always
  re-wraps `content` with fresh fences (`"```" + info + "\n" + content +
  "\n```"`), which would double the fences if `content` already carries
  them — and T1 says a *loaded* CodeBlock's buffer does. This is only
  survivable today because `serializeForSave`'s untouched-block fast
  path emits `blockLoadTimeBytes` verbatim and never calls the
  serializer for blocks nobody edited. Whether a *touched* code block
  round-trips correctly is untested and outside P1.4's docs-only scope
  — worth a real look before anything edits CodeBlock buffers in canvas
  (P4.6 syntax highlighting territory).

**P1.5 (2026-08-13) — phase close.**

- Full suite: **288/288 (100%)**, no drop from spike-close baseline —
  confirms P1.1's note that P1.1–P1.4 grew existing executables rather
  than adding new ones.
- `check-constitution.sh`: clean, 26 files scanned, C1–C4.
- Honest C1–C4 read (manual, not grep) over every file this phase
  actually touched — `git diff 603713c4..f6a28a5f` on
  `libs/markoff-canvas` + `libs/markoff-core`, 19 files: no re-entrance
  guard, no deferral (`singleShot`/`QMetaObject::invokeMethod` queued),
  no `QTextDocument`/QML text linkage, no cross-block byte arithmetic.
  `TextUnits::byteToQtPos/qtPosToByte` (P1.2) and `KindInference`
  (P1.1) are pure single-block functions; `promoteCaretBlockKind`/
  `updateCaretHeadingLevel` (P1.1) do all writes inside one
  `UndoLog::Transaction` scoped to the caret's block. Core's two new
  files (`KindInference`, `TextUnits`) aren't grep-scanned by
  `check-constitution.sh` (canvas-only script) — read directly instead;
  clean.
- Findings log: all four P1.x entries present and complete; nothing
  from this phase left unresolved needing an in-phase fix. Two items
  explicitly deferred on record rather than fixed unprompted: the
  `serializeCodeBlock` double-fence risk (P1.4, flagged for P4.6) and
  the third coordinate-helper consolidation
  (`SourceTextDocumentBinding::qtPosToByteOffset`, P1.2, flagged for
  P4.3).
- `docs/STATUS.md` baseline line updated to record the re-verification
  and Phase 1 close; workfront line points at Phase 2 next.

**P2.1 (2026-08-13).**

- New `ProjectionMap` (`src/ProjectionMap.{h,cpp}`): kept-run list built
  from a block's raw text + a set of omitted "full" QChar ranges,
  byte↔layoutQChar both ways. `BlockLayoutCache::rebuildInline()` (split
  out of the old `restyleInline()`) builds it fresh every time the entry
  rebuilds — caret move or content edit — from `omittedDelimiterRanges()`
  (`InlineFormatting.h`, the old `delimiterShouldHide` predicate
  unchanged, now generalized to take a cursor *set* per F1a). The old
  invisible-foreground-color format-range mechanism is gone entirely.
- **The snap-direction parameter is real for `layoutQCharToByte` but
  provably a no-op for `byteToLayoutQChar`/`fullQCharToLayoutQChar`.**
  Worked out by hand before trusting it: a hidden run has zero LAYOUT
  width by construction (kept runs concatenate back-to-back with no
  placeholder for what they omit), so the "run before" and "run after" a
  gap always share the exact same layout boundary — Left and Right
  necessarily compute the identical number. The direction only has
  observable effect on the REVERSE query (a given layout position that
  sits on a seam between two kept runs resolves to the *earlier* run's
  byte, "snap left," by construction of the search rather than a branch
  on `dir`). Kept the parameter on the forward functions anyway (API
  symmetry with the spec's own wording, and callers like the paint
  loop's selection-range code read correctly either way); documented the
  equivalence directly in `tst_canvas_projection` rather than asserting
  a divergence that doesn't exist.
- **Caret-motion routing through the entry's real (reveal-aware) layout
  is what makes "skip a hidden run in one press" true, and it falls out
  for free.** `moveCaretHorizontally`/`deleteCluster` used to build a
  disposable `QTextLayout` over the untransformed block text purely for
  `nextCursorPosition`/`previousCursorPosition`'s grapheme-boundary
  logic. Once hidden delimiters are actually removed from `e.layout`'s
  text, stepping through *that* layout instead automatically steps over
  them — no new skip logic needed, just routing through the cache's
  entry instead of a scratch layout. Verified by hand against the T7
  fixture (`a **b** c`) before trusting it: reveal already fires one
  keystroke *before* the caret would ever need to skip anything in this
  particular fixture (the reveal window is `[parentCharStart-1,
  parentCharEnd+1]`, and `parentCharStart` sits right at the delimiter's
  own start), so `tst_canvas_inline_formatting`'s existing 4-keystroke
  assertion needed no changes — a coincidence of that fixture's shape,
  not a general guarantee; a document where the caret has to cross
  several kept QChars *before* touching the reveal window would show
  the skip distinctly, which `tst_canvas_projection`'s ProjectionMap
  unit tests exercise directly instead of fighting pixel/keystroke
  choreography for it.
- Multi-cursor readiness (F1a): `omittedDelimiterRanges`/
  `inlineFormatRanges` take `QList<int> cursorsInBlock` throughout, one
  reveal check per span against the whole set. One member today
  (the caret, if it's in this block).
- Table cells (P2.3's scope) are untouched: `TableCell::layout` still
  holds raw, unomitted cell text — no projection there yet.
- New test file `tests/tst_canvas_projection.cpp`: a pure `ProjectionMap`
  unit layer (no `QApplication` needed — it's `QString` arithmetic) plus
  a `View`-integration reflow check via a new inspection accessor,
  `View::lineNaturalWidth(BlockId)` (mirrors `isDelimiterHiddenAt`'s
  existing "read the real layout state" pattern). Falsified: planted a
  `+1` in `fullQCharToLayoutQChar`'s in-run branch, confirmed both the
  unit test and the reflow test fail, reverted.
- Full suite: **289/289** (288 baseline + the one new executable).
  `check-constitution.sh` clean.

**P2.2 (2026-08-14).**

- **Headings needed zero canvas code.** Traced `TreeSitterParser.cpp`'s
  post-processing order by hand before writing anything: post-process 1
  gives every still-unset delimiter span inside a heading's byte range a
  parent range spanning the WHOLE heading line (`h.startByte`/`endByte`),
  and runs *before* post-process 2, which then overwrites that wide range
  with a narrow one for any delimiter that's actually part of an inline
  tree (e.g. a nested `**bold**` inside the heading text). The ATX marker
  span itself isn't part of any inline tree, so it's never touched by
  post-process 2 and keeps the whole-line range. Net effect: the
  existing per-span `touchedByAnyCursor` check (unchanged since P2.1)
  already implements per-block reveal for the marker, and still gives a
  nested emphasis span inside a heading its own correct per-span reveal
  — both for free, no special-casing. `tst_parser_inline_span_bake.cpp`'s
  `heading_marker_has_parent_range_without_trailing_newline` (pre-
  existing, unrelated regression guard) was the tell that pointed at this
  before any code was read.
- **Code fences DID need a real change**, and it's canvas-local:
  `fenced_code_block_delimiter`/`info_string`/`language` spans are never
  given a parent range at all (`collectParentRanges` only walks inline
  ASTs — fences aren't inline content), so the shared predicate always
  bailed out at the `parentCharStart < 0` guard and never hid them.
  `delimiterShouldHide` special-cases `isCodeBlockFence` directly:
  hidden unless `cursorsInBlock` is non-empty. No parser change, so
  markoff-live (frozen, bug-fix-only) is untouched — the two-flag
  distinction (`isHeading` gets a parser-level parent range;
  `isCodeBlockFence` doesn't) meant the "same mechanism" the plan
  promised only had to grow one new branch, not touch shared code.
- Code content's monospace font + background were already block-level
  presentation (`BlockPresentation.cpp`'s `CodeBlock` case), never
  inline spans — "code content keeps monospace + background" needed no
  work at all.
- New tests in `tst_canvas_inline_formatting.cpp` (grown, not a new
  executable): `heading_marker_hides_per_block` places the caret at the
  far END of the heading text (byte 7 of `"# Title"`, nowhere near the
  marker's own `[0,2)` span) and confirms it still reveals — the actual
  proof that reveal is per-block, not incidentally per-span-that-happens-
  to-be-wide. `code_fence_hides_per_block` mirrors it for a fenced code
  block. Falsified per the plan's own named target: pinned
  `span.isHeading` to always-visible, confirmed
  `heading_marker_hides_per_block` fails, reverted.
- Full suite: **289/289**, unchanged test count (grew an existing
  executable). `check-constitution.sh` clean.

**P2.3 (2026-08-14).**

- The "cell-ordered linear position sequence" the task asks for already
  existed: T9's `BlockLayoutCache::realizeTable` has always filled
  `tableCells` row-major (`e.tableCells[r * cols + c]`, byte ranges
  strictly increasing across the whole vector). P2.3's job was making
  selection/copy actually *walk* that sequence instead of treating a
  table block like any other — raw-byte-range dumping — which is what
  `View::selectedText()` was doing for a table caught inside a wider
  drag before this task.
- Per-cell `ProjectionMap`s (`BlockLayoutCache::TableCell::projection`)
  are an identity map today — no delimiter omission happens inside a
  cell yet — but they replace the ad hoc `coords::byteToQtPos`/
  `qtPosToByte` calls `hitTestTable`/`paintTable` used before, so cell
  coordinate conversion goes through the one sanctioned C4 path like
  every other block's does.
- New free helpers in `View.cpp`'s anonymous namespace:
  `cellIndexNear` (byte → nearest row-major cell index, gap bytes
  round to the nearest following/preceding cell per a `preferAfter`
  flag), `coveredCellRange` (byte range → `[lo, hi]` cell-index range —
  the table's contribution to a selection is **cell granularity**, the
  whole cell an endpoint lands in counts as covered, not the characters
  under it), `serializeTableCells` (covered cells, row-major,
  `" | "` within a row, `\n` between rows, each cell **trimmed** of the
  tokenizer's pipe-padding spaces — a raw substring would carry those,
  plus for any range spanning rows the alignment-row's leftover bytes).
  `View::selectedText()`/`paintTable()` (tint) both call through these
  instead of the raw byte-range path when `e.style.isTable`.
- Caret motion: only the "entering a table from an adjacent block's
  edge" half of `moveCaretVertically` changed — landing in row 0
  (nearest column by x) when crossing in from above, the last row when
  crossing in from below. Motion *inside* an already-caret-occupied
  table (row-to-row Up/Down, Left/Right, Backspace/Delete) is
  unaffected — it was already a no-op before this task (`e.layout` is
  null for tables; every per-block motion path guards on it) and stays
  one; that's explicitly P5.1's job ("Up/Down at cell edge moves rows,
  then exits the table"), not this task's — P2.3's own wording ("in/out
  … at top/bottom edges") reads as *entering* at either edge, not a
  request to build full in-table row navigation early.
- New test file `tests/tst_canvas_table_selection.cpp` (2 cases): a
  within-table cross-cell drag (header row excluded, both body rows
  covered) and a whole-table selection nested inside a wider
  before/after-paragraph drag — both assert the clipboard against the
  cell-serialized form, not a byte substring. Falsified per the task's
  own named target: reversed `realizeTable`'s per-row column fill order
  (`cols - 1 - c` in place of `c`) in a throwaway commit so the
  row-major vector no longer matched byte order; both new cases failed
  (`tst_canvas_table`'s existing single-cell-edit case, which never
  crosses cells, stayed green — a reminder that it alone would not have
  caught this class of bug), reverted.
- Full suite: **290/290** (289 baseline + the one new executable).
  `check-constitution.sh` clean.

**P2.4 (2026-08-14) — phase close, perf re-baseline.**

- `build-perf` = `-DCMAKE_BUILD_TYPE=Release`. Re-ran
  `tst_canvas_perf_500` unchanged: all four E9 budgets hold with the
  full P2.1–P2.3 projection-map/per-cell-map mechanism active, and
  with margin to spare — the projection work has not regressed the
  spike-era numbers:
  - load → first paint: **143 ms** (budget < 500 ms; NDEBUG-gated
    assertion, per the T10 finding — Release satisfies it).
  - p95 keystroke → paint: **0.734 ms** (p50 0.523 ms; budget < 16 ms),
    mid-document, plain-paragraph caret — same fixture as the spike.
  - scroll start → end realized: **58/500 = 11.6 %** (budget < 30 %).
  - RSS delta across the run: **0 KB** (budget < 100 MB).
- New `tst_canvas_perf_formatted` (`tests/tst_canvas_perf_formatted.cpp`):
  the 500-doc run's mid-document caret sits in a plain paragraph with no
  spans nearby, so its ProjectionMap is trivial (no omitted runs) — it
  never exercises the reveal/omission machinery on the hot per-keystroke
  path at all. This test types 200 keystrokes with the caret **inside**
  a revealed `**bold**` span in a single formatted paragraph that also
  carries a second (unrevealed) `*italic*` span, so the block's
  `ProjectionMap` carries real kept-run boundaries and rebuilds fully
  every keystroke (P2.1's restyleInline() note) for the whole run.
  Result: **p95 1.001 ms, p50 0.707 ms** (budget < 16 ms) — comfortably
  under, in both `build-perf` (Release) and `build-dev` (unoptimized;
  ran via `scripts/run-tests.sh -R canvas`, 14/14 green including the
  new test).
- No fix needed — all budgets pass with wide margin (p95 keystroke
  costs sit two orders of magnitude under budget in both the plain and
  formatted-paragraph cases), so nothing was profiled. Registered in
  `libs/markoff-canvas/tests/CMakeLists.txt` alongside the existing
  perf target, `QT_QPA_PLATFORM=offscreen` like every other canvas
  test. Phase 2 closes clean.

**P3.1 (2026-08-14).**

- `View` gained one new public method, `setCaretPosition(BlockId,
  byteOffset)` — the sanctioned way `EditorWidget` drives the caret
  programmatically, funnelled through the same private `setCaret()`
  every real-event path already uses (no second caret-mutation
  entrypoint). Clamps an unknown block to the last surviving one and
  the byte offset to the target block's length; clears the selection
  anchor, matching a real click.
- `EditorWidget::cursorPosition()`/`setCursorPosition()` implement the
  full flat-visual-line `CursorPos` mapping now, not a stub deferred to
  P3.2 — the task's own falsifiable "attach-window" check needs a real
  caret write/read to be meaningful, and the plan's P3.2 line reads as
  "grow this with signals + scroll", not "invent it from scratch". The
  mapping mirrors live's `toCursorPos`/`fromCursorPos` shape exactly,
  substituted to canvas's byte-offset caret via
  `TextUnits::byteToQtPos`/`qtPosToByte` against `blockText()` (never
  the layout string — P1.2's U+2028 substitution note). P3.2's
  remaining scope: `scrollPositionVisualLine()`/setter, and the
  `cursorPositionChanged`/`scrollPositionChanged` signal emissions
  (neither wired yet — `cursorPosition()` is a pull-only accessor here,
  same as it will be for scroll before P3.2).
- Added a new **shared** `ViewContractChecks.h` check,
  `checkAttachWindowCaretWriteSurvives` — general over any
  `MarkdownView*`, not canvas-specific, so live/source/styled can adopt
  it too (not required by this task; left for their own maintainers).
  It pumps `QCoreApplication::processEvents()` between the write and
  the read specifically so it can catch a regression that defers part
  of `setDocument()`'s caret handling onto the event queue — for a
  fully synchronous leaf (canvas today) the pump is a no-op, but it's
  what makes the check load-bearing rather than merely "nothing raced
  *yet*". Falsification planted exactly that: `setDocument()`'s
  `m_view->setDocument(doc)` call deferred via
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. Confirmed
  both `attach_window_caret_write_survives` AND `cursor_round_trip`
  fail (the latter because `init()` itself relies on the same
  synchronous-attach property) — reverted clean.
- `checkContextChangedKindGated` /
  `checkContextChangedOnStructuralKindChangeWithoutCaretMove` are
  deliberately **not** enrolled yet — `EditorWidget` doesn't emit
  `contextChanged` until P3.5. `checkReadOnlyBlocksUndoAndKeepsBytes`
  and `checkFontScaleSignal` **are** enrolled and pass today, but only
  by exercising the base class's own storage (`setReadOnly`/
  `isReadOnly`, `setFontScale`/`fontScaleChanged`) — `EditorWidget`
  hasn't overridden either yet (P3.3, P3.5 respectively). Left enrolled
  rather than held back: they cost nothing today and start testing real
  behavior the moment those overrides land, with no separate
  enrollment step to remember.
- New executable `tst_canvas_view_contract` (9 test slots, all green)
  registered in `tests/CMakeLists.txt` alongside the existing canvas
  tests. `-R canvas` now 16/16 green (constitution included); full
  suite green (STATUS.md's 288/288 baseline line is a phase-close-only
  update per the session protocol, left for P3.7).

**P3.2 (2026-08-14).**

- `View` gained one new signal, `caretChanged()` — emitted
  unconditionally from `ensureCaretVisible()`, the file's own
  documented chokepoint every caret-changing code path (mouse,
  keyboard, IME, structural keys) already calls afterward.
  `onDocumentChanged()` did NOT previously call it (only the
  interactive code paths did) — a document-driven caret clamp
  (`clampCaret`, e.g. a remote edit or undo/redo landing the caret on
  a different surviving block) went unnoticed and didn't scroll-follow.
  Added the call there too, so `EditorWidget::cursorPositionChanged`
  and the caret's scroll-into-view now fire for that path as well —
  a small, in-scope completion of the chokepoint the file already
  claimed to have, not a new invention.
- `caretChanged()` is deliberately unconditional (no "did it really
  move" check inside `View`); `EditorWidget::onViewCaretChanged()` does
  the real change-gating in `CursorPos` space against
  `m_lastCursorPos`, the coordinate space the base contract's
  `cursorPositionChanged(int line, int column)` is actually in. Two
  gating layers (View's "something might have changed", EditorWidget's
  "did the reported position actually change") rather than one is
  intentional: `View`'s internal caret representation and the flat-line
  `CursorPos` model are different spaces, and only the latter is what a
  consumer cares about.
- `scrollPositionVisualLine()`/`setScrollPositionVisualLine()`/
  `scrollPositionChanged` land on the same "fraction of
  `verticalScrollBar()`'s range" convention `Source::Editor` and
  `Styled::Editor` already use (confirmed by reading both before
  writing this — `libs/markoff-source/src/Editor.cpp:218-235` is the
  closest reference; canvas's `View` is already a `QAbstractScrollArea`
  tracking a real pixel-space scrollbar, so no new scroll-state needed
  inventing). The plan's "estimated-height correction" phrase turned
  out to need no separate mechanism: `View::updateScrollRange()`
  already re-derives the scrollbar range (and re-pins a bottom-parked
  view) every time block realization corrects an estimated height, so
  `scrollPositionVisualLine()` reading the scrollbar's current
  value/maximum picks up that correction for free.
- Falsification finding: `QAbstractSlider::setValue()` (which
  `QScrollBar` inherits) already clamps its argument to
  `[minimum(), maximum()]` internally, so the `qBound(0.0f, pos, 1.0f)`
  guard added to `setScrollPositionVisualLine()` is provably redundant
  on the `maximum() != 0` branch — removing it as a first falsification
  attempt produced **zero** test failures (verified, not assumed; see
  process notes below). Kept the `qBound` anyway (defense-in-depth,
  cheap, and it's what makes the function's contract legible without
  reading `QAbstractSlider`'s docs) but retargeted the falsification
  commit at the cursor clamp instead — see checklist SHAs. Worth a
  maintainer's-note-to-self: any future "skip the clamp" falsification
  task on this scrollbar-backed setter needs a different target (e.g.
  the `maximum() == 0` explicit-emit branch), not the `qBound`.
- `checkCursorPositionChangedSignal` added to the shared
  `ViewContractChecks.h` (general over any `MarkdownView*`, same shape
  as `checkFontScaleSignal`) and enrolled here alongside two
  canvas-local slots (`set_cursor_position_out_of_range_clamps`,
  `scroll_position_changed_fires_on_set` /
  `scroll_position_round_trip_and_clamps` — the latter needs a real
  shown/resized widget since `View`'s scrollbar range is 0 until it has
  laid out against a real viewport size, so it's leaf-local rather than
  shared). `-R canvas` 15/15 green (constitution included); full suite
  292/292 green (up from the 288/288 baseline recorded at arc open —
  net +4 from this task's new checks).

---

**P3.3 (2026-08-14).**

- `View` gained the read-only authority itself (`setReadOnly`/
  `isReadOnly`, `m_readOnly`) — the base `MarkdownView`'s own store
  wasn't wired to anything the composed `View` reads, same gap P3.2's
  cursor/scroll work found and closed for position mapping.
  `EditorWidget::setReadOnly` now overrides to forward: base store
  first (keeps `isReadOnly()`/`hasEditing()` coherent for a caller
  reading the base pointer), then `View::setReadOnly` (the actual
  gate authority), mirroring `Live::EditorWidget::setReadOnly`'s
  documented pattern exactly.
- Six-gate table, mapped onto what actually exists in this leaf today:
  - **Real gates** (block on `m_readOnly`, all in `View.cpp`):
    printable insertion; `StructuralKeyHandler` (Enter split, boundary
    Backspace/Delete merge, Tab/Shift+Tab list indent — Tab/Backtab
    added to the read-only key set even though the pre-existing
    `isMutatingKey` local deliberately excludes them, since that
    local also gates the *selection-collapses-first* branch and
    Tab-with-a-selection isn't "collapse then type" today); the
    Undo/Redo keyboard shortcut (`Ctrl+Z`/`Ctrl+Shift+Z` apply
    `undoD2`/`redoD2` directly in `keyPressEvent`, bypassing
    `MarkdownView::undo()`/`redo()` entirely — that base-level no-op
    while read-only does NOT cover this path, so it needed its own
    check, found by reading the base's doc comment against what
    `View::keyPressEvent` actually does rather than assuming the
    inherited behavior reached here); Cut (`Ctrl+X`) — disabled in
    its *entirety* while read-only, including the copy-to-clipboard
    half, mirroring Qt's convention of disabling the whole Cut
    `QAction` rather than only its mutating half (Copy keeps its own
    branch, unaffected); IME commit — the task's named falsification
    target, `inputMethodEvent` now returns immediately while
    read-only, disabling composition outright rather than letting
    preedit run and only blocking the eventual commit (a composition
    that can never commit is worse UX than none, and matches Cut's
    "disabled in its entirety" choice above).
  - **Placeholder / not yet real** (nothing to gate because the
    feature doesn't exist in this leaf yet): checkbox toggles (P4.7)
    and format verbs via action enabled-state (P4.3, no
    `CanvasActionController` yet — `MarkdownView`'s default
    `toggleBold()`/etc. no-ops are inert regardless of read-only
    state). Paste is *also* not implemented at all yet (no `Ctrl+V`
    handling anywhere in `View.cpp` — deferred to P4.4's context menu
    and P7.2's drag-drop/middle-click paste per the plan's own task
    list), so it isn't a "gate" so much as "nothing to gate" — noted
    here so a future paste implementation knows to add the
    `m_readOnly` check at the same time it lands, not after.
- `caretRect()`: `View::caretRectInViewport()` is a new private helper
  factoring out math `inputMethodQuery(Qt::ImCursorRectangle)` already
  had (moved, not duplicated — `inputMethodQuery` now just calls it,
  no behavior change there). `View::caretRect()` translates that into
  View's own local frame via `viewport()->mapTo(this, ...)`;
  `EditorWidget::caretRect()` does the same one more hop
  (`view()->mapTo(this, ...)`) rather than assuming the zero-margin
  `QVBoxLayout` keeps the frames identical — correct even if that
  layout ever grows a margin. Known gap, inherited from
  `inputMethodQuery` (not introduced by this task): table-cell carets
  (T9) aren't special-cased, so `caretRect()` while the caret is
  inside a `Table` block shares whatever `inputMethodQuery` already
  did there (untouched by this task; not a new limitation).
- Contract harness: `checkReadOnlyBlocksUndoAndKeepsBytes` was already
  enrolled (P3.1) but only exercised `undo()`/`toggleBold()` through
  the base `MarkdownView` virtuals — it didn't reach any of the real
  gates above (those are `View`'s own keyboard/mouse/IME event
  handlers, a different ingress entirely). Left it enrolled as-is (it
  now exercises the real `EditorWidget::setReadOnly` forward, which
  it didn't before this task) and added
  `set_read_only_forwards_to_composed_view` (pins the
  `EditorWidget`→`View` wiring specifically) and
  `caret_rect_is_valid_and_in_widget_bounds` to
  `tst_canvas_view_contract.cpp`. No new *shared* check was needed in
  `ViewContractChecks.h` for the gates themselves — they're
  `View`-specific event-handling behavior, not something generic over
  any `MarkdownView*`, so they're canvas-local tests instead:
  `tst_canvas_ime.cpp` (`read_only_blocks_ime_commit` — the named
  falsification target), `tst_canvas_typing.cpp`
  (`read_only_blocks_printable_and_backspace_but_not_navigation`),
  `tst_canvas_selection.cpp` (`read_only_blocks_cut_but_not_copy`).
- Falsification: removed `inputMethodEvent`'s read-only early-return
  (the IME-commit gate) — `tst_canvas_ime::read_only_blocks_ime_commit`
  failed as expected (`'!view.isComposing()' returned FALSE`), other
  four gates' tests were unaffected (each has its own independent
  early-return, so this falsification only exercises the one target
  named in the task). Reverted; `-R canvas` and the constitution check
  both green again after the revert. `check-constitution.sh`: clean
  (C1–C4) throughout. Full suite: **292/292** green, unchanged from
  P3.2's count (this task added tests but touched no new test
  *files*, so the total count didn't move — the new slots landed
  inside already-counted `.cpp` files).

---

**P3.4 (2026-08-14).**

- `View` gained a leaf-local `FindHighlight { BlockId; int byteOffset;
  int byteLength; bool isCurrent; }` and `setFindHighlights(QList<FindHighlight>)`
  — deliberately **not** `Markoff::FindController::Match`: `View`
  stays ignorant of `FindController` entirely (same separation as
  `CanvasCursor`/selection), so `EditorWidget::attachFindController`
  is the only place that translates the controller's shape into the
  view's own idiom. Storage is `QHash<BlockId, QList<FindHighlight>>`
  grouped at `setFindHighlights` time, for O(1) per-block lookup in
  `paintEvent` rather than an O(matches) scan per visible block.
- Paint mechanism confirmed draw-time `QTextLayout::FormatRange`,
  reusing the exact list `paintEvent` already builds for selection
  (`e.layout->draw(&p, pos, selections)`) — find highlights are
  appended to the same `selections` vector after the selection range,
  through the same `ProjectionMap::byteToLayoutQChar` snap-left/
  snap-right byte→QChar mapping selection already uses. No
  `QTextCharFormat`/`setFormats` mutation of the layout anywhere;
  `check-constitution.sh` stayed clean throughout (C1–C4 unaffected —
  this task didn't touch any of the four; the "draw-time not
  setFormats" rule is a spec §3 architectural discipline, not one of
  the grep-checked C-rules). Current-match vs other-matches use the
  two dedicated slots that already existed in `Theme::Slot`
  (`SearchMatchBackground` / `SearchActiveMatchBackground` — no new
  slot needed. Table-cell matches are a known gap: `paintTable` never
  consults `m_findHighlightsByBlock` (same limitation selection
  already has there, T9 deferred to P5.1/P5.2) — noted in both the
  `setFindHighlights` doc comment and here so a future table-editing
  task knows to wire it up, not rediscover the gap.
- `EditorWidget::attachFindController`/`detachFindController`:
  subscribes `matchesChanged` + `currentMatchChanged` to a single
  `rebuildFindHighlights()` (whole-list rebuild from
  `m_findController->matches()` + `currentMatchIndex()` — cheap
  relative to a keystroke, one source of truth, no incremental diffing
  to get wrong) and `navigationRequested` to
  `onFindNavigationRequested`. `m_findController` is a `QPointer`
  (consumer-owned, never linked) mirroring live's `LiveFindAdapter`
  pattern read as reference before writing this (never copied — no
  `FindSpan`-equivalent type needed here since `View` already had a
  draw-time highlight mechanism live doesn't). `detachFindController`
  disconnects and calls `setFindHighlights({})` — the empty-list path
  already clears `m_findHighlightsByBlock` via `.clear()`.
- Navigation turned out to need **zero new plumbing**: `View::
  setCaretPosition(BlockId, int)` (P3.1's sanctioned programmatic
  caret entrypoint) already routes through `setCaret()` →
  `ensureCaretVisible()` (P3.2's chokepoint), which already scrolls
  the target block into the viewport AND emits `caretChanged()` (which
  `EditorWidget` already turns into `cursorPositionChanged`). Neither
  function touches `QWidget::setFocus`, so
  `FindController::navigationRequested`'s documented contract ("MAY
  scroll + place the caret, MUST NOT take focus") holds for free — no
  focus-suppression logic had to be written or verified against a race,
  it simply isn't there. `onFindNavigationRequested` is therefore a
  one-line forward: `m_view->setCaretPosition(match.block,
  int(match.byteOffset))`. `Markoff::BlockAnchor` (the `Match::block`
  field's type) is confirmed a plain alias for `BlockId`
  (`markoff/core/BlockAnchor.h`), not a distinct type needing
  resolution — no lookup step exists here the way live's
  `resolveByteToQtPos` needs one (canvas's caret coordinate space
  already *is* block+byte, live's is a ListView row + QChar position).
- New `tst_canvas_find.cpp` (3 tests, all green): match/current-match
  highlight placement + movement on `findNext()`, navigate places the
  caret and scrolls without stealing focus (asserted via
  `QWidget::hasFocus()` before/after — the widget can genuinely hold
  focus from an earlier `show()`, so the check compares state across
  the navigate call rather than assuming focus is always false),
  detach clears highlights and stops reacting to a controller left
  running. Registered in `tests/CMakeLists.txt`. `-R canvas` 16/16
  green (constitution included).
- Falsification: commented out the `currentMatchChanged` connection in
  `attachFindController` (throwaway commit `de9920ef`) —
  `matches_and_current_match_paint_highlights` failed as expected
  (`'!h0b.first().isCurrent' returned FALSE`: the current-match
  highlight never moved off the first match after `findNext()`); the
  navigate test stayed green (it only exercises `navigationRequested`,
  a separate connection — expected, not a gap). Reverted (`c5a22a58`);
  `-R canvas` 16/16 green again after the revert.
- Test tier: canvas-scoped only per the session protocol (diff stays
  entirely inside `libs/markoff-canvas/`, no core seam touched —
  `FindController` itself is pre-existing, unmodified). Full suite not
  run for this task.

---

**P3.5 (2026-08-14).**

- `contextChanged`: `EditorWidget::recomputeContext()` reads the
  composed `View`'s caret block (`caretBlock()`), maps kind→
  `EditorContext::blockKind` and heading level (same switch/attr-read
  shape as live/source's `recomputeContext()`, read as reference), and
  is called from two places — `onViewCaretChanged()` (every
  `View::caretChanged`, P3.2's chokepoint) and a
  `structuralEditSequence`-gated `d2DocumentChanged` connection wired
  in `setDocument()` (Queue #15: a programmatic `Cmd::changeKind` on
  the caret's own block that never moves the caret). Both shared
  checks (`checkContextChangedKindGated`,
  `checkContextChangedOnStructuralKindChangeWithoutCaretMove`) were
  already sitting in `ViewContractChecks.h`, added ahead of need by
  P3.1/P3.2 — no core change needed, just enrollment.
- **Table row/col is real, not the live leaf's `(-1, -1)` contract
  minimum.** Live has no stable C++ owner for a QML delegate's
  focused cell; canvas's `View` owns table layout directly
  (`BlockLayoutCache::Entry::tableCells`, row-major, P2.3), so a new
  `View::caretTableCell()` reuses the exact row-major cell-index
  lookup (`cellIndexNear`, P2.3's own helper) selection/hit-testing
  already use — not a second table-position scheme. Returns
  `std::optional<std::pair<row, col>>`; `std::nullopt` if the caret
  isn't in a table block or the table isn't realized yet (row/col
  derivation needs `tableCells`, built at realize time). `row 0` is
  the header row, same convention as `tableCellRect()`.
- **Ordering finding (attach-window interaction with the sentinel):**
  `EditorWidget`'s permanent ctor-time `View::caretChanged` connection
  fires *synchronously* during `setDocument()`'s internal caret reset
  (the same C2 chokepoint every real move uses — canvas has no queued
  step anywhere to hide behind). live/source's contract text reads
  "the FIRST cursor movement after `setDocument()` always emits"; on
  live/source that holds for free because their cursor-move connection
  is wired fresh *inside* `setDocument()`, after the sentinel reset, so
  nothing can fire before it. Canvas's connection is permanent and
  fires *during* `setDocument()`, before the sentinel-consuming
  `if (doc) {...}` block that wires `m_contextD2Con` even runs —
  without a guard this silently consumes the fresh-document sentinel
  during attach, so the *test's own* first post-attach move (landing
  on the same block kind by coincidence, as the shared fixture's
  clamp-to-last-line does) sees no change and never emits, breaking
  `checkContextChangedKindGated` populated by `init()` before the
  test's spy attaches. Fixed by guarding `recomputeContext()` on
  `m_contextD2Con` being valid (only true once `setDocument()` has
  finished wiring the current document) — not a re-entrance flag on
  shared mutation state (C1's actual target), just reusing an
  already-existing piece of readiness state rather than inventing a
  new boolean for the same fact. `cursorPositionChanged` doesn't hit
  this class of bug: the same eager fire's resulting `CursorPos`
  happens to equal `m_lastCursorPos`'s `{1,1}` default (a fresh
  document's caret always lands on block 0/byte 0 = flat line 1,
  column 1), so it was already a coincidental no-op before this task
  touched anything.
- `setTheme`/`setFontScale`: both now override to push base-store-first
  into the composed `View` (mirrors `setReadOnly`'s P3.3 pattern).
  `View` already had `setTheme` from the spike (clears the cache +
  resyncs, so it needed no new relayout mechanism); `setFontScale` on
  `View` is new this task. `BlockPresentation::fontForSlot`/
  `presentationFor` gained a `fontScale` parameter (default 1.0,
  multiplies every slot's pixel size) threaded through
  `BlockLayoutCache::sync` — the ONE call site that ever calls
  `presentationFor` (styles are computed once per entry and cached in
  `Entry::style`, reused by `realize()`/`realizeTable()`, so this is
  the only seam that needed touching, not every layout call).
- **Scroll re-anchoring needed a two-pass correction, not one.**
  `View::setFontScale` captures the block at `indexAtY(scrollbar
  value)` before `clear()`, resyncs at the new scale (estimated
  heights only — `BlockLayoutCache::estimateHeight()` counts newlines,
  no wrap simulation), sets the scrollbar to that block's *estimated*
  new `y`, then calls `ensureLayoutForViewport()` to realize the
  visible range. First attempt stopped there and the scroll-anchor
  half of the falsifiable test failed at the 1px tolerance: realizing
  blocks *before* the anchor corrects their estimated heights (real
  layout height rarely equals the newline-count estimate exactly),
  which shifts every later `y` via the prefix sum — including the
  anchor's own — AFTER the scrollbar had already been set from the
  pre-realization estimate. Fixed by re-reading the anchor's `y` and
  re-setting the scrollbar a second time after realization, then
  realizing once more (same "fixed-point, not one-shot" reasoning
  `ensureLayoutForViewport()`'s own doc comment already documents for
  its own loop). Deliberately NOT `ensureCaretVisible()`: that would
  re-scroll to the caret's block, which is not necessarily the block
  that was at the viewport's top.
- New tests in `tst_canvas_view_contract.cpp`:
  `context_reports_table_row_and_col` (real row/col, not contract
  minimum — see above) and
  `font_scale_triggers_relayout_and_anchors_scroll` (the task's named
  falsifiable check: asserts the anchor block's real height actually
  grew under the larger font AND that the anchor's top ends up back at
  the viewport's top edge after the scale change). `-R canvas` 16/16
  green (constitution included).
- Falsification: `View::setFontScale` short-circuited to store the
  scale and `return` before any cache invalidation (throwaway commit
  `93795083`) — `font_scale_triggers_relayout_and_anchors_scroll`
  failed exactly on the relayout half
  (`'heightAfter > heightBefore * 1.2' returned FALSE`: the block's
  height never changed under the "larger" font). Reverted (`c4278364`);
  `-R canvas` 16/16 green again after the revert, `check-constitution.sh`
  clean (C1–C4) throughout.
- Test tier: canvas-scoped only per the session protocol (diff stays
  entirely inside `libs/markoff-canvas/`, no core seam touched — the
  two shared checks enrolled were already present in
  `ViewContractChecks.h` from P3.1/P3.2, unmodified this task). Full
  suite not run for this task.

---

**P3.6 (2026-08-14).**

- Neither `MarkdownView` (core) nor any leaf had `saveEphemeralState`/
  `restoreEphemeralState` before this task — the "check what live does"
  instruction found nothing to mirror (grepped `EphemeralState`/
  `ephemeral` across all four leaves + core: zero hits besides an
  unrelated `Session` comment and a foundation test). Corbomite's own
  `Corbomite::EphemeralState` (`libs/storage/include/corbomite/storage/
  EphemeralState.h`) is the actual consumer the plan's spec §5 line
  references — but it's Obsidian's `{mode, source}` + flat `{line,
  column}` cursor shape, built externally from the base contract's
  existing `cursorPosition()`/`scrollPositionVisualLine()` accessors,
  not from a per-leaf JSON method. So this task adds the virtuals to
  `MarkdownView` itself (P3's own "contract v2" pattern — P3.1–P3.5 each
  added a virtual the same way) rather than reusing Corbomite's schema
  verbatim: canvas's native coordinate space is block-index + byte, not
  line/column, and the task text names that shape explicitly.
- Schema landed exactly as specced: `{"scroll": {"blockIndex",
  "fraction"}, "cursors": [{"blockIndex", "byte"}], "folds": []}`.
  `cursors` is confirmed a JSON ARRAY of one element (F1a), not a bare
  object — pinned directly by `ephemeral_state_schema_shape`
  (`QCOMPARE(cursors.size(), 1)`). Block **index**, not raw `BlockId`,
  in both scroll and cursor — an index survives detach/reattach (the
  task's own required test) in a way a raw id cannot: a reattached
  document's cache mints fresh internal state, but document-order
  position is stable. `View` grew the index↔id conversion
  (`blockIndexOf`/`blockIdAt`) and the scroll-anchor pair
  (`scrollAnchor`/`setScrollAnchor`) needed to read/write that space —
  reusing P3.5's exact "block at the top of the viewport" concept
  (`indexAtY(verticalScrollBar()->value())`), generalized with a
  within-block fraction (P3.5's fontScale re-anchor snaps to a block's
  top edge only; this task's precision requirement needed more).
- **Real bug found by the round-trip test itself, not invented for the
  falsification:** the first working version restored scroll, then
  cursor — and failed its own in-place round-trip test by ~126px.
  Cause: `View::setCaretPosition` (the cursor-restore call) routes
  through `ensureCaretVisible()`, which auto-scrolls the caret's block
  into view if it isn't already visible; restoring scroll first and
  cursor second let that auto-scroll silently clobber the just-restored
  scroll position whenever the saved caret block didn't happen to sit
  inside the saved scroll viewport (a legal, independent combination —
  this schema deliberately saves the two separately). Fixed by
  restoring cursor BEFORE scroll, so the explicitly-saved scroll always
  wins. Debugged by dumping the same block's `blockRect()` at save vs.
  restore time (identical — ruling out an estimate-vs-real-height
  drift, P3.5's own known failure mode for scroll math) before finding
  the actual cause in call order.
- **This bug became the falsification target**, in place of the task
  text's suggested "skip restoring cursor position" — a real regression
  the test caught, planted deliberately (reverting the restore order
  back to scroll-then-cursor), confirmed
  `ephemeral_state_round_trip_in_place` fails
  (`'qAbs(... - savedScrollValue) <= 2' returned FALSE`) while every
  other slot in the file stays green, then reverted.
- Missing/malformed JSON: every sub-key read is individually
  `isDouble()`/`isObject()`/`isArray()`-guarded and skipped (not
  fatal) on mismatch — `ephemeral_state_restore_handles_malformed_json`
  round-trips an empty object and a blob with all three top-level keys
  present but wrong-typed, confirming no crash and no unintended
  mutation.
- Base `MarkdownView::saveEphemeralState()`/`restoreEphemeralState()`
  default to an empty object / no-op (same "inert until a leaf
  overrides" shape as `caretRect()`'s `{}` default) — live/source/
  styled inherit that default unchanged; adopting the real schema for
  those leaves is out of this task's scope (canvas-only, per session
  protocol) and not attempted.
- New tests in `tst_canvas_view_contract.cpp`: `ephemeral_state_schema_
  shape`, `ephemeral_state_round_trip_in_place`, `ephemeral_state_
  round_trip_through_detach_reattach` (real `setDocument(nullptr)` +
  `setDocument(doc)` cycle, per the task's explicit wording — not just
  an in-place restore), `ephemeral_state_restore_handles_malformed_
  json`. `-R canvas` 16/16 green (constitution included); full suite
  **293/293** (288 baseline + 5 new slots, no regressions — checked
  because this task touched `markoff-core/MarkdownView.h`, a shared
  seam every leaf compiles against, not just canvas). `check-
  constitution.sh`: clean (C1–C4).
- Test tier: full suite run this task (not canvas-scoped-only) because
  the diff crosses into `markoff-core`'s `MarkdownView.h` — a seam
  live/source/styled all depend on — even though only canvas overrides
  the new virtuals.

---

**P3.7 (2026-08-14) — phase close.**

- Full suite: **293/293 (100%)**, no drop from P3.6's count — confirms
  P3.7 itself added no new executables (it's a checkpoint task).
- `check-constitution.sh`: clean, 35 files scanned, C1–C4.
- Honest C1–C4 read (manual, not grep) over every file P3.1–P3.6
  actually touched — `git diff 3a7a884a..8a8390be` (P2.4's phase-close
  commit to P3.6's), 18 files across `libs/markoff-canvas` +
  `libs/markoff-core`: no re-entrance guard (`m_lastCursorPos`/
  `m_lastContext`/`m_lastStructuralSeq` in `EditorWidget` are
  documented change-gate caches, not reaction-suppression flags — each
  has a doc comment distinguishing it from a C1 guard); no deferral
  (`singleShot`/`QueuedConnection`/`callLater`) anywhere in the diff;
  no `QTextDocument`/`QPlainTextEdit`/`QTextEdit`/Quick-text linkage;
  no cross-block or flat byte arithmetic (`EditorWidget`'s
  `toCursorPos`/`fromCursorPos` walk blocks to build a flat *line*
  count for the base contract's `CursorPos`, never a byte offset —
  the per-block byte space stays block-relative throughout). Find
  highlights (P3.4) confirmed draw-time `QTextLayout::FormatRange`
  pushed into the same `selections` vector selection already uses in
  `paintEvent` — no `setFormats` mutation of any layout anywhere in
  the diff (grepped and hand-checked; the two `setFormats` hits in the
  diff are doc-comment prose warning against it, not code). Clean —
  nothing found, nothing fixed.
- Findings log sweep: read all six P3.x entries end to end. One loose
  thread carried forward explicitly (no fix in this session — it
  predates P3 and isn't this phase's to fix): `View::caretRect()` /
  `caretRectInViewport()` (P3.3) doesn't special-case a caret inside a
  Table block (T9's pre-existing gap, inherited via
  `inputMethodQuery`'s `Qt::ImCursorRectangle` case, not introduced by
  P3.3) — same table-caret limitation P3.4's find-highlight painting
  already carries forward to **P5.1/P5.2** (table cell wrap +
  navigation); folded into the same pointer rather than a separate
  one, since both gaps close together once tables get real per-cell
  layouts. Everything else in the six entries either resolved fully
  within its own task or already carries an explicit forward pointer
  (P4.3: `FormatOps`-adjacent coordinate-helper consolidation,
  checkbox/action-enabled-state gating; P4.4/P7.2: paste; P4.6:
  `serializeCodeBlock` double-fence risk, carried from P1.4). No
  action items dropped.
- `docs/STATUS.md` baseline updated to **293/293**; also backfilled
  the Phase 2 close banner it had missed (P2.4 closed 2026-08-14 but
  the board was never touched — caught during this sweep, not a P3.x
  regression). Workfront line now points at Phase 4 (P4.1) next.
- Phase 3 (MarkdownView contract v2) closes clean. Next: **P4.1** —
  full inline kind set.

---

**F1 — CodeMirror parity audit (2026-08-13).** Source: `~/src/codemirror`
(`view`, `state`, `commands`, `lang-markdown`, `search`, `autocomplete`,
`language`, `basic-setup`). Nothing implemented; §5.3 edits proposed below.

*Corrections to the spec as written:*

- §5.3 says "audit task **P1.1** verifies against the CodeMirror
  checkout" — the audit is **F1**. Fixed in the spec.
- §5.3's list is a *rendering* benchmark. Obsidian's Live Preview is
  CodeMirror 6 with `basic-setup`-shaped extensions, so the real parity
  surface also includes an **editing-command floor** and **local
  multi-cursor**, neither of which appears anywhere in the spec. Those
  are the two findings that change the arc's shape; the rest are small.

*Gaps, ranked. (CM refs are file:symbol in the checkout.)*

| # | Gap | CM / Obsidian reference | Our status | Proposal |
|---|---|---|---|---|
| 1 | **Editing-command floor.** Word-wise motion + selection, word-wise delete, Ctrl+Home/End, delete-line, move/copy line up-down, select-line, Esc→simplify | `commands/src/commands.ts` `defaultKeymap` (l.1040–1138): `cursorGroupLeft/Right`, `deleteGroupBackward/Forward`, `cursorDocStart/End`, `deleteLine`, `moveLineUp/Down`, `copyLineUp/Down`, `selectLine`, `simplifySelection`. Obsidian binds most of these | Canvas has arrows/Home/End/PageUp-Down/Backspace/Delete/Ctrl+A,C,X,V,Z **only** | New spec §5.2 row + a P4 task. Word boundaries: `state/src/charcategory.ts` maps to `QTextBoundaryFinder`, not hand-rolled |
| 2 | **Local multi-cursor.** Add cursor above/below, Alt+click, select-next-occurrence, rectangular selection, Esc to collapse | `commands`: `addCursorAbove/Below`; `view/src/rectangular-selection.ts`, `clickAddsSelectionRange`; `search/src/selection-match.ts` | Absent. Caret is one `{BlockId, byte}` + one anchor; `Session::secondarySelections` exists but is spoken for by *remote* presence (P6.2) | **Decide explicitly.** Recommend: painting comes free with P6.2 (same draw-time path); the *editing* half multiplies every mutation ingress and should be its own arc. Either way it belongs in §5.3 as scope or in the deferred list — silence is the wrong answer |
| 3 | **Canvas undo is per-keystroke.** | `commands/src/history.ts` `newGroupDelay: 500` | Core already coalesces (`UndoLog::maybeCoalesceOrTransaction`: 1000 ms, same block, printable-only) and live gets it via `Cmd::insertCharacter` — but `View::insertPrintable` opens a bare `Transaction` per key, bypassing it | Straight defect, no core change: route canvas typing through `Cmd::insertCharacter`. Fold into a P4 task |
| 4 | **Auto-pairing / wrap-selection.** Typing `(`/`[`/`"`/`` ` ``/`**` with a selection wraps it; typing the closer over an auto-inserted one types through; Backspace between a fresh pair deletes both | `autocomplete/src/closebrackets.ts` (`insertBracket`, `deleteBracketPair`, `closeBracketsKeymap`) | Absent from spec and code | Add to §5.3; sized as one P4 task. Note CM tracks auto-inserted closers in a `StateField` — for us that is *view* state and needs a §2 justification, or derive it from the undo entry |
| 5 | **Markdown Enter/Backspace semantics are a spec, and we should test against it.** Enter continues list/quote markers, renumbers ordered lists, and outdents one level on an empty item; Backspace at content start replaces the marker with blank space or removes one indent level instead of merging blocks | `lang-markdown/src/commands.ts`: `insertNewlineContinueMarkup` (l.98), `deleteMarkupBackward` (l.240) | `StructuralKeyHandler` covers Enter-split / boundary-merge / list Tab; `Cmd::renumberRunStartingAt` exists. Coverage vs. these two commands is **unverified** | Not a spec change: a checklist test in P4/P5 diffing our structural keys against these two commands' documented cases |
| 6 | **Atomic-range skipping has three ingresses, not one.** | `view/src/cursor.ts` `skipAtoms`; applied at `editorview.ts:680` (char), `:687` (group), `:721` (vertical) **and** `domchange.ts:179` for selections derived from pointer/DOM | P2.1's snap rule is written for byte→layout queries | Tighten P2.1's done-when: hit-test (mouse) and vertical motion must snap too, not just horizontal stepping |
| 7 | Highlight other occurrences of the selection | `search/src/selection-match.ts` `highlightSelectionMatches` (`minSelectionLength`, `wholeWords`) | Absent | Add to §5.3 as optional; cheap once P3.4's match painting exists |
| 8 | Scroll past end | `view/src/scrollpastend.ts` — bottom padding = viewport − one line | Absent | §5.3 one-liner; trivial in our y-layout |
| 9 | Invisible/control-character rendering | `view/src/special-chars.ts` `Specials` — C0/C1, U+00AD, U+200B, U+200E/F, **U+202D/E and U+2066–9 (bidi overrides)**, U+FEFF | We substitute U+2028 for `\n` and nothing else | §5.3 addition. The bidi-override set matters beyond cosmetics — invisible RTL overrides in a note are a spoofing surface |
| 10 | Empty-document placeholder; drop-cursor during drag; bracket-match highlight | `view/src/placeholder.ts`, `dropcursor.ts`, `language/src/matchbrackets.ts` | Absent | §5.3 minor list; drop cursor folds into P7.2 |
| 11 | Fold state is serialized as part of editor state | `language/src/fold.ts:129` `foldState.toJSON/fromJSON` | P3.6 already reserves the fold key | No change — confirms the design |
| 12 | Gutter is a general seam, not a fold detail | `view/src/gutter.ts` (line numbers, fold markers, active-line gutter) | P5.6 paints "a fold affordance in the gutter margin" | Suggest P5.6 build a small gutter seam rather than a fold-only affordance; line numbers then cost nothing |

*Not gaps (checked, deliberately staying out):* active-line highlight
(Obsidian doesn't), lint, autocompletion UI (Corbomite owns it via
`CompletionRegistry`), indent-on-input for code, `basic-setup`'s line
numbers by default.

**F1a — local multi-cursor: deferral + feasibility check (2026-08-13,
user decision).** Multi-cursor *editing* is deferred to its own arc with
its own spec. Before deferring, the user asked for two confirmations:
that it is possible at all, and that nothing in this arc works against
the refactor. Both checked against the code, not assumed.

*Possible — the foundation was already built for it.*

| Requirement | What exists today |
|---|---|
| A selection model with more than one editable cursor | `Selection::Kind` has **`Secondary` — "editable, multi-cursor (commands apply to all)"** alongside `Primary`, `SearchMatch` and `Presence`. Local extra carets and remote presences are already *different kinds*, so they cannot collide |
| Storage + persistence for them | `Session::secondarySelections()` / `addSecondarySelection` / `clearSecondarySelectionsOfKind`, kind-tagged JSON round-trip (`Selection::toJson`). Live already: `SearchEngine` writes `SearchMatch` entries into that same list |
| Carets that survive an edit made at *another* caret | `TextAnchor` is CRDT-identity-based and block-scoped: `textAnchorAt(BlockAnchor, offset, bias)` → anchor, `offsetInBlock(BlockAnchor, anchor)` → offset (`BlockAnchor` *is* `BlockId`). N carets → N anchors, apply edits one at a time, re-resolve. No flat offsets, no reverse-order trickery, C4-clean |
| N edits as one undo step | `UndoLog::Transaction` already hosts arbitrarily many ops per entry |
| Per-caret structural keys | `StructuralKeyHandler::handle` is pure and takes one `(block, byte)` — call it per caret |
| Prior art in-house | collabtext's `MultiCursorController` / `CollabPlainTextEdit` |

The canvas side is a real but bounded refactor: `m_caret` (98 uses) and
`m_selectionAnchor` (22) become a cursor *list* plus a primary index.
The uses are mostly mechanical reads, and the helpers are already
value-typed (`CanvasCursor`, `setCaret`, `hitTest`, `caretLessThan`,
`selectedByteRangeInBlock`), which is the shape that makes the change
mechanical rather than architectural.

*Nothing in P1–P7 is blocking.* Three places would be
**counter-productive if written caret-singular**, so they get a
constraint now (recorded on the tasks themselves, and each is cheap
today and expensive later):

1. **P2.1 reveal predicate.** Delimiter reveal must be asked as
   "is this entry/span revealed by *any* cursor", through one predicate
   function — not `m_caret.block == entry.id` scattered across the
   restyle sites. One cursor in the set today.
2. **P3.6 ephemeral-state schema.** Write the cursor key as a *list* of
   one, not a scalar, so multi-cursor is not a schema migration. (The
   `MarkdownView` contract's `cursorPosition()` stays single by
   definition — that is the primary caret, and correctly so.)
3. **P6.2 presence painting.** Paint from a kind-tagged list rather than
   a remote-only `RemotePresence` list, so local `Secondary` carets
   reuse the path instead of growing a second one. Core's
   `Selection::Kind` is the natural discriminator.

Two accepted limitations to carry into the future spec, both matching
CodeMirror/Obsidian: IME preedit belongs to the primary caret only, and
undo coalescing (`CoalesceContext::block`) breaks per keystroke when
carets sit in different blocks — the future arc decides whether to widen
the coalesce key.

*One genuine dependency, and it is already on the plan:* per-caret
anchor survivability runs through the same `TextAnchor` round-trip
**P6.1** must build for Session caret authority. P6.1 is therefore the
enabling task, not an obstacle — if that seam has gaps, P6.1 finds them
first.

*User-directed additions (2026-08-13, folded into the spec — see §5.2):*

- **Inline title.** Note: this is **not** a CodeMirror feature. Obsidian
  renders `.inline-title` in the source-view container *above* the
  CodeMirror instance; it is the file basename, editable, renames the
  file, and is not part of the document text (Obsidian does not write an
  H1). For us that means a leading title band on `EditorWidget` that is
  **not a document block** — so document authority and C4 are untouched,
  but it must be excluded from `cursorPosition()` flat-line coordinates,
  find, selection-copy and serialization, while participating in caret
  motion at the seam (Down/Enter from the title enters block 0; Backspace
  at doc start does not consume it).
- **Fixed-width column** is already spec §5.2 "Word wrap" / task P4.5.
  Obsidian calibration recorded there: `--file-line-width: 700px`
  default, centered, toggleable ("Readable line length"); full viewport
  width when off; tables and code may exceed the column with their own
  horizontal scroll. The inline title shares the same column.
