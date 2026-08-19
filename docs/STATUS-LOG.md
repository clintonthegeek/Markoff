# Markoff status log — banner history

Append-only history of the dated status banners that used to accrete at
the top of the root `CLAUDE.md` (newest first). Moved here 2026-06-09
during the docs-consolidation audit; the live status now lives in
`docs/STATUS.md` and a short Current-status block in `CLAUDE.md`.
Entries below are verbatim and historical — claims reflect the date of
their banner, not the present.

---

> **2026-08-19 — canvas production arc board (CLOSED), moved off the live board.**
>
> Verbatim STATUS.md workfront section for the canvas production
> arc (D5 part 1), moved here when the board was reset for the G1
> accessibility arc. All three gates decided: G1 deferred then
> reopened (see the 2026-08-19 a11y spec), G2 done 2026-08-18, G3
> retired `markoff-live`. Arc-close baseline 315/315. Claims below
> reflect 2026-08-15, not the present.

## Workfront (CLOSED) — canvas production arc (D5 part 1)

User-opened 2026-08-13, same day the spike closed PASS. Goal: take
`libs/markoff-canvas` to feature parity (Corbomite contract v2, old-leaf
parity, Obsidian Live Preview benchmark, collab rendering surface).

- **Spec (normative):**
  [`specs/2026-08-13-canvas-production-design.md`](specs/2026-08-13-canvas-production-design.md)
