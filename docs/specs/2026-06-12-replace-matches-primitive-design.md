# replaceMatches primitive + selectMatchAtOrAfter — design

**Date:** 2026-06-12
**Driving consumer:** Corbomite Find/Replace. Full design:
Corbomite `docs/superpowers/specs/2026-06-12-replace-find-ui-design.md`.

## Additions (markoff-core)

1. `void MarkoffDocument::replaceMatches(const QList<SearchHit> &matches,
   const QString &replacement)` — replace each match's byte span with literal
   `replacement` as ONE undo transaction. Block-local match offsets are mapped
   to global no-separator flat offsets via `iterateBlocks()` + `blockText()`
   accumulation (NOT `blockByteRange`, which is parse-source space). Edits are
   applied descending-by-start so earlier-applied edits never shift later ones;
   folded into one UndoLog entry via `coalesceLastUndo()`; ends with
   `flushPendingD2Changed()` so the post-state (and any active FindController
   recompute) is synchronous.

2. `void FindController::selectMatchAtOrAfter(Markoff::BlockAnchor block,
   quint32 offset)` — mutation-free; moves only `currentMatchIndex` to the first
   match at/after (block, offset), wrapping to 0. Emits `currentMatchChanged`.
   Preserves invariant D3 (no document/focus/cursor/scroll contact).

## Invariants

Touches the edit path, not the focus/caret/block-change seam — seam rules
(INVARIANTS §scope) do not bind. INVARIANT 4 (falsifiable, production-callsite
tests) applies: see `tst_replace_matches` (the offset-shift case is falsifiable
by reversing the apply order).
