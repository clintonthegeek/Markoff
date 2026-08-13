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

**277/277 (100%)** via the full `scripts/run-tests.sh` (including
`tst_realistic` and `tst_benchmark`), verified 2026-08-13. Queue #10
is fully closed — all 6 previously-failing slots across
`tst_live_render_focus_chokepoint_invariant` and
`tst_live_render_cursor_typing_invariant` are green; the "known
failures" language is retired. See queue.md #10 for the two
architectural decisions (chokepoint-owns-caret-after-undo;
collapsed-cursor-is-a-valid-paste-target) and the two extra bugs those
fixes uncovered (delegate focus-before-cursorPosition ordering;
`applyFlatEdit`'s block-boundary byte-ambiguity).

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
- ~~**#10**~~ — closed 2026-08-13: all 6/6 previously-failing slots
  green. Undo/redo caret loss fixed by having the chokepoint re-anchor
  on a vanished focused block (not a `UndoLog` extension); a
  focus-before-cursorPosition ordering bug in three QML delegates
  fixed alongside. Paste-without-selection fixed by falling back to
  the collapsed caret as the insertion point, which uncovered and
  fixed an `applyFlatEdit` block-boundary byte ambiguity.
- ~~**#11**~~ — closed 2026-08-12: legacy `SearchEngine::findAll`/
  `CompletionDetector` deleted (zero production callers, confirmed).
- ~~**#12**~~ — closed 2026-08-12: `EmbedRegistry`/`EmbedDepthGuard` now
  have an 11-slot test binary.
- **#13** — *(retired queue item numbering; was "source cursor/selection
  translation rewrite" — renamed from old TODO.md)*. See queue.md.
- ~~**#14**~~ — closed 2026-08-13: `SourceFindAdapter`/`StyledFindAdapter`
  now read `Theme::Slot::SearchMatchBackground`/`SearchActiveMatchBackground`
  instead of a hardcoded soft yellow, matching the live leaf; both also
  gained active-match distinction and re-render on `themeChanged`.
- ~~**#15**~~ — closed 2026-08-12: `Cmd::changeKind`'s real code path
  (`d2SetBlockKind`) never bumped `structuralEditSequence`; fixed, both
  leaves now recompute `contextChanged` on structural change without a
  caret move.
- ~~**#16**~~ — closed 2026-08-12: `setFontScale`→`StyledTableRenderer`
  path now covered via observable frame geometry (`cellPadding`).
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
