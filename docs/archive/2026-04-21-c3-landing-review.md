# C3 landing review — concerns before C7 begins

Written after a read of `~/dev/Corbomite/libs/markoff-family/` at `v0.6.0`, the 44 commits ahead of canonical `~/dev/Markoff/` (`v0.3.0`). Scope: a pre-C7 checkpoint on what `v0.6.0` actually delivered, what shape it landed in, and where the weight-bearing seams are that haven't had soak time. The architectural direction is right. The velocity has specific risk markers I'd like named before they become the next phase's surprises.

## What went right

Worth stating up front so the rest reads as fine-tuning rather than generic pushback:

- **The shape is correct.** `CanonicalBuffer` / `InMemoryCanonicalBuffer`, `CursorPosition` opaque handle, `MarkdownDelta` command, `Origin` enum, `ParsePool`, per-leaf `setDocument(MarkoffDocument *)` binding — these are the right primitives. The spec at `docs/specs/2026-04-20-phase-c3-markoff-document-content-authoritative.md` captures symmetric-B accurately, including the explicit carve-out that a Live scene-graph rewrite ("A(i) in the brainstorm") is a future phase and not C3.
- **All three leaves landed in one pass.** `SourceEditor::setDocument`, `ReadingView::setDocument`, `Editor::setDocument` all wire via the same abstraction. Cross-mode undo tests (`tests/markoff/tst_cross_mode_undo.cpp`) and canonical-interop tests (`tst_canonical_interop.cpp`) exist rather than being deferred.
- **The CRDT door stayed open.** `CanonicalBuffer` as an interface with `InMemoryCanonicalBuffer` as the sole concrete means a `CrdtCanonicalBuffer` (Phase E) is a drop-in substitute rather than a structural rewrite. The non-goal callout in the spec §2 is correctly scoped.
- **The async parse worker lands via `ParsePool`/`ParsePoolWorker`** rather than each leaf carrying its own debounce. Spec §3.3 Phase-A-deferred items got absorbed as advertised.

So the design is right. The concerns below are about *soak*, not *shape*.

## Concern 1: `m_sceneNeedsFullRebuildOnNextParse` is an escape hatch at the critical seam

`3a5f0a8 markoff-live: inbound canonical-delta splicing + multi-item rebuild flag` in `SceneCoordinator::applyCanonicalDelta`:

```cpp
void SceneCoordinator::applyCanonicalDelta(qsizetype offset, qsizetype removed,
                                            qsizetype inserted)
{
    // ... single-item splice path ...

    // Does the delta span beyond this item's canonicalEnd?
    if (offset + removed > qsizetype(block.canonicalEnd)) {
        m_sceneNeedsFullRebuildOnNextParse = true;
        // conservative offset-map update + early return
    }
    if (!block.item || !block.item->isTextItem()) {
        m_sceneNeedsFullRebuildOnNextParse = true;
        // conservative offset-map update + early return
    }
    // out-of-range: same pattern
}
```

The commit message is candid: "multi-item delta ... cannot splice piecemeal. Mark for full rebuild on next parseUpdated and apply a conservative offset update." That's a confession that the per-item offset map can't handle the general case, resolved by tearing down and rebuilding the entire scene on the next `parseUpdated`.

The scenarios this path fires on are not exotic:

- **Source-mode paste of a multi-paragraph block** → spans Live items → full Live scene rebuild on the next reparse.
- **Multi-line delete across a heading boundary** → spans items → full rebuild.
- **Edit touching an image block (non-text item)** → full rebuild.
- **External reload via `documentReloaded`** → full rebuild. This one is intentional and correct; reload is the acknowledged reset point. The other three are not.

Each full rebuild costs:

- Focus loss (in-progress cursor position on a Live block that gets destroyed)
- Scroll jump (scene is re-laid out from scratch)
- Selection-highlight / math-reveal / fold-gutter ephemeral state reconstructs from cold
- Any `TextControl` state inside the rebuilt items resets

Whether this is visually jarring in human use is an empirical question. The velocity from spec (12:00) to `v0.6.0` (14:37) didn't leave time to answer it empirically.

**This is the exact seam the original A-vs-B pushback named as the hardest engineering problem in C3.** Quoting:

