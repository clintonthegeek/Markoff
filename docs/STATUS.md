# Markoff — live status board

> **The** single live status board. Replaces the per-arc boards
> (`docs/d-arc/d-arc-status.md`, `docs/e-arc/e-arc-status.md`), which
> went stale twice under the board-per-arc convention and are now
> closed with disposition banners. Update this file when the
> workfront, baseline, or open-item set changes; move superseded
> dated entries to `docs/STATUS-LOG.md`.

**Last updated:** 2026-06-09 (late — contract-v2 arc half landed)

## Workfront

**The MarkdownView contract-v2 arc (API finalization for Corbomite) is
mid-flight: plan Tasks 1–8 of 13 complete, Tasks 9–13 remain.**
Resume from
[`docs/handoff/2026-06-09-contract-v2-arc-handoff.md`](handoff/2026-06-09-contract-v2-arc-handoff.md)
(per-task commits, review dispositions, Task-9 RED patch ready to
apply). Spec
`docs/specs/2026-06-09-markdownview-contract-v2-design.md`; plan
`docs/plans/2026-06-09-markdownview-contract-v2.md` (progress banner).
Landed so far: honest base contract (find/undo/theme/fontScale/verbs),
FormatOps hoist, styled frame-aware find + verbs, live cursor mapping +
read-only ingress gates (both with falsifiability proof pairs in
history).

## Test baseline

**266/269** via `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`
(verified 2026-06-09 at handoff; the arc added 6 contract/find/format
test binaries). The 3 failing binaries
— `tst_live_render_e2_nav_shift_extend` (2 slots),
`tst_live_render_focus_chokepoint_invariant` (3 slots),
`tst_live_render_cursor_typing_invariant` (1 slot) — are
**deterministic, not flakes**, and queue #10 tracks their triage.
`tst_realistic` passes; `tst_benchmark` excluded from the fast loop for
time.

## Arc dispositions

| Arc | State | Record |
|---|---|---|
| C-restoration | retired 2026-05-04 | `docs/archive/c-restoration-arc/` |
| D-arc (per-block CRDT foundation) | complete 2026-05-08 | `docs/d-arc/` (closed board) |
| E-arc (live-render maximalist prototype) | **dormant since 2026-05-25** — E1–E2.6 complete + tagged; E3a shipped, never tagged (`v0.7.0-e3a` not created; dogfood signoff never recorded); E4 shipped phases A–G, Phase H checklist never executed, `v0.7.0-e4` not created; E3b+/E5/E6 not started | `docs/e-arc/` (closed board with disposition banner) |
| WP unification + styled leaf (2026-05-26 →) | **active** | specs/plans dated 2026-05-26..31; `docs/STATUS-LOG.md` banners |
| API finalization for Corbomite | **next** — gap list from the 2026-06-09 audit | see "Open items" below |

## Open items (summary — details in `docs/queue.md`)

- **#8.3** — source-view list-item marker reconstruction (source widget
  shows `foo` for `- foo`).
- **#10** — deterministic live-test failures (the "3 known offscreen
  flakes" relabeled honestly; 6 slots, classifications inside).
- **#11** — legacy flat-buffer APIs (`SearchEngine::findAll`,
  `CompletionDetector`) documented as legacy-coordinate-space; retire
  or migrate with the legacy buffer.
- **#12** — `EmbedRegistry` has zero test coverage despite being
  Corbomite-consumed.
- **Styled tables** — deferred: in-grid cell edit, structural row/col
  ops, alignment context menu, source-reveal flip (seam landed
  2026-05-30; queue/banner record in `docs/STATUS-LOG.md`).
- **API finalization** (from the 2026-06-09 audit; spec TBW via
  brainstorming): honest `cursorPosition`/`setReadOnly` on
  `Live::EditorWidget`; find-attach on `MarkdownView` base;
  `FindController` integration in `markoff-styled`; real undo API on
  `Source::Editor` (Corbomite currently calls `plainTextEdit()->undo()`,
  bypassing `undoD2` — dual-authority risk, INVARIANTS §3); theme
  parity; `Markoff::Live` CMake alias target; hoist the ~321 lines of
  format ops out of `Source::Editor` into a shared helper before
  styled grows a copy.
- **Release scaffolding** — LICENSE + README landed 2026-06-09;
  install/export rules + header tiering remain open for a
  non-submodule consumer.

## Corbomite pin

Corbomite tracks Markoff via submodule at `libs/markoff-family`.
As of 2026-06-09 the pin is `ddf5e9a8` (5 commits behind master).
**Re-pin guidance: jump to `b1b238f` or later** — never into the
`8c13c5d..079ac1f` window, which renders styled tables but contains
the list-after-table SIGSEGV fixed in `b1b238f`.
