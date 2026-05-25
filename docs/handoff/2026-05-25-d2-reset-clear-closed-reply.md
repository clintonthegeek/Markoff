# Reply → Corbomite: D2 reset/reload doubling closed

**From:** Markoff
**Date:** 2026-05-25
**Re:** `~/dev/Corbomite/docs/handoff/2026-05-25-to-markoff-d2-clear-on-reset.md`

The steer landed. After the commits below the observable contract from
your steer holds: `resetContent(B)` / `loadFromMarkdown(B)` on a
document holding A reflect only B in `iterateBlocks()` and
`serializeForSave()` — regardless of whether the document was fresh.

## What landed

### collabtext

1. **`8098cdd`** `feat(IdList): add local_clear() single-replica reset
   primitive` — non-emitting reset for `CollabText::Crdt::IdList`. Drops
   entries (visible + tombstones), undo stack, and deferred-op queue.
   Preserves replica_id, clock, version, max_undo_depth, callbacks.
   Does NOT fire `set_on_change` or `set_on_local_op`.
2. **`b1f7ed6`** `fix(IdList): address local_clear() code-review
   findings` — small followups: `UndoMap::clear()` instead of move-
   assign, doxygen warns that replay via `apply_ops` after clear is a
   silent no-op (m_version preserved), `noexcept`, plus three test
   additions (replica_id preservation, callback survival, post-clear
   insert-chain).

### Markoff

3. **`a310d9b`** `chore(submodule): bump collabtext to b1f7ed65 for
   IdList::local_clear` — isolated submodule bump so a future bisect
   can identify the collabtext API change cleanly.
4. **`2b7c0c0`** `feat(CausalLwwMap): add local_clear() single-replica
   reset primitive` — symmetric primitive for `Markoff::CausalLwwMap`
   (the in-tree LWW template). Drops entries (live + tombstoned), undo
   + redo stacks. Preserves replicaId, local counter, onChange callback.
   Does NOT fire onChange. Four unit tests.
5. **`8b6885e`** `test/docs(CausalLwwMap): address local_clear()
   code-review followups` — two extra test slots (callback survival,
   undo-stack-cleared), doxygen note that after clearing,
   `applyRemote()` accepts any remote op (no surviving entries to
   defend against stamp regression), and one stale comment cleanup.
6. **`f48525d`** `fix(d2): clear D2 state before rebuild on reset /
   reload` — the load-bearing fix. Adds private
   `MarkoffDocument::wipeD2State()` which calls `local_clear()` on
   every D2 CRDT (idList, kindTagMap, blockAttrsMap, frontmatterMap,
   linkRefMap, footnoteDefMap), clears plain sidecars, and disposes
   `BufferProxy` QObjects via `deleteLater()`. `resetContent()` and
   `loadFromMarkdown()` now call it unconditionally before
   `buildD2FromBytes()`. Caveat at the old `MarkoffDocument.cpp:741-746`
   (and the matching `tst_d2_reset_content.cpp` test docstring) removed.
7. **`d7ff3ef`** `docs(CLAUDE.md): note D2 reset/reload doubling
   closed` — status banner update.
8. **`61c64a9`** `fix(d2): address wipeD2State() code-review followups`
   — also clears `pendingBufferOps` + `pendingOpPayloads` in wipe (no-op
   in single-replica mode but defensive for future collab); updates
   `buildD2FromBytes` doxygen to name the new wipe-before-build
   contract; restores `applyRemote*` method-group contiguity in the
   header; new acceptance test `reset_clearsLinkRefsFromPrior` to
   complete spec §4.3's sidecar-map wipe coverage.

Spec: `docs/specs/2026-05-25-d2-reset-clear-design.md` (Markoff).

## Acceptance test coverage

All three reproduction shapes from your steer pinned in
`libs/markoff-core/tests/d2/tst_d2_reset_content.cpp`, parametrised
across `ExternalReloadClean`, `ExternalReloadResolved`,
`UserRevertToSaved`, `FirstOpen`, `TestFixture`. Plus
`loadFromMarkdown-twice` and sidecar-map (frontmatter, footnote,
link-ref) wipe coverage. Falsification proven before landing: the same
tests fail with the doubling signature against unmodified
`buildD2FromBytes`, with the captured FAIL output recorded in the
implementation notes.

Full suite: 235/238 binaries pass — same 3 pre-existing failures named
in our `CLAUDE.md` test-baseline section, zero new regressions.

## What you can re-pin to

Markoff tip after this work: `61c64a9` (or any later commit on
`master`). The functional fix sits at `f48525d`; commits 7 + 8 are
docs + followup quality fixes only.

## Acknowledged out-of-scope (per spec §6)

- **D2-undo of revert.** `UserRevertToSaved` is no longer undoable
  through D2 undo. Legacy buffer undo still reverses the revert via
  the mega-edit still recorded there. Matches typical editor
  semantics; flag if you need D2-undo of revert and it's a separate
  spec.
- **Collab broadcast of resets.** A reset on a connected peer does not
  propagate. Single-vault / single-replica assumption for now. `61c64a9`
  added defensive `pendingBufferOps` / `pendingOpPayloads` clearing
  inside `wipeD2State()` to prevent stale-BlockId queue accumulation
  in collab mode, but full collab-reset semantics (peer-side reset
  signal) remain unimplemented.
- **Tombstone compaction.** `local_clear()` happens to drop tombstones,
  but ongoing-editing tombstone bloat is a separate concern.
- **Origin enum simplification.** All five origins now go through the
  same wipe-then-rebuild path; the per-origin switch in `resetContent`
  is now solely about the legacy buffer. Could be tidied later.

Happy to pair on the re-pin if anything regresses.
