# Projection Layer — Stage 4 redesign session brief

**Date authored:** 2026-05-01
**Branch:** `exploration/new-foundation` (worktree `.worktrees/foundation-exploration/`)
**Branch tip:** `cfbc30f` (revert of Stage 4 v0)
**Spec:** [`docs/specs/2026-05-01-live-projection-layer.md`](../specs/2026-05-01-live-projection-layer.md)
**Plan:** [`docs/plans/2026-05-01-live-projection-layer.md`](../plans/2026-05-01-live-projection-layer.md)

## TL;DR for the next session

You're picking up after a partially-shipped feature. The projection layer infrastructure (Stages 1-3 of the original plan) is in place and stable. The first hole consumer (Stage 4 — empty-paragraph hole) was implemented, dogfooded, and reverted because the design was wrong on multiple axes. Spec §3 has been rewritten around an IME-preedit pattern for v1; plan's Stage 4 (v1) section has the new task list. Your job is to implement Stage 4 (v1), then redo Stage 5 (v1) dogfood + docs.

**The user-reported bug** ("Enter at end of paragraph in the live demo does nothing visible") is *unfixed* at branch tip. Fixing it correctly is the entire purpose of Stage 4 (v1).

## Where things stand

```
master (some old tip; we don't merge to master)
  │
  └─ exploration/new-foundation
       │
       ├─ ... earlier foundation work ...
       │
       ├─ 19094bb  feat(view-qml): LiveProjectionLayer skeleton (T1-T6)         [Stage 1, shipped]
       ├─ ef62f57  refactor(view-qml): LiveSpeculativeFenceController via … (T7-T11)  [Stage 2, shipped]
       ├─ 3e1c437  refactor(view-qml): InlineFormatHighlighter via …          (T12-T16) [Stage 3, shipped]
       ├─ b07b07c  refactor(view-qml): rename InlinePrediction byte→char       [prep, shipped]
       ├─ a463bca  feat(view-qml): empty-paragraph hole (Stage 4 / T17-T25)    ★ broken
       ├─ ef0433b  fix(view-qml): focus routing + stacked-Enter dedup          ★ broken
       ├─ 030d581  refactor(view-qml): Stage 5 cleanup                          ★ broken
       ├─ 9fc0b83  docs: Live Projection Layer spec + plan
       ├─ 48c1f3e  docs(view-qml): document projection layer in CLAUDE.md
       └─ cfbc30f  revert: Stage 4 empty-paragraph hole (broken design)         ← branch tip
```

The three commits marked ★ implemented Stage 4 v0 and were reverted in `cfbc30f`. Their content is preserved in git history if you want to learn from the failure mode beyond the §3.6 summary.

## What works at branch tip

- `LiveProjectionLayer` infrastructure: prediction storage, parse-return reconciliation, layer ownership by `LiveListModelBinding`.
- `LiveSpeculativeFenceController` registers `BlockKindPrediction`s through the layer; reconciles via the layer's `onParseUpdated`. Tests green.
- `InlineFormatHighlighter` registers `InlinePrediction`s through the layer; renders them in `highlightBlock`. Tests green.
- `BlockHole` / `InlineHole` value types exist in `ProjectionItem.h` (storage skeleton); no production code creates holes; `rowIsHole` is a Stage 1 placeholder that returns `!m_blockHoles.isEmpty()` (correct only because nothing creates holes today).
- `tst_view_qml_live_paragraph_hole.cpp` and `tst_view_qml_live_paragraph_hole_integration.cpp` are gone (deleted by the revert). They were the v0 test files; v1 will need new ones.

107/107 tests pass at the tip with `ctest -j 8 -E 'tst_realistic|tst_benchmark|tst_view_qml_live_view_qml'`. The pre-existing `tst_view_qml_live_view_qml` baseline (10 passed / 2 failed / 2 skipped) is unchanged and unrelated.

## The five v0 failure modes (so you don't repeat them)

Spec §3.6 has the full forensics. In one paragraph each:

1. **Visual double-spacing** — v0 inserted `\n\n` at hole-creation. That `\n\n` lived in the previous block's byte range (per the live-editing-design "delegate owns trailing whitespace" invariant) and rendered as a blank line below the previous paragraph. The hole row added another. User saw two paragraph breaks for one Enter press.
2. **Character scramble during fast typing** — reify-on-first-keystroke destroyed the hole's delegate, then `applyLocalEdit` ran async (parse on a worker thread, ~30-100ms), then a new delegate materialised. During that window, focus was in transit. Subsequent keystrokes landed on the wrong delegate, producing scrambled source. The 10-attempt `Qt.callLater` retry loop only routed *focus*, not in-transit *keystrokes*.
3. **Arrow keys destroyed the hole** — abandonment-on-focus-out fired on any navigation. The delegate's TextEdit's default arrow-key behavior (Up/Down at line edges) yielded focus, which triggered abandonment.
4. **Focus went nowhere after abandonment** — no focus-routing logic for the abandonment path. User left with no caret.
5. **Source-state leak** — the `\n\n` from step 1 stayed in the file across abandonment. Files accumulated trailing newlines invisibly.

