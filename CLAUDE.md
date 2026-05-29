# Markoff

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

---

## Engineering discipline — read before any non-trivial change

This branch has three serious post-mortems on file, each of which
correctly named the failure pattern the **next** refactor went on
to reproduce. The bottleneck is not awareness, it is discipline.
The eight invariants below are how we break that cycle. Full text
and citations in [`docs/INVARIANTS.md`](docs/INVARIANTS.md) — read
it once, then it's yours.

**You are responsible** for following these in your own work, **and
for noticing when you see them violated in code you pass through —
even if the violation is off-topic from your current task.** A
smell you leave unmarked is a vote for it being normal. The
Discipline Log (invariant 8 below; section at the top of
`docs/queue.md`) is the cheap mechanism — log the smell, finish
your task, move on.

These rules scope to the **focus/caret/block-change seam** (see
`docs/INVARIANTS.md` §"Scope and exceptions" for the file list).
Outside the seam, normal engineering judgement applies.

1. **Cite the developmental record before refactoring the seam.**
   `docs/handoff/2026-05-07-live-binding-developmental-history.md`
   is authoritative for *why this code looks like this*. Cite the
   relevant section in your spec.

2. **L4 (block-content authority) is decided in writing first.**
   Model wins, or delegate wins — pick one in your spec, before
   the plan, before the code. The current implicit decision is
   the foundational fault (2026-05-02 audit).

3. **A new authority retires the old one in the same plan.** Dual
   sources of truth → pairwise reconciliation → race window →
   focus loss. Every regression in this seam has come through this
   mechanism. Name the retiring store in the spec; delete it as a
   work-unit in the same plan, not a follow-up.

4. **Falsifiable invariant tests come first.** On
   `LiveRealisticInputHarness`. Prove falsifiable by breaking the
   target seam in a throwaway stub — if the test doesn't fail,
   the test is too lenient; fix the test before touching
   production. (Prescribed by R5-holes post-mortem §6.2; never
   enforced; queue #2 concern #6 names the specific invariant the
   cursor seam needs.)

5. **Tests exercise the production callsite, not a synonym.** A
   C++ test that calls a slot directly does not protect that slot
   if production reaches it through QML. See the
   `pendingVisualLineHint` precedent.

6. **`Qt.callLater` / `QTimer::singleShot(0, ...)` are smells.**
   "I gave up on understanding the timing and brute-forced it."
   When adding: justify in the commit. When seeing: log it.

7. **Re-entrance guards (`m_applyingX`, `isApplyingY()`) are
   smells.** Same rule.

8. **Notice and note — the Discipline Log.** Append to
   `docs/queue.md` § Discipline Log: one line, `file:line`,
   invariant number, one phrase of context. No fix required in
   the same session. The point is the smell becomes visible to
   the next agent.

If you are about to deliberately violate one of these and have a
real reason: **write the reason in the spec**, cite the rule by
number, and proceed. The rules exist to make deviations visible,
not to forbid them.

---

Qt6/C++ markdown editor family, mid-rebuild. The new-foundation branch
has retired the original four leaves (`markoff-core`, `markoff-live`,
`markoff-reading`, `markoff-source`) and is rebuilding around a
foundation library + two canonical view leaves.

## Layout

- `libs/rapidyaml`             — vendored YAML parser (`ryml::ryml`).
- `libs/markoff-parser`        — tree-sitter Markdown AST + frontmatter.
                                 Public type `Markoff::Document` is a
                                 value snapshot; `TreeSitterParser`
                                 parses on demand (no incremental path
                                 after D4).
- `libs/collabtext`            — CRDT text engine, sibling-symlinked
                                 from `/home/clinton/dev/collabtext`.
- `libs/markoff-core`    — `Markoff::MarkoffDocument` (D2: per-
                                 block CRDT buffers + `IdList` for block
                                 order + sibling causal-LWW maps for
                                 kind/attrs/link-refs/footnotes/
                                 frontmatter). `Cmd::*` command set,
                                 `UndoLog`, `WatermarkCoordinator`,
                                 `applyFlatEdit` (D4: flat-text entry
                                 point for source-widget edits).