- **Plan (do the topmost unchecked task):**
  [`plans/2026-08-13-canvas-production-plan.md`](plans/2026-08-13-canvas-production-plan.md)
  — phases P1–P7, user gates G1 (a11y scope), G2 (Corbomite adoption),
  G3 (retirement decision). Phase 1 (core promotions) closed
  2026-08-13 at P1.5; Phase 2 (projection map) closed 2026-08-14 at
  P2.4 (perf re-baseline, all E9 budgets held); Phase 3 (MarkdownView
  contract v2) closed 2026-08-14 at P3.7; Phase 4 (inline/text parity)
  closed 2026-08-14 at P4.8; Phase 5 (block parity) closed 2026-08-14
  at P5.7 (perf re-baseline held). Phase 6 (collaboration surface)
  opened and closed 2026-08-14: P6.0 (core anchor seam + fold retro-
  wire to Session, reduced scope — see plan findings log), P6.1
  (Session caret authority closure), P6.2 (remote presence rendering —
  caret bar, name flag, selection tint), P6.3 (IME-vs-concurrent-
  remote-edit + seeded gremlin fuzz convergence test — no C1/C2
  workaround needed, both tests passed on the first run), P6.4 (⏸
  phase close, full suite 310/310). **G1 decided 2026-08-14: user
  deferred accessibility** — P7.1 skipped for this arc, canvas ships
  with no a11y support this arc (explicit, logged gap, not an
  oversight). P7.2 (drag-drop + middle-click paste) landed the same
  day: text drag out (Copy while read-only, Copy+Move once editable —
  self-drop Move is treated as a copy, logged decision, see plan
  findings log), text/file drag in (text routes through the same
  `insertText()` `paste()` uses; file drops emit
  `EditorWidget::fileDropped(urls, viewportPos)`, Corbomite decides
  embed-vs-link), and X11 primary-selection middle-click paste
  (no-op under this leaf's offscreen test environment, which has no
  platform clipboard integration — real behavior can't be exercised
  here, only the guard path). User then directed F1 gap closure
  (P7.2a-g, 2026-08-14): **P7.2a (undo-coalescing defect, F1 #3)**
  landed same day — `View::insertPrintable` now routes each typed
  character through `Cmd::insertCharacter`, so printable-only,
  same-block runs within 1000ms coalesce into one undo entry
  (`UndoLog::maybeCoalesceOrTransaction`) instead of one entry per
  keystroke; surrogate-pair codepoints (emoji) fall back to a direct
  `maybeCoalesceOrTransaction` call with `isPrintable=false` to avoid
  the QChar-only signature's data-loss trap (see plan findings log).
  No core change needed. **P7.2b (editing-command floor, F1 #1)**
  landed same day: word-wise motion + selection (Ctrl+Left/Right,
  Ctrl+Shift+Left/Right — `QTextBoundaryFinder`, same idiom
  `markoff-live` already uses, not hand-rolled), word-wise delete
  (Ctrl+Backspace/Delete), document start/end (Ctrl+Home/End, now
  moving the caret in addition to the pre-existing scroll-to-extreme),
  delete-line (Ctrl+Shift+K — clears the block's content), move-line
  up/down (Alt+Up/Alt+Down — a content swap between adjacent
  `BlockId`s, since core's `StructuralOp`/`IdList` has no reorder
  primitive; logged, not a core change), select-line (Alt+L), and Esc
  simplify-selection. New test `tst_canvas_editing_command_floor` (4
  falsification-backed scenario groups). P7.2b's agent found
  `tst_canvas_concurrency`'s gremlin-fuzz convergence test failing
  deterministically and mis-logged it as pre-existing; independently
  re-bisected and **fixed same day**: a latent `UndoLog` bug (coalesced
  transactions never fired `onCommit`, silently dropping ops to collab
  peers after a run's first keystroke) made reachable for the first
  time by P7.2a's routing change (commit `623ed6ca`; see Dormant items
  and the plan findings log's "Regression fix" entry for the full
  writeup). **P7.2c (auto-pairing/wrap-selection, F1 #4)** landed
  2026-08-15: 5 named pairs, view-local freshness tracking
  (`m_autoPairedClose`), insertion routed through the same
  `insertPrintable`/`Cmd::insertCharacter` machinery the regression
  fix lives in (deliberately, to not reintroduce that bug class) — see
  plan findings log. **P7.2d (Enter/Backspace semantics checklist,
  F1 #5)** landed 2026-08-15, test-only: diffed `StructuralKeyHandler`
  against CodeMirror lang-markdown's `insertNewlineContinueMarkup`/
  `deleteMarkupBackward` case by case. 3 of 4 documented cases already
  correct — 2 had no direct regression test (ordered-list renumber on
  mid-split; empty-nested-item outdent) and got one each; the 3rd
  (blockquote continuation losing quote depth) is a pre-existing,
  already-logged follow-up (`docs/plans/2026-05-29-styled-structural-
  key-authority.md:670`), re-confirmed not re-fixed. The 4th case is a
  **real, found gap left unfixed on purpose**: CM's Backspace at an
  indent-0 list item's content-start de-lists the line without
  touching the previous block; ours instead merges into the previous
  block via `Cmd::backspaceMerge` — existing, deliberately-tested
  behavior (`listitem_backspace_at_start_indent0_merges`), not a fresh
  regression, and changing it would flip a shared core handler's
  documented behavior against its own test's name — logged as a
  dormant item (below) rather than changed unilaterally. No core
  source change landed. **User decided same day: switch to CM's
  de-list-in-place semantics** — implemented and closed, see Dormant
  items below. **P7.2e (highlight selection occurrences, F1 #7)**
  landed 2026-08-15: a non-trivial (min length 2), non-whitespace-only
  selection gets every OTHER exact-text occurrence in the realized
  entries painted with a new `Theme::Slot::SelectionOccurrenceBackground`
  (green, distinct from both the active selection's blue and find's
  orange), via `View::recomputeOccurrenceHighlights()` hooked into the
  existing `pushSelectionToSession()` chokepoint — same draw-time
  `QTextLayout::FormatRange` mechanism `setFindHighlights` established.
  Realized-entries-only scope (matches every other paint-time
  highlight feature in this leaf); case-sensitive, no whole-word
  requirement (CM `highlightSelectionMatches` defaults). **P7.2f
  (scroll-past-end + placeholder + bracket-match + drop-cursor, F1
  #8/#10)** landed 2026-08-15: 4 small, independent visual additions.
  Scroll-past-end adds bottom scroll padding (viewport height minus
  one line) once a document already needs scrolling, so the last line
  can reach the viewport's top instead of stopping at its bottom edge
  — guarded so a short, already-fully-visible document gains no
  spurious scroll range. The empty-document placeholder
  (`tr("Start typing…")`, reused `Theme::Slot::Quote`) is gated on
  document emptiness alone, deliberately **not** view focus — CM's own
  `placeholder.ts` source gates purely on document length, confirmed
  by reading it rather than copying this same file's own
  focus-gated title-band placeholder convention. Bracket-match
  highlight scans the caret's own block only (C4) for the bracket
  matching the one adjacent to the caret, honoring nesting depth, in a
  new `Theme::Slot::BracketMatchBackground`. The P7.2 drag-drop
  handlers gained a dashed drop-cursor indicator (`Theme::Slot::
  CursorPrimary`, re-hit-tested on every drag move, never cached).
  **P7.2g (invisible/control-char rendering, F1 #9)** landed 2026-08-15
  — the last of the 7 user-directed F1 gap-closure sub-tasks (all now
  ☑). C0 controls/DEL get Unicode Control Picture glyphs; C1 controls,
  soft hyphen, ZWSP, LRM/RLM, BOM, and the safety-relevant bidi
  override/isolate controls (U+202D/E, U+2066–9) get a boxed-hex
  Private-Use sentinel (U+E000, new `Theme::Slot::InvisibleCharBox`).
  The bidi subset is **neutralized at the substitution point**, not
  merely painted over — U+E000 defaults to bidi class L, so it never
  reaches `QTextLayout`'s bidi algorithm; the boxed label is the
  visible warning on top of an already-safe substitution. Phase 7
  continues at P7.3 (⏸ arc close). **P7.3 closed 2026-08-15**: full
  suite 315/315, perf budgets held (build-perf: load→paint 128ms/500ms,
  p95 keystroke 0.64ms/16ms, scroll-realize 9%/30%, RSS delta 0KB/100MB
  — no regression despite P7.2a-g's typing/paint-path churn), whole-leaf
  constitution honest read clean, F1 parity audit done (all 12 gaps
  closed, not waived). **The canvas production arc (D5 part 1) closed
  pending G2** — see the one-page summary delivered alongside this
  close. **G2 UPDATE (2026-08-19):** Corbomite's Cluster K Phase 5
  (2026-08-18, their commit `7a6f18a4`) made
  `Markoff::Canvas::EditorWidget` the sole LivePreview engine and force-
  disabled `MARKOFF_BUILD_LIVE` from their build — G2 is done, recorded
  here a day late per their handoff
  (`docs/handoff/2026-08-19-to-markoff-retire-live-close-e-arc-regroup.md`).
  **G3 decided the same day, scoped to `markoff-live` only:** retired
  (zero downstream consumers left); `MARKOFF_BUILD_LIVE` now defaults
  OFF, source stays in-tree build-fix-only, tag
  `archive/markoff-live-final` marks the last default-ON commit;
  `markoff-styled` is untouched by this decision (still backs
  Corbomite's Reading mode). The arc is now fully closed; the whole
  tree is out of standstill and open for self-directed work again
  (see CLAUDE.md).

Standstill from this opening is now lifted (arc closed 2026-08-19,
see above): `markoff-core` and `markoff-canvas` are open for ordinary
work again, not restricted to plan-named seams. `markoff-styled` stays
bug-fix-only (backs Corbomite Reading mode); `markoff-source` stays
untouched, permanently; `markoff-live` is retired (G3, above). Queue
**#18** is absorbed into the plan (P1.1, P2.1–P2.3 done).



> **2026-08-13 — Contract-v2 arc + Corbomite adoption closed; canvas spike opens.**
>
> Superseded STATUS.md body, moved here when the board was reset for the
> canvas spike: Contract-v2 arc COMPLETE (spec
> `docs/specs/2026-06-09-markdownview-contract-v2-design.md`, plan with
> SHAs `docs/plans/2026-06-09-markdownview-contract-v2.md`, plus
> follow-ons: `caretRect()` `39c5423b..cdd2bd3c`, `replaceMatches`/
> `selectMatchAtOrAfter` `ee77b157`/`b349f122`, findSpans preservation
> `c3b5070f`). **Corbomite adoption done 2026-08-12** (Corbomite
> `e9d70a8b`, re-pinned to Markoff `2a551cde`); migration-table call-site
> work verified item-by-item against the brief
> (`docs/handoff/2026-06-09-corbomite-api-adoption-brief.md`); Corbomite
> ctest 116/117 (one unrelated forcegraph benchmark timeout). Test
> baseline **277/277** verified 2026-08-13 — queue #10 fully closed (all
> 6 slots; decisions: chokepoint-owns-caret-after-undo,
> collapsed-cursor-is-valid-paste-target; bonus fixes: delegate
> focus-before-cursorPosition ordering, `applyFlatEdit` block-boundary
> byte ambiguity). Queue #8.3/#11/#12/#14/#15/#16 all closed 2026-08-12..13
> (details in `docs/archive/2026-08-13-queue-pre-canvas-snapshot.md`).
> Corbomite pin guidance: jump to current master when next touched; never
> pin into `8c13c5d..079ac1f` (list-after-table SIGSEGV window, fixed
> `b1b238f`). Same day: view-layer direction decided (qtbase fork
> rejected; projection-view spike authorized) — see
> `docs/specs/2026-08-13-view-authority-direction-decision.md`.

> **2026-06-14 — Find/Replace primitives for Corbomite + stuck-highlight fix.**
>
> Driven by Corbomite's Find/Replace feature. Two small additions to
> `markoff-core` (spec `docs/specs/2026-06-12-replace-matches-primitive-design.md`):
> `MarkoffDocument::replaceMatches(QList<SearchHit>, QString)` — replaces matches
> as ONE undo transaction (back-to-front global flat offsets via
> `iterateBlocks()`/`blockText()`; nested `UndoLog::Transaction`, NOT
> `coalesceLastUndo()` which is legacy-buffer-only; ends with
> `flushPendingD2Changed()` for a synchronous post-state) — and the mutation-free
> `FindController::selectMatchAtOrAfter(BlockAnchor, offset)`. Tests
> `tst_replace_matches` (6 slots) + 2 slots in `tst_foundation_find_controller`.
>
> Then a markoff-live fix (`fix/find-span-preserve` → `master`): `LiveBlockModel::applyOps`
> wholesale-assigned a fresh parse record on a text-changing edit, wiping the
> adapter-owned `findSpans` **without** signalling `FindSpansRole` — so after a
> find/replace the live delegate kept painting stale find-highlights that could
> not be cleared. Now preserves `findSpans` across the row update (same pattern as
> `inlineSpans`); regression test
> `apply_ops_preserves_adapter_owned_find_spans_on_text_change`. Tip `c3b5070f`.
> Follow-up logged: `defaultDark()` lacks the search-highlight slots (queue #14
> dark half).

> **2026-06-10 — contract-v2 arc COMPLETE (all 13 tasks landed); Corbomite adoption brief written.**
>
> Tasks 9–13 landed in this session on top of the Tasks 1–8 foundation from the
> previous session (2026-06-09). The arc delivers a fully honest `Markoff::MarkdownView`
> base contract: all three view leaves now implement find-attach/detach, base undo/redo
> (→ `undoD2`), theme/fontScale forwarding, all six format verbs, real
> `cursorPosition()`/`setCursorPosition()`, scroll signals, read-only mutation gates,
> and `contextChanged(EditorContext)` — with zero leaf-type switches needed by the
> consumer for any of those operations.
>
> Task 9 (`96b88eec`): live theme/fontScale/verb delegation.
> Task 10 (`ebdbdeff`): contextChanged feed — source + styled.
> Task 11 (`334bfd35`): contextChanged feed — live + scroll-signal sweep (amended through review+polish).
> Task 12 (`ffbde444`): source fontScale + §10 contract-suite consolidation + styled fontScale authority consolidation.
> Task 13 (this session): Corbomite adoption brief
> (`docs/handoff/2026-06-09-corbomite-api-adoption-brief.md`), spec §5/§7 deviation
> annotations, queue items #14–16 filed, per-lib CLAUDE.md public-surface sections
> updated, STATUS board advanced. Test baseline unchanged: **266/269** fast inner loop.
>
> Next: Corbomite adoption (migration table in the brief; no Markoff code changes needed).

---

> **2026-05-31 — styled-table SIGSEGV fixed (dogfood crash on a list-after-table).**
>
> Opening a real doc with a table followed by a list
> (`markoff-styled-app docs/phase-c-status.md`) crashed. `FormatPass` was
> computing QTextDocument positions from flat pipe-source bytes; once a table is
> a compact `QTextTable` frame, every block after it overran the document → an
> invalid `QTextBlock` reached `QTextList::add()` → SIGSEGV. Fixed by making
> `FormatPass` walk the document's real top-level elements
> (`QTextFrame::iterator`, which doesn't descend into cells) in lockstep with
> model blocks instead of byte arithmetic; plus a defense-in-depth invalid-block
> guard in `manageListMembership`, and a frame-key format fix
> (`StyledTableRenderer` wrote a `"markoff-table:"` prefix the binding read as 0,
> so it re-seeded on every edit). Regression test
> `tst_styled_table_render::list_after_table_does_not_crash_and_renders`
> reproduces the crash and passes; the app loads `phase-c-status.md` cleanly.
> Baseline still 259/262 (the 3 known offscreen flakes). Discipline-log lesson in
> `docs/queue.md`: the original guard test used a paragraph-after-table, which
> tolerates a bad position — a list is required to exercise the failure.
>
> ---
>
> **2026-05-30 (later) — `markoff-styled` renders tables (read-only QTextTable) via a new opaque-block seam.**
>
> `BlockKind::Table` blocks now render as native Qt `QTextTable` grids in the
> styled view — real borders, per-cell alignment from the `:---:` row, bold
> header — **read-only** (edit a table by dropping to Source mode; the grid
> re-renders). The table buffer stays canonical and is never mutated by
> rendering, so save round-trips byte-for-byte.
>
> The enabling piece is a view-agnostic **opaque-block seam** in `markoff-core`:
> `Markoff::OpaqueBlockRenderer` + `SourceTextDocumentBinding::setOpaqueRenderer`.
> A `QTextTable` frame's character stream is not the block's flat bytes, so the
> binding's normal whole-document reverse diff would clobber it; with a renderer
> set, the reverse path switches to **region-based** reconciliation (frames
> partition the document into text regions diffed independently; frames survive
> edits to other blocks). **Inert when no renderer is set** — `markoff-source` is
> byte-identical to before. Non-obvious fact found via diagnostic: Qt forces a
> trailing empty block after a table frame, so the document is *not* 1:1 with
> model blocks — hence region-based, not naive lockstep.
>
> `markoff-styled` side: `TableFrame` (`parsePipeTable`/`materializeTable`, ports
> the deleted master-era `TableConverter`/`TableSerializer::parseAlignments`),
> `StyledTableRenderer` (opaque only when the buffer parses — malformed tables
> degrade to text), `FormatPass` skips `Table`, `StructuralTextEdit` swallows
> edit keys inside a frame. Spec/plan under `docs/superpowers/`.
>
> **Tests:** +24 table slots green (`tst_binding_opaque_block` 5,
> `tst_styled_table_{parse 6, materialize 4, render 4, readonly 5}`) +
> `tst_table_block_loading` gains an empty-pipe-row split guard (8/8). **Baseline
> 259/262** via `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'` — the 3
> failures are the long-standing offscreen flakes only; no regression.
> **Deferred (seam is the foundation; in `docs/queue.md`):** in-grid cell edit,
> structural row/col ops, alignment via context menu, source-reveal in-place flip.
>
> ---
>
> **2026-05-30 — Test baseline cleaned (queue #8.6/#8.7 closed) + #9 empty-blockquote regression bisected & fixed.**
>
> Triaged the two flagged failing binaries, classifying every slot
> drift-vs-real-bug *before* touching code. 6 of 7 slot failures were
> **drift** from the WP-unification single-`\n` separator + soft-break
> collapse — test contracts realigned to the new shape
> (`tst_styled_block_formats` 9/9, `tst_source_widget_format_ops` 16/16;
> e.g. heading-block indices off the retired `\n\n` separator blocks,
> list-item indent assertion moved to `QTextList` membership, stale
> 2-byte qt-coords). 1 was a **real production bug** (`a0d8f5b`):
> `Source::Editor::setHeadingLevel` hardcoded `SEP_LEN = 2` in a bespoke
> block-walk while reading positions from the live single-`\n`
> `toPlainText()`, underflowing `byteInBlock` for any heading below the
> first block — Ctrl+heading on a non-first line mangled it (`Hello## `).
> Fixed by reusing the shared `Detail::findBlockAtSepByte` (single
> `SEP_LEN == 1`), deleting the duplicate constant.
>
> The triage surfaced an **undocumented, consistently-failing** live
> regression (`tst_live_render_structural::blockquote_enter_on_empty_exits`,
> not one of the known offscreen flakes). `git bisect` (automated via
> `git bisect run`) pinned it to `4faa451` (#8.1 `block_quote` per-child
> recursion): an empty quote `"> "` has only marker children, so the
> walker emitted **zero** TLBs and `loadFromMarkdown("> \n")` produced 0
> blocks. Fixed at the parser layer (`3b0ca22`,
> `TreeSitterParser::collectTopLevelBlocks`): a `block_quote` that recurses
> to nothing now emits one empty `Paragraph` TLB carrying the quote
> depth/runId; the load side maps it to `BlockKind::BlockQuote` and strips
> the `"> "` buffer to empty, round-tripping to `"> "`. Falsifiable tests
> added at the parser + core layers (both proven failing pre-fix).
>
> **Test baseline (2026-05-30): 254/257** via
> `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`. The 3 remaining
> are the long-standing offscreen/window-manager-dependent live failures
> only (`tst_live_render_e2_nav_shift_extend`,
> `tst_live_render_focus_chokepoint_invariant`,
> `tst_live_render_cursor_typing_invariant`) — every remaining failure is a
> known, deferred item. **Still open (logged in `docs/queue.md`):** the
> `SourceFindAdapter.cpp:104 += 2` latent find-highlight drift (same
> "one flat-view changed separator width, a sibling didn't" bug class as
> the `setHeadingLevel` fix); queue #8.3 source-view list-item markers.
>
> ---
>
> **2026-05-29 — WP unification dogfood arc closed for Paragraph + ListItem; flat-view spacing now em-based; bullets render.**
>
> Two dogfood-driven arcs landed on top of the WP unification baseline
> (commits `1f7fb99..b8a6bf8`):
>
> 1. **StyleApplier coordinate fix** (`6b5abcc`) — StyleApplier was
>    computing block positions against `flatView()` (2-byte `\n\n` sep)
>    while the QTextDocument is seeded from `widgetFlatView()` (1-byte
>    `\n` sep). Every block past the first drifted by 1 char per
>    preceding boundary, landing inline char-formats several chars past
>    their span. Coordinate space realigned.
>
> 2. **Hard-wrapped paragraph & list-item rendering** (`fa3d9ce`,
>    `845fc0f`). CommonMark "soft line breaks" (single `\n` inside a
>    paragraph/list item) were surviving into block buffers as raw
>    bytes, so flat-view leaves saw spurious QTextBlock boundaries per
>    hard-wrap line. `MarkoffDocument::buildD2FromBytes` now collapses
>    internal `\n` → space at load time for `Paragraph` and `ListItem`
>    kinds. `markoff-styled`'s `applyListItem` now reads
>    `MarkerStyle`/`IndentLevel`/`Checked` from block attrs and renders
>    bullets/decimals via per-item `QTextList` (and task checkboxes via
>    native `QTextBlockFormat` marker). Falsifiable invariants pinned
>    in `tst_block_buffer_invariant`.
>
> 3. **Em-based spacing rework** (`6019b70`) — all per-kind block
>    margins, list indent, and `QTextDocument::indentWidth` switched
>    from pixel constants to em-multipliers derived from
>    `kBaseBodyPt × fontScale`. Zoom in/out now scales gaps
>    proportionally. Spacing helpers (`paragraphMarginPt`,
>    `listItemMarginPt`, `docIndentWidthPx`, etc.) are named constants
>    at the top of `StyleApplier.cpp` — one-line tuning, not a hunt.
>
> **Still open from this arc (documented in queue + guide §0):**
>
> - ~~`BlockQuote` retains internal `\n`s — its byte range includes
>   per-line `> ` markers that need marker-aware stripping.~~ → closed
>   2026-05-29 by queue #8.1 (spec
>   `docs/specs/2026-05-29-blockquote-multi-paragraph-split-design.md`).
>   Parser walker now recurses `block_quote` children carrying
>   `blockQuoteDepth` + `blockQuoteRunId` fields; load strips `> `
>   markers + collapses `\n→space`; serializer reconstructs depth ×
>   `> ` and uses RunId for `\n>\n` (same run) vs `\n\n` separators;
>   StyleApplier reads depth from attrs and overlays left-margin on
>   non-BlockQuote inner kinds. Setext `Heading` closed 2026-05-29 in
>   `0291ac6` — load-side strip + `serializeHeading` reconstructs
>   underline from `(content.size(), level)`. `serializeForSave`
>   bypasses its untouched fast path for setext so reconstruction
>   always runs. `LiveListModelBinding`'s form-aware demote tightened:
>   single-line buffer no longer triggers demote (canonical setext
>   shape).
> - **Source view shows list-item buffers without their markers** —
>   the buffer is post-marker content, so `widgetFlatView` for source
>   shows `foo` not `- foo`. Source-view marker reconstruction is a
>   follow-up.
> - **Ordered-list continuous numbering** — every item is its own
>   single-item `QTextList`, so ordered items always render `1.`.
>   Sibling-grouping needed.
> - **Hash gate over attrs** — `computeBlockHash` covers
>   `(kind, text, spans, fontScale)` but not attrs, so
>   `toggleListItemChecked` and `IndentLevel` rewrites without text
>   change don't restyle. Add attr hash to gate.
> - **`tst_styled_block_formats`** has 2 pre-existing failures
>   (`heading_levels_descend_in_size`, `horizontal_rule_uses_monospace`)
>   that predate this arc; triage.
> - **`tst_source_widget_format_ops`** has 4 failures that crept in
>   during the WP unification commits; pre-existed today's fixes
>   but post-date the arc-start; needs investigation.
> - **Enter at end of bullet under heading merges bullet into
>   heading (styled leaf).** Surfaced 2026-05-29 by user dogfood
>   immediately after the #8.1 push (`2a7d757`). Repro: open
>   `docs/phase-c-status.md`, place caret at end of first bullet
>   under `### C1 — DI seam`, press Enter — entire bullet body
>   sucked into the heading with the bullet's first character (`R`)
>   duplicated; caret jumps to end of last bullet and merges in the
>   next heading. Filed as queue #8.8 with bisect plan
>   (`46643e7` is the session-start pre-#8.1 commit). **Until
>   bisected #8.1 is code-complete but not closure-confirmed.**
>
> **Test baseline (2026-05-29):** 249/254 pass via
> `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`. The 5
> failures above; zero new regressions from this arc.
>
> ---
>
> **2026-05-27 — `markoff-styled` view leaf landed + hardened; View Implementor's Guide written; cursor-authority fix CLOSED.**
>
> A third view leaf, `markoff-styled` (QTextEdit, inline-styled, no QML/KF6),
> landed 2026-05-26 (spec `docs/specs/2026-05-26-markoff-styled-leaf-design.md`).
> Dogfood drove two follow-ups: v0.1 perf/styling fixes
> (`docs/specs/2026-05-27-markoff-styled-dogfood-fixes-design.md`) and a
> `markoff-core` binding-robustness arc that made the single-document binding
> a first-class peer of the per-block path — boundary-correct forward
> dispatch, normalize-on-edit, incremental reverse sync
> (`docs/specs/2026-05-27-markoff-core-binding-robustness-design.md`,
> commits `f5cdc4e..10ed95a`).
>
> Cursor-authority fix (queue #7) landed via `ff33a6e..eb685f0` —
> `applyInteractiveNewline` + caret-resolution chokepoint in the
> binding, consumed by both styled and source. Spec
> `docs/specs/2026-05-27-flat-view-enter-and-caret-authority-design.md`.
> The cross-cutting view↔model concerns are catalogued once in
> **[`docs/VIEW-IMPLEMENTORS-GUIDE.md`](docs/VIEW-IMPLEMENTORS-GUIDE.md)**
> (evergreen, peer to INVARIANTS; required reading for any view leaf).
>
> ---
>
> **2026-05-20 — Port-first phase ACTIVE. Corbomite renders foundation-exploration content (milestone hit late session).**
>
> Corbomite's `port/foundation-exploration` branch successfully builds + launches +
> renders documents against this branch's HEAD. The port-first rhythm
> (port-driven micro-specs land in Markoff as gaps surface in Corbomite) is
> producing real fixes against real consumer pressure. Full session recap:
> [`docs/handoff/2026-05-20-port-first-session-recap.md`](docs/handoff/2026-05-20-port-first-session-recap.md).
>
> **Branch story across both repos (post-merge, 2026-05-25):**
>
> | Repo | Branch | State |
> |------|--------|-------|
> | Markoff (this repo) | `master` | **Active development branch.** Foundation rebuild merged from `exploration/new-foundation` at `3c7afa9` (2026-05-25). All ongoing work happens here. |
> | Corbomite | `master` | Active. Re-pinned to Markoff `master` post-merge per `docs/handoff/2026-05-25-to-corbomite-merge-complete.md`. |
>
> The 2026-05-20 "eventual merge plan" has been executed: Markoff
> `exploration/new-foundation` → `master` landed at `3c7afa9`, retiring the old
> four-leaf layout wholesale, and Corbomite's `port/foundation-exploration` has
> been merged + re-pinned. Both repos are single-line-of-development again.
>
> **Port-driven Markoff commits this session (2026-05-20, all on this branch):**
>
> | Commit | What |
> |--------|------|
> | `af45aa5` | `MARKOFF_BUILD_APPS` option — submodule consumers can skip the demo app |
> | `47f62c4` | Restore `EmbedRegistry` + `MarkdownRenderChild` + `EmbedDepthGuard` + `Vault::ResourceProvider` (port pull) |
> | `e8986f8` | `EmbedRegistry::hasExtension/unregisterExtension` (port pull) |
> | `bc8216d` | New `Markoff::Live::EditorWidget` — QQuickWidget wrapper for QWidget hosts |
> | `d4b117a` | Fix EditorContent.qml resource path (Qt preserves source-side `/qml/` prefix) |
> | `d5d210e` | EditorWidget flushes pending d2 changes after setDocument (initial model population) |
> | `7dae201`/`2291c99` | Debug instrumentation added then removed once data path confirmed |
>
> Three earlier commits from same session were docs-only (freeze-spec withdrawal, port-first pivot).
>
> **Speculative `markoff-core` freeze draft preserved at**
> [`docs/specs/2026-05-20-markoff-core-freeze-shape-design.md`](docs/specs/2026-05-20-markoff-core-freeze-shape-design.md).
> Banner at top of that file marks it draft-reference-not-action-plan. Type-identity
> decisions D1/D2/D3/D10 may survive a future evidence-driven freeze; the rest awaits
> real port pressure.
>
> **Withdrawn live-freeze amendments** (Capabilities::Editable, EditorWidget) — the
> EditorWidget got built anyway in `bc8216d` because Corbomite actually pulled on it.
> Recorded as proposed-and-built in [`docs/specs/2026-05-19-markoff-live-freeze-shape-design.md`](docs/specs/2026-05-19-markoff-live-freeze-shape-design.md).
>
> **Test baseline (2026-05-23, late session):** 235/238 pass. Test count
> 235 → 238 from the E4 wrap+width follow-up — added
> `tst_live_render_table_layout_metrics` (15 slots, C++ unit) and
> `tst_live_render_table_layout` (5 slots, QML-reach integration). Three
> pre-existing failures (all unrelated to E4; all pre-date this session):
> - `tst_live_render_e2_nav_shift_extend` (slot from `0cbdf48`'s within-block word-extend rework)
> - `tst_live_render_focus_chokepoint_invariant` (undo/redo edge cases)
> - `tst_live_render_cursor_typing_invariant` (emoji-typing mirror)
>
> Run the full suite via `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
> for the fast inner loop. None of the failures block ongoing work.
>
> **Prior closed items:** E2.6 tagged `v0.7.0-e2.6`, cursor architecture
> (tiers 1–4c), QML integration harness, B1 buffer convention, code review
> pass, E3a wikilinks (`109d3d3`; dogfood pending — tag `v0.7.0-e3a` to be created after signoff).
>
> **Port-driven items closed since 2026-05-20 (all four open items resolved):**
> 1. ✅ **Find UI wiring** — Corbomite `fa72b4ee..7f975120` executed the
>    10-task port plan (`docs/superpowers/specs/...` Corbomite-side); Markoff
>    `FindController` is the source of truth, FindBar drives it, view-mode
>    swap rewires.
> 2. ✅ **Doc-sharing doubling** — confirmed not recurring during ongoing
>    dogfood (2026-05-21); most likely subsumed by `c3316f9`
>    (`flushPendingD2Changed` unconditional emit) + the `EditorWidget`
>    wiring evolution.
> 3. ✅ **Source mode renders empty** — `f16f894` rebuilt the source-widget
>    binding around `flatView` + separator-bearing QTextDocument.
> 4. ✅ **`MarkoffDocument::resetContent` doesn't build D2 blocks** —
>    `861196c` factored a `buildD2FromBytes()` helper and calls it from
>    `resetContent` after legacy-buffer-of-origin handling. Caveat: wholesale-
>    replace origins on a non-fresh doc still need a D2 wipe pass before
>    rebuild (the IdList CRDT lacks `clear()` semantics yet); tracked in the
>    test docstring + `resetContent` comment.
> 5. ✅ **D2 reset/reload doubling** — closed 2026-05-25 by
>    `wipeD2State()` (Markoff `f48525d`) + IdList/CausalLwwMap
>    `local_clear()` primitives. Spec
>    `docs/specs/2026-05-25-d2-reset-clear-design.md`. Closes
>    the caveat previously documented at
>    `MarkoffDocument.cpp:741-746` and the
>    `tst_d2_reset_content.cpp` test docstring.
>
> **Currently open port-driven work:** none surfaced. Next port pressure
> drives the next micro-spec — until then, opportunistic cleanup from the
> follow-ups doc ([`docs/specs/2026-05-21-source-view-cleanup-followups.md`](docs/specs/2026-05-21-source-view-cleanup-followups.md))
> or the `docs/queue.md` backlog.
>
> **Active workfront (as of 2026-05-23, late session) — E4 tables, phases A–G shipped + wrap/width follow-up + dogfood-confirmed.**
> The E-arc roadmap's `E4` sub-phase has been worked end-to-end this
> week: spec [`docs/specs/2026-05-22-e4-tables-design.md`](docs/specs/2026-05-22-e4-tables-design.md),
> plan [`docs/plans/2026-05-22-e4-tables.md`](docs/plans/2026-05-22-e4-tables.md),
> status [`docs/e-arc/e-arc-status.md`](docs/e-arc/e-arc-status.md). The
> wrap + smart column-width follow-up landed end-to-end in this session:
> spec [`docs/specs/2026-05-23-e4-cell-wrap-and-column-width-design.md`](docs/specs/2026-05-23-e4-cell-wrap-and-column-width-design.md),
> plan [`docs/plans/2026-05-23-e4-cell-wrap-and-column-width.md`](docs/plans/2026-05-23-e4-cell-wrap-and-column-width.md),
> implementation `65b6a35..6a0865a` (Penelope's `distributeColumnsAuto`
> ported to `TableEditBinding::computeColumnWidths`; `TableDelegate.qml`
> consumes via per-cell `Layout.preferredWidth` + `WrapAtWordBoundaryOrAnywhere`;
> post-land dogfood revealed a `Layout.fillHeight` gap that exposed
> horizontal seams, fixed in `6a0865a`). **User dogfood-confirmed the wrap
> behaviour: "the table seems quite usable, as it were."** Remaining
> within E4: tag `v0.7.0-e4` after the broader Phase H checklist passes
> (Source-mode round-trip, real-doc Tab nav, BlockSelected delete, link
> clicks in cells, etc. — see [`docs/handoff/2026-05-22-e4-dogfood-request.md`](docs/handoff/2026-05-22-e4-dogfood-request.md)).
> Same-day dogfood iterations from the morning session: `5c67777`
> typing-perf, `9d5235f` Shift+arrow cross-cell.
> Architectural side-quest from the E4 work:
> [`docs/specs/2026-05-22-cursor-authority-decision.md`](docs/specs/2026-05-22-cursor-authority-decision.md)
> captures the L3 cursor-authority decision the codebase had left
> implicit (chokepoint `syncFromTextEdit` same-block contract, anchor
> preservation via `m_selectionExtended`); the doc + corresponding
> regression tests are the authoritative reference for any
> focus/caret/cross-block work going forward.
>
> **For any new spec work:** use `superpowers:brainstorming` first. Don't draft
> speculative freeze specs — wait for port evidence.
>
> ---
>
> **2026-05-07 — Branch is on D5-first posture. v1.0 plan retired.**
> Authoritative posture is `docs/handoff/2026-05-07-pivot-to-d5-first.md`.
>
> Past-context references (for provenance only; not required for new work):
> - `docs/handoff/2026-05-09-e2.5-dogfood-findings.md` — dogfood
>   findings + the resolution log (D1–D9 fixes documented).
> - `docs/specs/2026-05-09-e2.5-editing-affordances-design.md` — E2.5
>   spec (background).
> - `docs/specs/2026-04-29-live-render-design.md` §Widget-window bridge
>   — design pattern used for the native-`QMenu` context menu landed in
>   `c7da731`.
>
> Decision record (§4.6 deferral): `docs/handoff/2026-05-08-defer-46-to-e-arc.md`.
> Live status: `docs/e-arc/e-arc-status.md`.
>
> **E-arc** is live-render completion as the maximalist Markoff
> prototype: inline-format styling, cursor-aware delimiter visibility,
> Obsidian affordances (wikilinks/embeds/tags/callouts), tables,
> frontmatter, footnote rendering, math/mermaid Live parity. E-arc
> bookends with a distillation phase (E6) that extracts the recipe
> for generalising Markoff into new view shapes — every future view
> is a structural subset of the live-render prototype. Authoritative
> framing: `docs/specs/2026-05-08-e-arc-framing.md` (read §0.1
> amendment first). Roadmap: `docs/e-arc/2026-05-08-e-arc-roadmap.md`.
>
> **Fresh agent context — read in order:**
>
> 1. `docs/specs/2026-05-08-e-arc-framing.md` — **E-arc constitutional
>    framing; authoritative for the active arc.** Read §0.1 amendment
>    first (records the §4.6 deferral and that E-arc begins now).
> 2. `docs/e-arc/2026-05-08-e-arc-roadmap.md` — E-arc orientation,
>    phase summary, binding constraints.
> 3. `docs/e-arc/e-arc-status.md` — **live E-arc status board.**
> 4. `docs/handoff/2026-05-08-defer-46-to-e-arc.md` — decision record
>    for the §4.6 deferral / E-arc activation.
> 5. `docs/handoff/2026-05-07-pivot-to-d5-first.md` — D-arc-era pivot
>    doc; banner in §4.6 records the deferral, banner in §4.7 notes
>    E-arc begins.
> 6. `docs/handoff/2026-05-07-live-binding-developmental-history.md` —
>    pipeline-feature provenance; cited by operating principle 4.
>    Has 2026-05-08 erratum on §A.7 — `inlineSpansFor` is load-bearing
>    for E-arc, not dead code.
> 7. `docs/d-arc/d-arc-status.md` — D-arc status board (closed at §4.5;
>    reference-only).
> 8. `docs/d-arc/2026-05-04-d-arc-roadmap.md` — D-arc orientation
>    (with §6 pointer to E-arc).
> 9. `docs/d-arc/collabtext-scope-line.md` — six "won't do" items.
> 10. `docs/specs/2026-05-07-d5-collab-activation-design.md` —
>    D5 substantive design (background; complete).
> 11. `docs/specs/2026-05-07-d4-parser-scope-reduction-design.md` —
>    D4 spec (background; complete).
> 12. `docs/specs/2026-05-05-d3-view-layer-adaptation-design.md` —
>    D3 spec (background; complete).
> 13. `docs/specs/2026-05-06-per-item-listitem-blocks-design.md` —
>    D3 corrective (background; complete).
>
> The C-restoration arc and the v1.0 plan series are retired and
> archived under `docs/archive/c-restoration-arc/` and
> `docs/archive/v1.0-plan-pre-d5/` respectively. They are not
> authoritative and not to be cited in new specs except as historical
> context. R5.5 Bug 3 is cancelled, not paused.
>
> `libs/markoff-view-qml` was deleted entirely in commit `f646c90`
> (landed 2026-05-07 as part of v1.0 Part 1 prep before the v1.0 plan
> was retired). The deletion stands; the pivot doc §4.2 work-unit was
> redundant and is closed. `markoff-source` is the canonical source
> widget.
>
> All other content below describes the project at large.