**Why the tests didn't catch it:** the unit test (`tst_view_qml_live_paragraph_hole`) drove `LiveProjectionLayer` and `LiveStructuralKeyHandler` directly, bypassing QQuickView entirely. The integration test (`tst_view_qml_live_paragraph_hole_integration`) used `QQuickView` + `QTest::keyClick`, but `QTest::keyClick` delivers events synchronously between event-loop spins — masking the very async race that broke real keyboard input. **Both tests passed while the demo app was visibly broken.**

## The v1 design in 60 seconds

The hole's delegate is a real local-typing surface (preedit buffer). Source is not written to until commit. Commit triggers: 250ms idle after last keystroke, focus-out with non-empty buffer, Ctrl+S (which flushes pending holes first), or explicit Enter when buffer has content. Abandon triggers: Esc, focus-out with empty buffer, Backspace at qtPos 0 with empty buffer. Arrow keys are normal navigation — focus-out on a non-empty buffer commits; on an empty buffer drops cleanly.

The five v0 failure modes are gone:
1. No `\n\n` in source at create-time → no visual double-spacing.
2. The delegate stays alive throughout typing; destroy-and-recreate happens once, at commit, with the delegate's text already preserved → no scramble during fast typing.
3. Arrow keys are normal navigation, not destruction.
4. Abandonment routes focus to nearest live neighbor (block before).
5. Nothing in source until commit → no leak. Empty hole + abandon = no source change.

See spec §3.2-§3.5 for the full design and §3.6 for why it's right.

## What to read, in order

1. This brief.
2. `docs/specs/2026-05-01-live-projection-layer.md` — the full spec, especially §3.2-§3.6 (the v1 design and v0 lessons), §5 (invariants), §6 (save/undo v1), §9 (open questions for dogfood).
3. `docs/plans/2026-05-01-live-projection-layer.md` — Stage 4 (v1) task list (T17-T25 numbering kept) and Stage 5 (v1) dogfood/docs.
4. `git show a463bca ef0433b 030d581` — what the v0 attempt looked like. Useful to learn from but **do not** cherry-pick anything from it; the design was wrong.
5. `libs/markoff-view-qml/CLAUDE.md` — library conventions, projection-layer section now reflects the partial-shipped state.
6. `docs/specs/2026-04-30-live-editing-design.md` §4 — the ten editing invariants the projection-layer's invariants 11-16 build on.

## How to start

The plan's Stage 4 (v1) has 9 tasks (T17-T25). T17-T19 are the layer-side and model-side changes (value type, hooks, interleaving). T20-T22 are the QML/delegate side (key handling, idle commit, focus-out). T23 is the save flush. T24 is the test suite — **the stress-typing test is load-bearing**; if it doesn't fail before T20-T22 are wired, your test is too lenient.

Suggested execution: subagent-driven-development as before (1 implementer per stage with two-stage review). Stage 4 (v1) is one logical stage; Stage 5 (v1) is dogfood + docs.

**Do not start until** you can answer this question: *"Why will my stress-typing test see the v0 race that the v0 stress test missed?"* The answer should mention `qWait` and `processEvents()` between keystrokes, mimicking real-keyboard timing. If your test injects keystrokes synchronously between event-loop spins, it will pass while the user-facing experience is still broken.

## Out-of-scope explicit list (still deferred after v1 ships)

- Hole inventory beyond empty-paragraph (list items, checklists, code-fence interior, blockquote, callout, table cells, links, wikilinks, footnotes, math). Each is its own follow-on plan.
- Multi-cursor holes.
- Configurable abandonment timeouts (250ms idle is hard-coded for v1).
- Hole serialization across process exit.
- Heuristic create-on-click (clicking past EOB to create-on-click).
- Backspace at the start of a real block following a hole row (the cross-hole-merge case from spec §9). Validate during dogfood.

## Ownership note

This brief is jointly owned with the user. They will steer; you will execute. The user has not signed off on commit-to-branch authority for code touching this area until v1 has passed manual dogfood (the same protocol that surfaced the v0 failures). Plan accordingly.
