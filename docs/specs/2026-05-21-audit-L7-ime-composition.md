# Audit L7 — IME composition under D2

**Date:** 2026-05-21
**Status:** Spec + test coverage of current behavior; design changes deferred.
**Branch:** `exploration/new-foundation`
**Source:** `docs/specs/2026-05-21-textedit-interface-audit.md` §3.5(c), §5 (L7).

## What the audit says

> Symptom: untested entirely. Dead keys (Compose) and CJK IMEs may or
> may not produce correct d2 buffer state. The wholesale
> `flushPendingComposition` swap is structurally lossy in coordinate
> granularity but correct in final content; that may or may not
> interact correctly with concurrent remote edits in D5.

## Current behaviour (as of this branch)

The IME path runs entirely through `LiveEditBinding`:

1. Each delegate's `TextEdit` exposes the QML
   `inputMethodComposing` property, which is `true` while a preedit
   string is present.
2. `UnifiedInlineTextDelegate.qml:76` (and the CodeBlock /
   MathDelegate counterparts) binds `LiveEditBinding.composing :
   edit.inputMethodComposing`. So composing's lifecycle mirrors
   `inputMethodComposing`.
3. While `m_composing == true`, every `onContentsChange(qtPos,
   removed, added)` early-returns after setting
   `m_compositionPendingFlush = true`. The CRDT sees no edits.
4. When composing transitions back to false, `flushPendingComposition`
   runs: it issues a single `d2ApplyBufferEdit(blockAnchor, 0,
   prevByteLen, postUtf8)` — **a wholesale replacement of the entire
   block's bytes** with the post-commit text. No incremental edits
   are recorded.

This is correct in final content (the block ends up matching what the
user sees) but has two known shape problems:

### Shape problem 1 — coordinate granularity loss

A user typing "猫が好き" via a CJK IME generates per-character
preedit updates. The CRDT sees only one operation at commit time:
`replace([0..preLen], "猫が好き")`. The undo log records this as a
single step. Undo collapses the entire composition in one Ctrl+Z;
arguably the right UX (CJK input methods usually present commit as
the atomic unit), but worth noting.

### Shape problem 2 — D5 concurrent-edit collision risk

D5 introduces remote concurrent edits. If replica B inserts a
character at the same block, byte 0, while replica A is composing,
A's commit-time `d2ApplyBufferEdit(0, prevByteLen, postUtf8)` will
overwrite B's insertion. The wholesale-replace is computed against
A's local pre-composition snapshot (`m_previousText`), so B's edit
is invisible to A's commit. CollabText's per-character ops would
have merged correctly.

This is a known issue at D5 enablement. Not a v1 blocker — D5 is
post-E-arc; by then the IME path is a legitimate redesign target.

## Decision

**No production code change in this session.** The current shape is
the best we have until D5 lands. What we add now is **test
coverage** for the current behaviour so the audit's "untested
entirely" concern resolves and any future change has a regression
net.

Tests injected via `QInputMethodEvent` (the same protocol Qt's input
method backends use). The fixture sends `preedit` and `commit`
events directly to the focused `TextEdit` (`fx.delegateTextEdit(row)`)
via `QCoreApplication::sendEvent`.

## Test slots (`tst_live_render_ime_composition_qml`)

1. **`commit_after_preedit_lands_in_d2_buffer`** —
   start with empty block. Send preedit "ab" (`inputMethodComposing`
   should become true; CRDT should remain empty). Send commit "ab"
   (preedit clears; CRDT should now hold "ab").

2. **`preedit_then_replace_then_commit_records_single_edit`** —
   preedit "a" → preedit "ab" → commit "ab". After each preedit step,
   the CRDT must remain empty. After commit, the CRDT holds "ab",
   recorded as a single `d2ApplyBufferEdit` (one undo step).

3. **`preedit_then_empty_no_commit_leaves_d2_unchanged`** —
   preedit "a" → preedit "" with no commit (cancelled composition).
   CRDT must remain empty. The `flushPendingComposition` path runs
   because composing transitions true→false, but the wholesale swap
   inserts the empty-preedit text, which matches the pre-composition
   block content — net effect zero (verified via undo-log size).

4. **`commit_into_non_empty_block_inserts_at_caret_after_swap`** —
   start with "hello", cursor at end. Preedit "WO" → commit "WORLD".
   Block should hold "helloWORLD". This exercises the
   wholesale-replace path on non-empty starting content.

5. **`composing_property_lifecycle_matches_qt_native`** —
   probe slot. Asserts that `inputMethodComposing` toggles true when
   a non-empty preedit is set, and false on commit or on empty
   preedit. Documents the lifecycle the L7 implementation relies on.

## Out of scope

- Per-character preedit forwarding to CRDT (the redesign that would
  solve shape problem 1 and 2). Tracked as future work alongside D5
  enablement; spec stub belongs in the D5-collision work-unit, not
  here.
- IME interaction with cross-block selection (the L1 collapse path).
  IME on a TextEdit with a within-block selection is the standard
  case Qt handles; composing semantics during a cross-block selection
  are undefined and probably should disable composing (TextEdit lacks
  focus during cross-block-selection display anyway). Not blocking;
  flag in the D5 spec.
- IME inside math LaTeX edit. MathDelegate has its own composing
  binding (line 69); the same wholesale-replace logic applies inside
  `latexEdit`. Adequate for v1; mathematicians rarely IME-compose
  LaTeX.

## Resolution

`docs/queue.md`: strike L7 with resolution pointer to this spec +
test. Audit's "deferred — needs design pass" closes. The D5
collision risk is documented here and will be re-opened explicitly
in the D5 work.
