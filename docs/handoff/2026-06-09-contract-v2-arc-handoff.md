# Handoff — MarkdownView contract v2 arc, Tasks 9–13

**Date:** 2026-06-09 (late session)
**For:** the next session picking this arc up fresh.
**Spec:** `docs/specs/2026-06-09-markdownview-contract-v2-design.md`
**Plan:** `docs/plans/2026-06-09-markdownview-contract-v2.md` (progress
banner at top has the per-task commit table)
**Process used so far:** `superpowers:subagent-driven-development` —
fresh implementer subagent per plan task, then a spec-compliance
review, then a code-quality review (qt-code-reviewer), fixes looped
until approved. It has worked well; recommend continuing it.

## State at handoff

- Branch `master`, working tree **clean**, pushed through this handoff
  commit. Tasks 1–8 of the 13-task plan are complete — implemented,
  two-stage-reviewed, committed (SHAs in the plan's progress banner).
- **Baseline: 266/269** via
  `scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`. The 3
  failures are the long-documented queue-#10 binaries
  (`tst_live_render_e2_nav_shift_extend`,
  `tst_live_render_focus_chokepoint_invariant`,
  `tst_live_render_cursor_typing_invariant`) — deterministic,
  pre-existing, NOT part of this arc. Any other failure is a
  regression.
- Six test binaries were added by this arc so far:
  `tst_markdown_view_base`, `tst_format_ops`,
  `tst_view_contract_{source,styled,live}`, `tst_styled_find_adapter`.
- Two invariant-4 falsifiability proof pairs live in history
  (`cf601e62`/`bcbeb683` cursor mapping; `bffcc874`/`bab51a6d`
  read-only gates). Keep doing this for seam tasks.

## What the completed half delivered (one paragraph)

`Markoff::MarkdownView` now carries the real contract: find
attach/detach, base-implemented `undo()/redo()` over `undoD2`
(read-only-gated), theme/fontScale stores with signals, six format
verbs. Source and styled have contract suites sharing
`libs/markoff-core/tests/ViewContractChecks.h`; the ~330-line format-op
logic is hoisted into widget-free `Markoff::FormatOps` (source wraps
it, styled implements its verbs over it with a conservative
table-frame guard); styled has frame-aware find
(`StyledFindAdapter` over the `BlockPositionWalk` extracted from
FormatPass); the live leaf has an honest `CursorPos` mapping (flat-line
model, spec §3) over `LiveCursorState` and real `setReadOnly` via
mutation-ingress gates on a single `LiveListModelBinding::readOnly`
flag.

## Next: Task 9 (live theme/fontScale forwarding + verb delegation)

The interrupted implementer already wrote the RED-phase test slots —
**apply `docs/handoff/2026-06-09-task9-red-slots.patch`** onto
`libs/markoff-live/tests/tst_view_contract_live.cpp` and they're your
failing tests (verify red, then implement). Context the patch's author
discovered, plus prepared implementation notes:

- `LiveListModelBinding::setTheme(const Markoff::Theme *theme)` takes a
  **pointer** and its `theme()` is **never null** — the binding rotates
  two internal copy-buffers (`Private::themeBuffers` in
  `LiveListModelBinding.cpp`). So a non-null check cannot falsify
  forwarding; the patch's slots probe theme **content** instead. The
  copy-buffer rotation also means repeated `setTheme` with the same
  widget-owned pointer is fine.
- Implementation shape (EditorWidget.h/.cpp only, mirror the
  `setReadOnly` override's comment style): `setTheme` = base call
  (store + signal) → keep a widget-owned `Markoff::Theme themeCopy` in
  the Private struct → `d->binding->setTheme(&d->themeCopy)`.
  `setFontScale` = base call (clamp + store + signal) →
  `d->binding->setFontScale(fontScale())`. Verbs = one-liners
  triggering `d->binding->actionController()`'s QActions
  (`boldAction()->trigger()` etc.; `setHeadingLevel(level)` guards
  0..6 and triggers `heading<level>Action`). The QActions are the
  production path Corbomite uses — delegation, not reimplementation.
- For the verb test: prime a selection via the cursor authority
  (`begin`/`extend` — the production drag path; Task 8's
  `readOnly_disables_edit_actions_but_not_copy_zoom` slot shows the
  idiom) so the actions are enabled per `updateEnabledStates`.
- Commit message: `feat(live): theme/fontScale/format verbs honor the
  base contract (spec §4.3-4.4)` + the Co-Authored-By trailer used
  throughout.

## Then Tasks 10–13 (plan has full text)

- **10** EditorContext feed, source+styled (change-gated
  `contextChanged`; `BlockKindNames` gains `Table`).
- **11** EditorContext feed live + scroll-signal consistency sweep.
- **12** Source fontScale + contract-suite consolidation. Also fold in:
  the Task-1 quality reviewer's note about styled's `setFontScale`
  local-sync comment (styled keeps a local `m_fontScale` for
  StyleApplier; consolidate or document while touching fontScale).
- **13** Corbomite adoption brief + docs. **Accumulated items Task 13
  must reconcile** (gathered from the reviews):
  1. Spec §5 signature drift: shipped `FormatOps` returns
     `std::optional<QtRange>` (nullopt = no edit, leave cursor) — update
     the spec snippet to match (reviewed and approved deviation).
  2. StyledFindAdapter constraints to document (styled CLAUDE.md +
     brief): attach-after-setDocument required (doc-swap while attached
     leaves the d2 connection on the old doc — same property as the
     source adapter); in-frame matches = counted, no highlight,
     navigation scrolls to the frame.
  3. Styled format-verb frame-guard policy (no-op + qWarning at/after
     the first table frame) → styled CLAUDE.md.
  4. Find-highlight color is hardcoded soft yellow in BOTH adapters
     (theme follow-up) → ensure a queue entry exists.
  5. New Discipline Log entry from Task 8 (already in queue.md):
     `LiveCursorState::selectAllBlocks` sets `m_selectionExtended`
     after emitting `selectionChanged` → stale QAction states on
     Ctrl+A. Untouched, logged.
  6. Per-lib CLAUDE.md "Public surface" updates (new overrides on all
     three leaves) + root CLAUDE.md status block + STATUS.md.

## Discipline reminders for the resuming session

- Build with `-j 8` max; tests offscreen only via
  `scripts/run-tests.sh`; never `--direct`/`--nested`.
- Classify drift-vs-bug before touching any failing test; the donor
  contract always wins over a plan sketch.
- Seam tasks (anything touching cursor/focus/mutation paths) need the
  falsifiability proof pair committed + reverted in history.
- Commit per task with the `Co-Authored-By: Claude Fable 5
  <noreply@anthropic.com>` trailer; push at session end.
