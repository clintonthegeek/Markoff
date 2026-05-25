# Handoff → Corbomite devs: foundation merge complete

**From:** Markoff (upstream)
**Date:** 2026-05-25
**Markoff branch:** `master`
**Markoff tip:** `1e0f332`
**Markoff tag:** `v0.7.0-freeze`

In response to: `/home/clinton/dev/Corbomite/docs/handoff/2026-05-25-to-markoff-green-light-foundation-merge.md`

---

## TL;DR

**Merge done. Re-pin and merge your port branch whenever you're ready.**

Master is now the foundation tree. The merge committed at `3c7afa9` with
`exploration/new-foundation`'s tip (`14abc3c`) as its second parent. A
follow-up `1e0f332` removed three root-level duplicate docs that came along
in the merge (they were already preserved at `docs/archive/`). Master tip is
tagged `v0.7.0-freeze` for your re-pin.

The old v0.6.x tree is preserved at tag `v0.6.x-final` (still on Codeberg).
Nothing was lost.

---

## What you need to do

1. **Re-pin your submodule to `v0.7.0-freeze`** (or master tip `1e0f332`; they're
   the same commit).
2. **Merge `port/foundation-exploration` → Corbomite `master`** when you're
   comfortable.
3. After your merge, the Markoff side of the joint roadmap continues normally:
   E4 Phase H dogfood pending, then E5 (math/Mermaid parity), E6 (recipe
   distillation).

The merge ordering constraint named in your handoff is now satisfied — we
went first.

## What changed in the prep window between your handoff and the merge

Three small commits landed on `exploration/new-foundation` after the pin you
were tracking (`03f088a`) and before the merge:

| Commit | What |
|---|---|
| `425629b` | **Re-vendored `libs/jkqtmathtext`** — the branch was carrying a cross-repo symlink to `/home/clinton/dev/Corbomite/libs/jkqtmathtext` that wouldn't resolve outside this machine. Replaced with the real ~155-file tree from master's prior `2b7b3e7` commit. The CMake wiring was already correct; no build-side changes. |
| `457ad34` | Pulled three master-root design notes (`2026-04-21-c3-*.md`, `undo-strategy-response.md`) into `docs/archive/` so they survive the tree swap. |
| `14abc3c` | `.gitignore` covers `selection*.txt` and `.superpowers/` (local-only scratch). |

Then the merge:

| Commit | What |
|---|---|
| `3c7afa9` | The merge itself (`--no-ff`). One trivial CLAUDE.md conflict, resolved foundation-side (master's CLAUDE.md was the v0.6.x-era one). |
| `1e0f332` | Removed three root-level c3 doc duplicates that came along (canonical copies remain at `docs/archive/2026-04-21-*.md`). |

The result is a 1738-file diff (+259k / -776k lines vs the prior `2b7b3e7`
master). The net deletion is the four old leaves you said you didn't need.

## Tags now on Codeberg

| Tag | Commit | Purpose |
|---|---|---|
| `v0.7.0-freeze` | `1e0f332` | **Recommended re-pin target.** Master tip after the merge + cleanup. |
| `v0.6.x-final` | `2b7b3e7` | The old master tree, recoverable forever via `git checkout v0.6.x-final`. |
| `archive/tri-view-phase-a` | `382e262` | The dead-end tri-view branch (preserved before deletion). |
| `v0.7.0-e1` / `e2` / `e2.5` / `e2.6` | (foundation history) | Earlier E-arc milestones. |
| `v0.7.0-find-highlights` | (foundation history) | Find UI highlights milestone. |

The branches `exploration/new-foundation` and `feature/tri-view-phase-a` are
**deleted** locally and on Codeberg now that they're reachable through the
merge commit and the archive tag. The history is intact — `git log master`
walks it fine.

## On the gating items from your handoff

Restating them with current status so they don't get lost in this transition:

| Your degradation | Markoff phase that addresses it | Status |
|---|---|---|
| Embeds non-functional (#7) | E3 | pending |
| Callouts not rendered | E3 | pending |
| Mermaid no-op (#6) | E5 | pending |
| Math block parity | E5 | pending |
| Reading-mode features (HoverPopover, checkbox-toggle, `setCursorLine`) | Pending direction decision: restore Reading leaf vs. Capabilities::Editable=false on Live | **Awaiting steer from you** (you flagged this as the most useful one to resolve) |
| Word count not updated (#10) | Small add — `wordCount` + `wordCountChanged` on `MarkoffDocument` | Pending; cheap |
| Undo/redo (#11) | Wire to `MarkoffDocument::d2UndoLog` | Your side; flag if intended path differs |

The Reading-vs-non-editable-Live decision is on our radar but not yet
specced. If you have a preference based on your port experience, surface it
on your side and we'll fold it in.

## On the freeze spec

Per your handoff Step 2: we have **not** drafted a `markoff-core` freeze spec
yet — agreed it should be driven by your real port pressure, not the
speculative `2026-05-20-markoff-core-freeze-shape-design.md` draft (which is
banner-marked draft-reference-not-action-plan in the tree). When you're ready
to surface the API surfaces your port actually leans on, we'll spec from that.

## Phase H still pending

E4 Phase H (dogfood + tag `v0.7.0-e4`) is on master but not yet ticked off.
The dogfood fixture `libs/markoff-live/tests/fixtures/tables_dogfood.md` is
matched 1-to-1 with the checklist in
`docs/handoff/2026-05-22-e4-dogfood-request.md`. Walking it is a near-term
todo on our side; doesn't gate your work.

---

*Ping us if anything surprises you after the re-pin.*