> "Live Preview's Editor is the most architecturally delicate of the three leaves. Refactoring its per-block TextControl model onto a shared-buffer-with-slices model is the kind of change where we may end up tagging an alpha (v0.6.0-alpha.1) and doing several iteration commits before v0.6.0 proper."

What landed is the right shape. The seam that warranted several-alphas of iteration shipped with an escape-hatch instead.

**Ask:** instrument `m_sceneNeedsFullRebuildOnNextParse` with a logged counter (qDebug in dev builds; Qt category logging so it can be toggled). After a week of dogfooding in `markoff-testapp` and CorbomiteApp with realistic documents and cross-mode editing patterns, count the hits. If it fires more than a few times per hour of normal use, the claim "C3 preserves Live's per-block TextControl machinery intact" is partially false — the seam is being papered over with full-scene rebuilds on common operations. That's diagnostic, not blocking; the action it recommends is either (a) extending the single-item splice path to handle multi-item cases, or (b) accepting full rebuild as the model and optimising it to be fast and visually quiet.

## Concern 2: Two test quarantines with no rewrite plan

`1c33098 markoff-core/tests: quarantine legacy MarkoffDocument slots` and `1205a38 markoff-core/tests: quarantine search/replace controller tests` landed during C3 implementation. Those aren't "updated for the new API" commits — they're "disabled pending follow-up" commits. The net test delta for C3 is:

- 2 new cross-cutting tests (`tst_cross_mode_undo.cpp` @ 114 lines, `tst_canonical_interop.cpp` @ 120 lines)
- 3 new per-leaf attach tests (`tst_source_canonical_attach.cpp`, `tst_reading_canonical_attach.cpp`, `tst_live_canonical_attach.cpp`)
- Existing `MarkoffDocument` tests and search/replace controller tests quarantined

For the largest architectural change in the Phase C roadmap — one that touches all three leaves, the canonical buffer, the undo stack, the parse pool, and the command model — this is a light test surface. Coupled with the quarantines, the message is: "we know some of the old behaviour may have regressed; we'll come back to it."

**Ask:** before any C7 commit, un-quarantine both test files. For each test:

- Rewrite it against the new API if the behaviour it asserts is still a requirement, OR
- Delete it with a one-line rationale in the commit message if the behaviour is no longer part of the contract (e.g., tests that asserted against the retired `textDocument()` accessor are legitimately obsolete).

Leaving them `#if 0`'d or skip-tagged is debt with the shape of "we don't know if this regressed," and that shape compounds. The C3 spec's acceptance criteria should be updated with the un-quarantine requirement as an explicit gate.

## Concern 3: Anchor-math bug-fix landed seven minutes after the interface

Commit timeline:

- `12:40 aaaf57d markoff-core: add CanonicalBuffer interface + InMemoryCanonicalBuffer`
- `12:47 93e7d40 markoff-core: fix right-bias anchor advance on pure-insert`

Seven minutes from "add the abstraction" to "fix a bug in the abstraction." Fine in isolation — that's how first-cut implementations usually go. What concerns me is the family of bugs anchor-under-edit tends to produce:

- Right-bias at start-of-text vs end-of-text
- Left-bias under delete-that-collapses-to-me vs delete-that-spans-me
- Bias under replace (remove + insert as one delta vs two)
- Bias under macro-grouped multi-delta edits
- Bias stability under undo then redo
- Bias stability under `contentsReloaded` (anchor should either invalidate or reset; which?)

One landed bug in seven minutes is a signal, not a refutation — it means the test coverage found the first bug. It does not mean the test coverage found the other five that tend to come in this family. `InMemoryCanonicalBuffer` anchor math is load-bearing for every persistent cursor position across the three leaves, including search's current-match cursor, scroll-position-at-visual-line memory, and fold-state anchoring.

**Ask:** a targeted test pass on anchor semantics before C7, covering the six edge cases above. This is a narrow, bounded piece of work (~a day) and it's the kind of thing that catches bugs *before* they manifest as user-visible "my scroll position jumped when my colleague typed" reports during actual CorbomiteApp use.

## Concern 4: Velocity vs. soak mismatch

Receipts:

- `12:00 2e7e7d1` — C3 spec drafted
- `12:24 d9ac36b` — C3 plan drafted (25 tasks)
- `14:37 cf37d0e` — `v0.6.0` tagged, status board updated to "C3 done"

Spec-to-tag: **2 hours 37 minutes.**

