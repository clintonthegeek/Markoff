# Tier 2 Completion Handoff — 2026-05-15

## Status

**Tier 2 cursor typing-authority plan: COMPLETE ✅**

All 14 tasks executed successfully on `exploration/new-foundation` branch. Spec implementation (`docs/specs/2026-05-15-tier-2-cursor-typing-authority-design.md`) and plan (`docs/plans/2026-05-15-tier-2-cursor-typing-authority.md`) both achieved.

### Commits Landed (12 commits)

1. `1a0b81a` – docstring honesty fix (queue #2 concern #1)
2. `543ed6a` – cachedByteOffset → cachedQtPos rename (23 refs, concern #2)
3. `4c8b4d7` – typeUnicode/typeUnicodeString harness methods
4. `7a0874c` – test scaffold (5 QSKIP'd slots, concern #6)
5. `c2f0e89` – ASCII typing slot
6. `f3fc136` – CJK typing slot
7. `a22529e` – emoji typing slot (pragmatic paste method documented)
8. `5c5237b` – in-block arrow nav slot
9. `32b6c06` – kind transition slot
10. `5593cdc` – falsifiability stub (proof: 5/5 fail)
11. `590cb22` – falsifiability revert (5/5 pass restored)
12. `8b8310d` – discipline-log entry + queue update

### Key Results

- **5 test slots all passing**: ASCII, CJK, emoji, in-block arrow, kind transition
- **Falsifiability proven**: Stub + revert in history (invariant 4 requirement)
- **0 regressions**: Baseline failure list (9 tests) stable before/after
- **0 scope creep**: Files touched match spec §2.1 table (12 files)
- **Architecture clean**: No new `Qt.callLater`, no re-entrance guards
- **Binary ready**: `markoff-live-app` rebuilt for dogfood

## Dogfood Findings (2026-05-15)

Fresh context dogfood session revealed issues that emerged or were missed during execution:

### High-Severity (Regressions)

**Jump-scroll to top on `#` or `-` input**
- Occurs at any position in document, not just newly-pasted blocks
- Behavior: typing `#` or `-` triggers kind transition → view jumps to top (like Ctrl+Home)
- Impact: Perceptible behavior change; breaks navigation feel
- Likely causes:
  - Focus chokepoint selection/re-anchor logic during kind transition
  - Scroll-position reconciliation issue in QGraphicsView
  - Per-keystroke invariant not updated correctly during transition

### Medium-Severity (Visual Issues)

**Cross-block copy-paste loses header styling**
- Symptom: Pasted headers show hash marks hidden (prefix rule works) but text is not sized/styled as heading
- Expected: Pasted `HeadingDelegate` should render at larger font
- Likely causes:
  - Kind-transition heuristic not firing on pasted blocks
  - Delegate re-anchor or re-initialization gap after paste
  - Structural model (kind attribute) not propagated correctly to view layer

### Architectural Gaps (Not Yet Tested)

**Empty documents untested**
- No testing of create-all-content-in-session (fresh document)
- CRDT initialization path (load-time) ≠ incremental-edit path (per-keystroke)
- May mask timing/pipeline issues only visible on empty-document workflows

## Recommendations for Tier 3

1. **Investigate jump-scroll regression first** (blocking, high-visibility)
   - Check: focus chokepoint selection during kind transition
   - Check: LiveListModelBinding::onD2Changed scroll reconciliation
   - Check: per-keystroke invariant updates during structural events
   - May require instrumentation of the S1/S2/S3 fix from tier-1

2. **Fix header-styling loss on paste**
   - Check: Is kind transition heuristic firing for pasted blocks?
   - Check: Does delegate re-anchor correctly apply `HeadingDelegate` after paste?
   - Check: Are block attributes (kind) synced from CRDT to ListModel correctly?

3. **Test empty-document workflows**
   - Add test fixture: empty document, type all content in session
   - Cover: paragraph → heading transition, multi-block pasting, undo/redo
   - Uncover: CRDT init vs. incremental differences

4. **Decision: Pre-tag or known issues?**
   - If fixing now: target v0.7.0-e2.5 tag after fixes + re-dogfood
   - If deferring: file discipline-log entries in queue.md, plan tier-3 investigation

## Files to Review

- `docs/queue.md` – Tier 2 banner and discipline-log entries (concern #9 deferral documented)
- `.worktrees/foundation-exploration/CLAUDE.md` – Updated with tier-2 completion note
- `build-dev/bin/markoff-live-app` – Ready for dogfood (binary at 14.7 MB, 2026-05-15 14:35)

## Next Agent Context

Branch: `exploration/new-foundation` (post-tier-2 commits)
Working tree: `.worktrees/foundation-exploration/`
Build status: Clean (9 pre-existing test failures, 0 new failures)
Ready for: Dogfood re-run after fixes, or formal tier-3 planning