- `libs/markoff-live`   — **the active live-preview view leaf.**
                                 Built on D2's per-block CRDT buffers via
                                 `LiveListModelBinding`, `LiveBlockModel`,
                                 `LiveCursorState`, `LiveStructuralKeyHandler`,
                                 `LiveEditBinding`, `BlockKindRegistry`.
                                 Layered L0–L8 (see lib's CLAUDE.md). D3
                                 is implemented here; D3-correction (per-
                                 item ListItem blocks) is the active rework.
- `libs/markoff-source` — canonical QPlainTextEdit-based source
                                 widget (replaces the retired Qutepart-
                                 based `markoff-source`).
- `libs/markoff-styled`        — third view leaf (2026-05-26). Plain-jane
                                 QWidget editor on `markoff-core`. No QML, no
                                 KF6. Parser-driven block + inline formats
                                 via `MarkoffDocument::inlineSpansFor` and
                                 `iterateBlocks`. `Markoff::Styled::Editor`
                                 is the public widget. Spec
                                 `docs/specs/2026-05-26-markoff-styled-leaf-design.md`. v0.1 added
                                 per-block hash gating + kind transition + scroll-position preserve
                                 (`docs/specs/2026-05-27-markoff-styled-dogfood-fixes-design.md`).
- `libs/jkqtmathtext`          — LaTeX math rendering. Untracked sibling
                                 wired in for D3's Math delegate.

## Building

```bash
cmake -S . -B build-dev -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-dev -j 8
```

## Testing

Run all tests via `scripts/run-tests.sh`. It defaults to
`QT_QPA_PLATFORM=offscreen` so Qt renders to memory buffers — no windows
appear, no focus is stolen, no virtual X server needed.

```bash
scripts/run-tests.sh                     # full suite
scripts/run-tests.sh -R 'cursor'         # ctest pattern
scripts/run-tests.sh --bin tst_block_id  # one test binary
scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'  # fast inner loop
```

Three modes:

- **offscreen** (default) — no window server interaction. Use for everything.
- **nested** (`--nested`) — spawns a nested Weston compositor in a single
  window on your desktop; tests run inside it. Use when you need to *see*
  rendering without it stealing focus from your main session.
- **direct** (`--direct`, requires `MARKOFF_ALLOW_DIRECT=1`) — runs against
  your real Wayland/X11 session. Expect focus interruptions. Reserved for
  cases where you explicitly want windows on your screen.

**Agents must default to offscreen.** Never invoke `--direct` without
explicit per-task user permission. Even `--nested` should be reserved for
visual verification work; the offscreen path is the daily driver.

A handful of tests currently fail under offscreen (~11 of 201, mostly in
`tst_live_render_*` — focus/cursor tests that depend on real window-manager
behaviour). Triaging them is open work; until then, exclude with
`-E '<pattern>'` when running a clean baseline.

## Conventions

- C++20, Qt6.8+, CMake 3.19+.
- `tr()` for user-visible strings.
- `QIcon::fromTheme()` for icons.
- SPDX header `GPL-3.0-or-later` on every source file.
- Tests define expected behavior — when a test fails, fix the code,
  not the test. Exception: tests that probed behavior we're explicitly
  changing (rename test contracts to match the new shape, don't
  retrofit).

## Edit hot path (D2, current)

In D2, typing does not reparse the document. The path is:

1. QML `TextEdit::contentsChange(qtPos, removed, added)` →
   `LiveEditBinding::onContentsChange`.
2. Compute byte offset from QChar pos via `Coordinates::qtPosToByte`.
3. `MarkoffDocument::d2ApplyBufferEdit(blockId, byteOffset, removeBytes,
   insertedUtf8, transaction)` — directly mutates the per-block CRDT
   buffer.
4. `scheduleD2Changed()` queues a debounced `d2DocumentChanged` signal
   (one per event loop iteration).
5. `LiveListModelBinding::onD2Changed` rebuilds `BlockRecord`s from
   `iterateBlocks()`, runs kind-transition heuristics on Equal-op
   blocks (issuing `Cmd::changeKind` if the prefix-rule inference
   disagrees with stored kind), runs `applyOps` against the model.
6. Model emits `dataChanged` / `rowsInserted` / `rowsRemoved`. QML
   delegates re-render. `LiveCursorState` resolves pending cursor
   requests via the `structuralRowsInserted/Removed` signals.

The parser is only called at **load time** (`Document::fromMarkdown` in
`loadFromMarkdown`) and **per-block on demand** (`inlineSpansFor(blockId)`,
cached). The incremental-parse pipeline (`ParsePool`, `IncrementalParseSession`)
was deleted in D4.

## Per-library guides

- `libs/markoff-core/CLAUDE.md`
- `libs/markoff-live/CLAUDE.md` — **active view leaf**
- `libs/markoff-source/CLAUDE.md`
- `libs/markoff-styled/CLAUDE.md`
- `libs/markoff-parser/` (no per-lib CLAUDE.md; docs in `docs/specs/`)

## Docs layout

- `docs/handoff/2026-05-07-pivot-to-d5-first.md` — **authoritative
  posture; read first.**
- `docs/handoff/2026-05-07-live-binding-developmental-history.md` —
  developmental record for the live-binding pipeline.
- `docs/specs/`   — design specs (dated, kebab-case). Live D-arc
  specs only; retired arcs are under `docs/archive/`.
- `docs/plans/`   — implementation plans (live D-arc only).
- `docs/handoff/` — session handoff briefs.
- `docs/d-arc/`   — D-arc roadmap, status board, scope-line.
- `docs/TODO.md`  — running todo list.
- `docs/archive/` — retired arc paper trails (C-restoration,
  pre-D5 v1.0 plan, Phase B/C). Reference only; not authoritative.
- `docs/phase-c-status.md` — master-side historical, superseded.
  Do not update.

## Branch posture

Master is the active branch (single line of development). The old four-leaf
layout (`markoff-core`, `markoff-live`, `markoff-reading`, `markoff-source`)
was retired wholesale during the `exploration/new-foundation` → `master`
merge at `3c7afa9` (2026-05-25); the new layout (foundation library + two
canonical view leaves) is canonical. Don't attempt to resurrect the old
leaves — if you need behavior from them, re-implement it in the new layout.
