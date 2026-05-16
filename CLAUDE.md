# Markoff (exploration/new-foundation branch)

> **2026-05-10 — If interactive dogfood is unavailable, work the queue.**
> S1/S2/S3 cursor fixes shipped (commits `463fc36..6c44a07`); the
> `v0.7.0-e2.5` tag is held pending an interactive re-dogfood that
> requires the user's local desktop. **When the user is remote/SSH or
> otherwise can't dogfood, the next work to pick up lives in
> [`docs/queue.md`](docs/queue.md)** — five items ordered by descending
> execution difficulty, each briefed enough that a fresh agent can
> draft a spec/plan via `superpowers:brainstorming` +
> `superpowers:writing-plans` and execute. Top item: E2.6 (theme +
> zoom). Smallest item: code review of the cursor-fix commit chain.
>
> **2026-05-07 — Branch is on D5-first posture. v1.0 plan retired.**
> Authoritative posture for the branch is
> `docs/handoff/2026-05-07-pivot-to-d5-first.md`. Collab (D5) ships
> before any public-API freeze, before any Corbomite-facing migration
> guide, and before any further perf or facade work. Operating
> principle: one arc at a time, no side work, retirement is explicit.
>
> **2026-05-09 — E2.5 dogfood + Option B selection architecture landed
> (commit `c7da731`). Setext heading support is the next active piece;
> spec + plan committed as `acdb647`, ready to execute.**
>
> The E2.5 dogfood findings (D1 Ctrl+S, D2 multi-block clipboard, D3
> arrow-key selection, D4 right-click context menu, D6 Shift+↓ anchor,
> D7/D8 double/triple-click, D9 `--new` flag) are all resolved. D5
> doc-wide undo deferred per the dogfood doc's recommendation. A
> follow-up dogfood pass surfaced multi-line Shift+arrow wonkiness and
> dual-selection-state bugs — addressed by **Option B**: TextEdit
> reduced to renderer + cursor + IME, `LiveSelectionView` is the single
> source of truth for selection, all keyboard nav routed through
> `LiveNavigationController`, `selectByMouse: false` on every text-bearing
> delegate. Same commit fixed the kind-string-mismatch and
> heading-prefix-doubling bugs in the structured-paste path.
> Re-dogfood found one further substantive gap → setext.
>
> **The `v0.7.0-e2.5` tag is held until setext support lands** (and a
> final re-dogfood pass confirms the editor handles Obsidian-style
> setext-bearing notes correctly). E2.5 phases A–I + dogfood remediation
> + Option B are all green at 185/185 fast tests.
>
> **Fresh-agent start for setext (THE active work):**
>
> 1. **Read the spec:** `docs/specs/2026-05-09-setext-heading-support-design.md`.
>    §3 (storage) and §6 (kind-transition) are load-bearing for the design;
>    §9 (edge cases) is the test-coverage list.
> 2. **Read the plan:** `docs/plans/2026-05-09-setext-heading-support.md`.
>    Six phases, TDD per task. Build cap: `-j 8` always. Test commands
>    are at the top of the plan.
> 3. **Execute** task by task. The plan is intended for either
>    subagent-driven or executing-plans inline use; pick whichever you
>    were dispatched for.
> 4. **Re-dogfood** at end of Phase 5 against the
>    `/tmp/setext-dogfood.md` fixture the plan creates. After Phase 6's
>    activity-log entry, the `v0.7.0-e2.5` tag becomes a candidate.
>
> Past-context references (read only if you need provenance for a
> specific decision; not required for executing the plan):
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

`exploration/new-foundation` diverges substantially from `master`. Don't
attempt to merge the old leaves back — they're intentionally gone. If
you need to preserve a piece of behavior from the deleted leaves,
re-implement it inside the new layout.
