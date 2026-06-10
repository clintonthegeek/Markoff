# Markoff — live status board

> **The** single live status board. Replaces the per-arc boards
> (`docs/d-arc/d-arc-status.md`, `docs/e-arc/e-arc-status.md`), which
> went stale twice under the board-per-arc convention and are now
> closed with disposition banners. Update this file when the
> workfront, baseline, or open-item set changes; move superseded
> dated entries to `docs/STATUS-LOG.md`.

**Last updated:** 2026-06-10 (contract-v2 arc ALL 13 tasks complete)

## Workfront

**Contract-v2 arc (API finalization for Corbomite) is COMPLETE.**
All 13 tasks landed; Corbomite adoption brief written at
[`docs/handoff/2026-06-09-corbomite-api-adoption-brief.md`](handoff/2026-06-09-corbomite-api-adoption-brief.md).
Spec `docs/specs/2026-06-09-markdownview-contract-v2-design.md`;
plan `docs/plans/2026-06-09-markdownview-contract-v2.md` (full task table
with SHAs).

**Next workfront: Corbomite adoption.** The brief is in hand. The
migration table (find-attach switch, undo/redo dual-authority fix, theme
propagation, format-verb re-wire, contextChanged hook-up, cursor/scroll
APIs, reading-mode find) is documented in the brief. No Markoff code
changes are needed on the Corbomite side of the adoption — it is purely
a consumer call-site migration.

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
| API finalization for Corbomite (contract-v2 arc) | complete 2026-06-10 — all 13 tasks landed | brief: `docs/handoff/2026-06-09-corbomite-api-adoption-brief.md` |

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
As of 2026-06-10 (verified against Corbomite `b6ae2c0f`) the pin is
behind master. **Re-pin guidance: jump to the commit containing Task 13**
(the commit that adds the adoption brief). Never re-pin into the
`8c13c5d..079ac1f` window, which renders styled tables but contains the
list-after-table SIGSEGV fixed in `b1b238f`.
