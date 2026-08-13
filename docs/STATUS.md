# Markoff — live status board

> **The** single live status board. Replaces the per-arc boards
> (`docs/d-arc/d-arc-status.md`, `docs/e-arc/e-arc-status.md`), which
> went stale twice under the board-per-arc convention and are now
> closed with disposition banners. Update this file when the
> workfront, baseline, or open-item set changes; move superseded
> dated entries to `docs/STATUS-LOG.md`.

**Last updated:** 2026-08-12 (post-idle resync; contract-v2 arc tail +
Corbomite adoption in progress)

## Workfront

**Contract-v2 arc (API finalization for Corbomite) is COMPLETE**, plus
four follow-on commits landed after the 2026-06-10 "all 13 tasks"
banner: `caretRect()` base contract + per-leaf implementations
(`39c5423b`..`cdd2bd3c`), `MarkdownView::replaceMatches` +
`FindController::selectMatchAtOrAfter` (`ee77b157`, `b349f122`), and a
fix preserving adapter-owned `findSpans` across `applyOps` row updates
(`c3b5070f`). Spec `docs/specs/2026-06-09-markdownview-contract-v2-design.md`;
plan `docs/plans/2026-06-09-markdownview-contract-v2.md` (full task table
with SHAs); caret-rect mini-spec
`docs/specs/2026-06-11-caret-rect-contract-design.md`; replaceMatches
spec `docs/specs/2026-06-12-replace-matches-primitive-design.md`.

**Workfront: Corbomite adoption.** Brief:
[`docs/handoff/2026-06-09-corbomite-api-adoption-brief.md`](handoff/2026-06-09-corbomite-api-adoption-brief.md).
**Corbomite adoption is done as of 2026-08-12** (Corbomite `e9d70a8b`):
re-pinned to Markoff master `2a551cde`; the migration-table call-site
work (find-attach switch, undo/redo dual-authority fix, theme
propagation, format-verb re-wire, contextChanged hook-up, cursor/scroll
APIs, reading-mode find) turned out to already be implemented on
Corbomite's `feature/find-replace` branch from an earlier adoption pass
— verified against the brief's migration table item-by-item. Corbomite
full ctest: 116/117 (the one failure, `tst_benchmark_layout`, is an
unrelated `forcegraph` submodule benchmark timing out under build
contention, not a correctness regression).

## Test baseline

**272/274** via `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
(re-verified 2026-08-12). Queue #10 triage closed 2/6 previously-failing
slots this session (`tst_live_render_e2_nav_shift_extend`, both slots —
reshaped to match `LiveNavigationController`'s correct current
behavior). The 2 remaining failing binaries
— `tst_live_render_focus_chokepoint_invariant` (3 slots: undo/redo
caret restoration, needs a `UndoLog` selection-snapshot extension) and
`tst_live_render_cursor_typing_invariant` (1 slot: paste-without-
selection silently no-ops, needs a `LiveCursorState`/
`LiveClipboardController` decision) — are **deterministic, not
flakes**, diagnosed in detail in queue #10 with a concrete next step
for each. `tst_realistic` passes; `tst_benchmark` excluded from the
fast loop for time.

## Arc dispositions

| Arc | State | Record |
|---|---|---|
| C-restoration | retired 2026-05-04 | `docs/archive/c-restoration-arc/` |
| D-arc (per-block CRDT foundation) | complete 2026-05-08 | `docs/d-arc/` (closed board) |
| E-arc (live-render maximalist prototype) | **dormant since 2026-05-25** — E1–E2.6 complete + tagged; E3a shipped, never tagged (`v0.7.0-e3a` not created; dogfood signoff never recorded); E4 shipped phases A–G, Phase H checklist never executed, `v0.7.0-e4` not created; E3b+/E5/E6 not started | `docs/e-arc/` (closed board with disposition banner) |
| WP unification + styled leaf (2026-05-26 →) | **active** | specs/plans dated 2026-05-26..31; `docs/STATUS-LOG.md` banners |
| API finalization for Corbomite (contract-v2 arc) | complete 2026-06-10 — all 13 tasks landed | brief: `docs/handoff/2026-06-09-corbomite-api-adoption-brief.md` |

## Open items (summary — details in `docs/queue.md`)

- ~~**#8.3**~~ — closed 2026-08-12: source-view list-item markers now
  render via paint-time decoration.
- **#10** — deterministic live-test failures; 2/6 slots closed
  2026-08-12, 4 remain (3 undo/redo caret restoration, 1
  paste-without-selection) with written diagnoses and next steps.
- ~~**#11**~~ — closed 2026-08-12: legacy `SearchEngine::findAll`/
  `CompletionDetector` deleted (zero production callers, confirmed).
- **#12** — `EmbedRegistry` has zero test coverage despite being
  Corbomite-consumed.
- **#13** — *(retired queue item numbering; was "source cursor/selection
  translation rewrite" — renamed from old TODO.md)*. See queue.md.
- **#14** — find-highlight color hardcoded soft yellow in all three leaf
  adapters; theme-integration follow-up.
- **#15** — `contextChanged` staleness: kind-change without caret move
  on source + styled (low severity; see spec §7 deviation).
- **#16** — styled `StyledTableRenderer::setFontScale` path untested.
- **Styled tables** — deferred: in-grid cell edit, structural row/col
  ops, alignment context menu, source-reveal flip (seam landed
  2026-05-30; queue/banner record in `docs/STATUS-LOG.md`).
- **Release scaffolding** — LICENSE + README landed 2026-06-09;
  install/export rules + header tiering remain open for a
  non-submodule consumer.

## Corbomite pin

Corbomite tracks Markoff via submodule at `libs/markoff-family`.
As of 2026-08-12 (Corbomite `fde31e82`) the pin is at Markoff
`b349f122`, 2 commits behind current master (`2a551cde`) — Corbomite
has been re-pinning after nearly every Markoff commit since the
adoption brief landed. **Re-pin guidance:** jump to current master when
next touched. Never re-pin into the `8c13c5d..079ac1f` window, which
renders styled tables but contains the list-after-table SIGSEGV fixed
in `b1b238f`.