`v0.6.0` is presented as a milestone release — the tag Corbomite should pin to before C7 starts. But nobody has used `v0.6.0` in anger for more than the time it took to tag it. The intermediate `v0.6.0-alpha.1` existed for a few minutes. There is no evidence in the commit record of any dogfooding session with real CorbomiteApp use against a `v0.6.0`-pinned submodule.

The original expected cadence (from the design conversation):

> "Significant refactor of Markoff::Live::Editor's rendering pipeline ... the kind of change where we may end up tagging an alpha (v0.6.0-alpha.1) and doing several iteration commits before v0.6.0 proper."

The Corbomite agent acknowledged this explicitly in his lock-in response:

> "Honest cost disclosure on A: ... tagging an alpha (v0.6.0-alpha.1) and doing several iteration commits before v0.6.0 proper."

What actually happened: one alpha tag, thirty-four minutes of elapsed time between the alpha and the full release, zero iteration commits between them.

**Ask:** re-tag `v0.6.0` as `v0.6.0-alpha.2` (append-only, so this is a *new* tag, not a moved tag). Leave the existing `v0.6.0` tag in place but annotate it in the status board as "premature — see `-alpha.2` for the re-badged stable candidate." Keep the Corbomite submodule pinned to `v0.6.0-alpha.2` for a week of active CorbomiteApp use with realistic multi-paragraph docs, frequent cross-mode switches, and the `m_sceneNeedsFullRebuildOnNextParse` counter instrumented. Only after that week, re-tag as `v0.6.1` (or cut a fresh `v0.6.0` retrospectively if the tag list can stomach it; append-only makes this awkward, so `v0.6.1` is probably cleaner).

This isn't process theatre — it's the "did this actually work" check that the 2h37m timeline skipped.

## Concern 5: The two clones have diverged

`cd /home/clinton/dev/Corbomite/libs/markoff-family; git remote -v` points at `/home/clinton/dev/Markoff/` (the canonical repo). There are **44 commits** and **three tags (`v0.4.0`, `v0.5.0`, `v0.6.0-alpha.1`, `v0.6.0`)** on the Corbomite-side clone that have not been pushed to the canonical upstream. Canonical `~/dev/Markoff/` is at `v0.3.0` on `master`.

This is not inherently bad — the Corbomite agent is working in-place on the submodule checkout and presumably intends to push. But until the push happens:

- Anyone auditing the state of the Markoff library at the canonical repo sees Phase C at 1/7. Anyone auditing at the Corbomite submodule sees Phase C at 4/7. The library's "current state" depends on which directory the observer is in.
- The standalone-Markoff invariant (#1 in the handoff doc) is unverifiable at the canonical repo because the code isn't there to verify.
- If any external reviewer (the Markoff-side agent, a future contributor, me in another session) pulls from canonical and tries to reproduce C3, they build `v0.3.0` and see none of this.

**Ask:** push from the Corbomite-side clone to canonical before starting C7. If there is a reason the pushes have been held back (e.g., waiting on review; wanting to squash; intending to rebase), name it in the status board. A 44-commit divergence that's load-bearing for the project's claimed state should be visible.

## Three asks, consolidated

In decreasing order of urgency:

1. **Push the 44 commits to canonical.** The state of the library should not depend on which checkout you're in. (Blocking C7 in spirit; zero effort.)
2. **Re-tag as `v0.6.0-alpha.2`, dogfood for a week with the `m_sceneNeedsFullRebuildOnNextParse` counter instrumented.** Use the week for Corbomite-side polish, CorbomiteApp dogfooding, or C7 spec-drafting — work that doesn't modify the Markoff library. Decide at the end of the week whether the full-rebuild fallback path needs work or whether the current approach holds in practice. (Blocking a clean `v0.6.x` milestone; one week.)
3. **Un-quarantine the two test files and run the anchor-semantics edge-case pass before C7.** (Blocking C7 start; ~1-2 days of focused work.)

The design direction is right; the build is not yet earned. Taking the week is how the build gets earned. Skipping it means the next surprise lands during C7 when the causal link to "we shipped C3 in 2.5 hours" has faded and the bug looks like it came from somewhere else.

Happy to help draft any of the instrumentation, the anchor-edge-case test pass, or the C7 spec during the soak week. Not urgent that I do so — the asks above stand on their own.
