# R5 empty-paragraph gap — why holes need to come back

**Date authored:** 2026-05-03
**Branch:** `exploration/new-foundation`
**Worktree:** `.worktrees/foundation-exploration/`
**Status:** R5 implementation Tasks 1–11 landed (commits `7c6f7f6..b8fb639`); Tasks 12–18 paused. The spec amendment proposed in this document blocks Task 18 (R5 dogfood gate). It does NOT block Tasks 12–17 (those are integration plumbing that holds for both the current design and a holes-augmented design), but the user has chosen to pause everything pending a fresh-context post-mortem and redesign.

**Audience:** the next session, with a fresh context window, that will:
1. Conduct a post-mortem of the v0 holes implementation that was reverted in the legacy `markoff-view-qml` library.
2. Decide whether the v1 IME-preedit-pattern hole design (already specced but never implemented) is the right answer.
3. Propose concrete amendments to the restoration spec to re-introduce holes — specifically, the `BlockHole` primitive — and define their integration with the C-architecture's existing freshness rule and `LiveCursorState::requestTextCaretAtRow` pending mechanism.
4. Re-derive the R5 (and possibly R6) plan to incorporate holes.

The user has authorised the spec amendment; you do not need to ask permission to propose one. You DO need to surface the proposal to the user and get explicit approval before editing the spec, per the spec-amendment protocol in `docs/handoff/2026-05-02-restoration-session-brief.md` §3.6.

---

## TL;DR

The R5 plan assumed that pressing Enter at the end of a paragraph, which inserts `\n\n` into the source via `MarkoffDocument::applyLocalEdit`, would cause the parser to produce a new empty paragraph block, which would materialise as a new row in `LiveBlockModel`, into which `LiveCursorState::requestTextCaretAtRow(row+1, 0)` would deliver the caret on `rowsInserted`.

This is wrong. Tree-sitter's CommonMark grammar does not emit empty-paragraph nodes for blank lines. Trailing `\n\n`, leading `\n\n`, and arbitrarily many blank lines between blocks all produce zero empty-paragraph blocks. Confirmed by the post-correction R5 test `enter_at_end_of_paragraph_inserts_paragraph_break`: after `resetContent("hello\n\n")` the model has exactly 1 row, not 2.

Therefore the R5 dogfood criterion (spec `2026-05-02-live-render-restoration-design.md` §10.3) — *"Press Enter at the end of every paragraph in a 10-block doc; caret lands in the new empty paragraph each time"* — cannot be satisfied by structural-key-driven `applyLocalEdit("\n\n")` alone. The user-facing behaviour of "press Enter, type into a fresh new paragraph" is structurally absent.

The C-architecture restoration spec retired holes under premise 6 ("Notion-style Enter; holes deleted"). That decision conflated two distinct legacy issues:
1. The legacy `LiveProjectionLayer` v0 holes implementation hit five concrete failure modes under dogfood (documented in `docs/specs/2026-05-01-live-projection-layer.md` §3.6) and was reverted.
2. The legacy "six sources of truth" cycle-guard architecture made adding ANY new view-side state difficult.

The restoration spec correctly retired (2). It also retired (1), and in doing so threw out the only mechanism that could deliver the dogfood UX the same spec demands. The v1 IME-preedit-pattern hole design (specced at `docs/specs/2026-05-01-live-projection-layer.md` §3.1–§3.5 but never implemented) was specifically designed to avoid the v0 failure modes and is structurally compatible with the C-architecture's freshness rule. **Holes need to come back, in their v1 form, scoped narrowly.**

---

## 1. What we assumed

The R5 implementation plan (`docs/plans/2026-05-02-live-render-r5-structural-keys.md`) and the architecture spec it derived from (`docs/specs/2026-05-02-live-render-restoration-design.md` §7.2 and §11 R5) both rest on this load-bearing assumption:

> **A structural Enter at qtPos == blockText.length() inserts `\n\n` at `currentBlockEnd`, the parse-back produces a new empty paragraph row at `blockIndex + 1`, and `LiveCursorState::requestTextCaretAtRow(blockIndex + 1, 0)` resolves on the resulting `rowsInserted` to land the caret in that new empty row.**

The data flow in spec §7.2 reflects this assumption literally:

```
parseUpdated arrives (round-trip):
  AstBlockDiff produces: Equal(B_old, truncated), Insert(B_new)
  rowsInserted fires on B_new
  LiveCursorState's pending request resolves; routes focus into B_new
```

The plan's `paragraphEnter` handler in Tasks 4/5/8 was implemented faithfully against this assumption. The implementation is correct *given the assumption*.

## 2. What actually happens

Tree-sitter's CommonMark grammar (`tree-sitter-markdown`) follows the CommonMark specification, which defines a paragraph as one or more non-blank lines of inline content. **A blank line is not a paragraph; it is a separator.** Multiple consecutive blank lines are equivalent to one. The parser emits zero block nodes for blank-only regions.

We confirmed this via the post-correction R5 test in `libs/markoff-live-render/tests/tst_live_render_structural.cpp`:

```cpp
void enter_at_end_of_paragraph_inserts_paragraph_break() {
    Markoff::MarkoffDocument doc(/*replicaId=*/1);
    LiveListModelBinding binding;
    binding.setDocument(&doc);
    doc.resetContent("hello", Markoff::Origin::FirstOpen);
    QVERIFY(parseSpy.wait(2000));

    // ... structural-key handler fires, applyLocalEdit inserts "\n\n" at byte 5 ...

    QCOMPARE(doc.toMarkdown(), QString("hello\n\n"));

    // After parse-back: trailing \n\n is whitespace; tree-sitter does not
    // create a second (empty) paragraph from trailing blank lines.
    QVERIFY(parseSpy.wait(2000));
    QCOMPARE(binding.model()->rowCount(), 1);   // <-- not 2
}
```

The test passes. The model has 1 row. The `requestTextCaretAtRow(1, 0)` fires, the `parseUpdated` handler doesn't see row 1 appear, the pending request lingers for two parse cycles per spec §8.4, then drops. The caret stays where it was — at `qtPos == 5` of row 0 ("hello").

### 2.1 Three concrete cases the dogfood criterion cares about

**Case A: End of last paragraph.** Source `"hello"`, cursor at qtPos 5, press Enter. CRDT becomes `"hello\n\n"`. Parser → 1 row (`"hello"`). Caret request to row 1 expires unresolved. **User outcome: caret stays at end of "hello"; nothing visible changed; subsequent typing extends "hello".** Broken UX.

**Case B: End of non-last paragraph.** Source `"para1\n\npara2"`, cursor at end of para1 (qtPos 5), press Enter. CRDT becomes `"para1\n\n\n\npara2"`. Parser still treats this as two paragraphs (`"para1"` and `"para2"`); zero empty paragraphs between them. Caret request to row 1 — but row 1 already exists (it's `"para2"`, just shifted byte position). Pending request resolves immediately on the FIRST `parseUpdated`, placing the caret at qtPos 0 of `"para2"`. **User outcome: caret jumps to start of next paragraph; no new empty paragraph appears; next keystroke prepends to para2.** Surprising UX.

**Case C: Start of any paragraph.** Source `"hello"`, cursor at qtPos 0, press Enter. CRDT becomes `"\n\nhello"`. Parser → 1 row (`"hello"`). The R5 plan's `enter_at_start_of_paragraph_inserts_blank_above` test asserts only `rowCount() >= 1` because the plan author already suspected this would not produce two rows. **User outcome: nothing visible.** Same broken UX as Case A.

### 2.2 What does work

Mid-block Enter — when both halves contain non-blank content — splits cleanly:

- Source `"hello world"`, cursor at qtPos 5, press Enter. CRDT becomes `"hello\n\n world"`. Parser → 2 rows (`"hello\n"` for row 0, `" world"` for row 1). `requestTextCaretAtRow(1, 0)` resolves to the new row 1. **Works.**

The success of mid-block split is what tricks an architecture into thinking the design is whole. Mid-block split works because the suffix has user content; the parser produces a row because the row is non-empty.

### 2.3 Why this matters for R5 specifically

The R5 dogfood criterion (spec §10.3) is precisely the failing case:

> *"Press Enter at the **end** of every paragraph in a 10-block doc; caret lands in the new empty paragraph each time; Backspace at the start of each merges back, restoring the original."*

End-of-paragraph Enter is what users do constantly. Mid-block Enter is rare. The criterion is not a corner case; it's the central UX gesture of structural editing. R5 cannot ship with this broken — the user-visible behaviour is "Enter does nothing or jumps unexpectedly."

## 3. Why naive workarounds don't recover the UX

Three obvious workarounds were considered and each is unsound:

### 3.1 Placeholder character

Inject a zero-width character (e.g. U+200B ZERO WIDTH SPACE) into the source on Enter so the parser sees content. The parser produces a row containing only the placeholder; `requestTextCaretAtRow(row+1, 0)` resolves; the caret lands inside the placeholder.

**Why it fails:**
- `serializeForCopy` round-trips the placeholder to the clipboard. Pasting into another editor reveals junk characters.
- Save round-trip writes the placeholder to disk. The next reopen sees a "wrong" file. Save → quit → reopen produces visibly damaged source. (This was the v0 holes' "source-state leak" failure mode in a different form — `2026-05-01-live-projection-layer.md` §3.6 mode 5.)
- The placeholder is opaque to undo, search, find/replace — every consumer of source bytes has to be taught to ignore it.
- Polluting the source for view-side rendering is exactly the cycle-guard antipattern the C architecture retired. We'd be re-inventing the wheel inside out.

### 3.2 Auto-pull cursor on next-paragraph parse

For Case B (non-last paragraph), redefine the structural Enter to mean "jump to start of next paragraph." For Case A (last paragraph), append a real paragraph by inserting placeholder content.

**Why it fails:**
- Inconsistent semantics: pressing Enter at end of paragraph does different things depending on whether you're at the last paragraph (creates content) or not (jumps).
- Users do not expect Enter to jump them into existing content. The mental model is "Enter starts a new line/paragraph here."
- The fallback for Case A reduces to §3.1 (placeholder).

### 3.3 Post-typing recovery

Hold the caret request pending; if the user types a character before the request expires (2 parse cycles), apply that character at the expected reify-offset and let the parser create the row. The caret follows.

**Why it fails:**
- The cursor is NOT in the new (yet to exist) row when the user types. The cursor is at end of the previous paragraph (`"hello"`, qtPos 5). The user's first keystroke goes to the foreground TextEdit, which is row 0's. Source becomes `"hellox\n\n"` after the user types `x`. Parser → 1 row (`"hellox"`). The user wanted `"hello\n\nx"` (two paragraphs); they got one paragraph extended.
- Even if we trapped the next keystroke specially and routed it through the structural-key handler instead of LiveEditBinding, the visual feedback during the typing window would be: user presses Enter (nothing visible), user types `x` (`x` appears at end of "hello"), parse arrives (`x` jumps to a new paragraph below). The flicker is unacceptable.

None of these recover correct UX. The fundamental problem is that **the user's intent — "I am about to type a new paragraph here" — has no source representation until the user has typed at least one byte of content.** Either the editor refuses Enter at end-of-paragraph (broken), teleports the caret somewhere unexpected (broken), or holds the user's intent in view-side state until the next byte resolves it. The third path is what holes do.

## 4. What "holes" means

A **hole** (specifically, a `BlockHole` per the legacy `2026-05-01-live-projection-layer.md` design) is a view-side row that:

- Exists in the model and renders a delegate.
- Has no source representation (the parser has not produced a corresponding block).
- Holds a `bufferText` (a transient local buffer the user types into).
- Has a `reifyOffset` (the source byte position where its content will be committed).
- Reifies into a real source-side block on commit (idle debounce, focus-out with content, save, explicit Enter on a non-empty buffer).
- Drops on abandon (focus-out with empty buffer, Esc, Backspace at qtPos 0 with empty buffer).

The user's experience: they press Enter at end of a paragraph, see a fresh new paragraph appear with their caret in it, type into it, and after a brief idle the parser sees the typed content and confirms a real block. From the user's POV the editor is just normal; from the architecture's POV the row was view-side state for a moment and source-side state from then on.

Holes are a strict superset of the existing R3-era cursor protocol. The C architecture's cursor mechanism (`LiveCursorState::request`) and its R5 extension (`requestTextCaretAtRow`) work on rows that exist in the model. Holes add a row to the model that the parser didn't produce.

## 5. Why the spec retired holes (and why that decision is incomplete)

The C-architecture restoration spec, premise 6:

> | 6 | Enter semantics | **N** — Notion-style: Enter creates a new block; Shift-Enter inserts a soft break (`\n`) within the current block. EOB-Enter hole feature is deleted. |

§4.4 ("Consequences (cycle-guards retired by C)") includes:

> | `commitBlockHole` rowsInserted listener leak | `LiveProjectionLayer.cpp:155-177` | Holes retired (premise 6: Notion-style Enter); listener does not exist in new library. |
> | Detach/reattach hole around `applyOps` | `LiveListModelBinding.cpp:175-194` | Holes retired. |

§5.4 mentions structural keys but assumes parse-driven row creation:

> The handler:
> 1. Looks up the focused block's descriptor.
> 2. If the key is in the descriptor's `consumedStructuralKeys`, dispatches to the kind-specific handler for that key.
> 3. ...

§7.2 documents the data flow assuming parse produces the new row:

> `MarkoffDocument::applyLocalEdit({insert "\n\n" at byte}) ← single edit` → `editSequence bumps; tag both halves' rows when they appear` → `schedule LiveCursorState::request(TextCaret(B_new, 0))` → `parseUpdated arrives` → `AstBlockDiff produces: Equal(B_old, truncated), Insert(B_new)` → `rowsInserted fires on B_new`.

The "Insert(B_new)" line is the load-bearing claim. It is wrong for end-of-paragraph and start-of-paragraph cases, where the parser produces no Insert.

**Why the spec retired holes is reasonable:** the v0 implementation in `markoff-view-qml` had five distinct failure modes (`2026-05-01-live-projection-layer.md` §3.6 — read this in full as part of your post-mortem). These were real, dogfood-surfaced, and damaging. The audit (`docs/2026-05-02-live-view-architectural-audit.md`) correctly identified that the v0 holes mechanism, the cycle-guards-everywhere architecture, and the six-sources-of-truth confusion were all symptomatic of the same underlying problem: too many independent authority claims for "what is the content of block N right now," reconciled ad-hoc.

**Why the retirement decision is incomplete:** the audit prescribed a single principled mechanism (sequence-tagged reconciliation) to retire the cycle guards and the six-sources-of-truth confusion. That prescription is correct. But the v0 holes' five failure modes were primarily caused by *the surrounding architecture*, not by the holes-as-a-concept. The v1 IME-preedit-pattern redesign at §3.1–§3.5 of the same projection-layer spec specifically addresses each of the five failure modes. The C architecture's freshness rule + `LiveCursorState::requestTextCaretAtRow` + deterministic-no-Qt.callLater-retry policy are the missing pieces v1 holes need to be safe. By the time the C architecture is in place — which it now is, post Tasks 1–11 of R5 — v1 holes become tractable in a way they weren't on the legacy stack.

## 6. The v1 hole design (already specced; needs ratification)

`docs/specs/2026-05-01-live-projection-layer.md` §3.1–§3.5 contains a complete v1 design that was never implemented. The receiving session's job is NOT to redesign holes from scratch — it is to:

1. Read §3.1–§3.5 and §3.6 of that spec carefully.
2. Verify that the v1 design's failure-mode mitigations are real, given the C architecture as it now stands.
3. Determine which parts of the v1 spec carry forward intact and which need adaptation to the new library structure (`libs/markoff-live-render/` not `libs/markoff-view-qml/`; `LiveSpeculationLayer` not `LiveProjectionLayer`).
4. Write a new spec for v2 holes — short, citing v1 wholesale where the design is unchanged, calling out adaptations precisely.
5. Propose the C-architecture spec amendments that follow.

A summary of the v1 design (the full text is at the source spec; do not paraphrase further than this in your work):

- **Trigger:** `LiveStructuralKeyHandler` detects Enter at `qtPos == blockText.length()` and creates a `BlockHole(kind="paragraph", reifyOffset=currentBlockEnd, bufferText="")`. **No source edit.** The CRDT is unmodified.
- **Local typing:** the hole's delegate is a regular `ParagraphDelegate` with `isHole === true`. Keystrokes update `bufferText` in the layer (binding mirrors QTextDocument contents into the hole's buffer). The TextEdit shows the buffer locally.
- **Commit:** idle debounce (250 ms), focus-out with content, save (Ctrl-S), or explicit Enter on a non-empty buffer. Emits one `MarkoffEdit` inserting `"\n\n" + bufferText` at `reifyOffset`. Drops the hole synchronously. Parse runs, real delegate materialises with the typed text.
- **Abandon:** Esc, focus-out with empty buffer, Backspace at qtPos 0 with empty buffer. Drops the hole. **No source mutation.** Routes focus to nearest live neighbor.
- **Architectural invariant:** source is never written until commit. The hole is invisible to the parser, dirty flag, and save until reified.

The v1 design's named mitigations of v0's five failure modes (per §3.6):

1. **v0 visual double-spacing** — caused by writing `\n\n` to source at hole-creation. **v1 mitigation:** no source edit at hole-creation; the visual paragraph break comes entirely from the hole row.
2. **v0 character scramble during fast typing** — caused by destroy-and-recreate cycle on first keystroke (focus-in-transit window). **v1 mitigation:** the user types into a stable delegate the whole time; destroy-and-recreate happens once at commit.
3. **v0 arrow keys destroyed the hole** — caused by drop-on-focus-out being too aggressive. **v1 mitigation:** arrow-key navigation is normal; if it would leave the delegate, commit-or-abandon based on `bufferText` non-empty/empty.
4. **v0 focus went nowhere after abandonment** — caused by drop-on-focus-out leaving no caret-routing. **v1 mitigation:** abandon explicitly routes focus to nearest live neighbor.
5. **v0 source-state leak** — caused by writing `\n\n` at hole-creation that survived silent abandonment. **v1 mitigation:** no source edit until commit, so abandonment leaves no trace.

## 7. What you (the receiving context) must do

### 7.1 Read first, in this order

1. This document, in full (you're already here).
2. `docs/specs/2026-05-02-live-render-restoration-design.md` premise 6, §4.4, §5.3, §7.2, §11 R5 and §11 R6, §15. The C architecture is the contract you'll amend.
3. `docs/specs/2026-05-01-live-projection-layer.md` §0 (status), §1 (background), §3.1–§3.5 (the v1 hole design — load-bearing), §3.6 (the five v0 failure modes — load-bearing), §4 (predictions refactor), §5 (architectural invariants), §6 (save/undo).
4. `docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md` — the brief that initiated the v1 redesign session that itself was deferred. This is your prior art on what a v1 implementation would have looked like.
5. `docs/2026-05-02-live-view-architectural-audit.md` — the audit that retired holes as part of retiring the broader cycle-guard / six-sources-of-truth architecture.
6. `docs/plans/2026-05-02-live-render-r5-structural-keys.md` — the R5 plan we just executed, particularly Task 4 (the structural-key handler) and Task 11 (descriptor wiring + test corrections).
7. The R5 test corrections in commit `b8fb639` — read the diff to see the empirical evidence of the parser's blank-line behaviour.

### 7.2 Conduct the post-mortem

Answer concretely:

- Did v0 fail because of the holes mechanism per se, or because of surrounding architectural problems that have since been retired?
- For each of the five v0 failure modes (§3.6), is the v1 mitigation real GIVEN the C architecture as it now stands?
- Are there v1 failure modes that the v1 spec did not anticipate, that the receiving context can identify with the benefit of the post-R4 dogfood lessons?
- Are the v1 commit triggers (idle 250 ms, focus-out, save, explicit Enter) the right set, or are some redundant / missing?
- Is the v1 abandon trigger set (Esc, focus-out empty, Backspace at qtPos 0 empty) right? Particularly: is "Backspace at qtPos 0 of empty hole" the right intent — are users likely to press Backspace to mean "cancel"?

### 7.3 Adapt the v1 design to the new library

The v1 spec was written for `libs/markoff-view-qml/`. The new library is `libs/markoff-live-render/`. Carry the v1 design forward with these specific adaptations to consider:

- **Where holes live.** Spec §6.1 L6 names `LiveSpeculationLayer` as the renamed `LiveProjectionLayer` for predictions only. R6 currently scopes that layer to inline-format predictions and fence-kind speculation. Holes are a third item-kind that needs to live in this layer (or a sibling). Decide: extend `LiveSpeculationLayer` to host holes too, or create a parallel `LiveHoleLayer`?
- **Integration with `LiveCursorState::requestTextCaretAtRow`.** When a hole is created, the cursor request resolves immediately into the hole's row (the row exists in the model from the moment the hole is created). When the hole is reified, the cursor stays in the row that just transitioned from hole to real-block — the existing pending mechanism may not apply directly. Determine the semantics.
- **Integration with the freshness rule (`row.lastEditEditSequence`).** A hole has no `editSequence` because its bufferText is not in the CRDT. Decide: do holes participate in the freshness rule at all? Or is the freshness rule a parser-row-only concept that holes sit alongside?
- **Integration with `LiveBlockModel`.** The model is a `QAbstractListModel`. Currently every row has a `BlockRecord` whose `blockAnchor` is a CRDT anchor at the parser-emitted block's first byte. A hole row has no parser-emitted block. Decide: does a hole row have a synthetic anchor, a null anchor, or a separate row-kind discriminator?
- **Integration with `LiveSelectionView`.** The selection view projects between Session::primarySelection and view-side block/offset pairs. What does selection across a hole mean? Decide: collapse selection on hole reification? Refuse selection that includes a hole?
- **Save semantics.** v1 spec §6 says save flushes pending bufferText first. Spec §6.1 L4 has `applyTextUpdate` per-delegate; how do holes integrate with that? Decide: does `Ctrl-S` traverse the hole layer first?
- **Undo.** v1 spec invariant 12: a hole's drop is not an undo entry; reification produces a normal CRDT edit which enters the stack normally. Verify this is consistent with `UndoCoalescer`'s policy from R5 Task 3.

### 7.4 Propose spec amendments

Per `docs/handoff/2026-05-02-restoration-session-brief.md` §3.6, spec amendments require:

1. Identify the contradiction concretely (you have it: §7.2 data flow assumes parse produces row; parser doesn't).
2. Propose the edit in the **Spec-amendment log** of `docs/restoration-status.md`.
3. Surface to the user: a clear message saying "the spec asserts X; implementation reveals Y; I propose amending §N to read Z; OK to proceed?" Include a draft of each amendment.
4. Wait for explicit user approval before editing the spec.

The amendments will likely touch:

- **Premise 6.** Either reword to acknowledge holes-for-end-of-paragraph as scoped exception, or remove the "EOB-Enter hole feature is deleted" clause and replace with the v1-scoped intent.
- **§4.4 cycle-guards-retired table.** Restore the `commitBlockHole` and detach/reattach rows, but with the v1-pattern note ("hole's source edit is deferred to commit, not at hole-creation; rowsInserted listener watches for hole→real reification, not for arbitrary parse-driven inserts").
- **§5.4 structural-keys.** Note that paragraph + Enter at end now produces a hole, not a `\n\n` insertion.
- **§6.1 L6.** Either widen `LiveSpeculationLayer` scope to include holes, or introduce L6.5 `LiveHoleLayer`. Update the L0-L8 architecture diagram.
- **§7.2 structural-edit data flow.** Replace the "applyLocalEdit at edit time → parse-back → rowsInserted" flow with the hole-create → local-typing → commit-on-trigger flow. The Mid-block split (`B_old → B_old.truncated + B_new` via parse-back) stays as-is — that case works without holes.
- **§7.3 etc.** May need adjustment.
- **§11 R5 scope.** Either declare R5 ships without holes (and the dogfood criterion is amended) and holes ship in a new R5.5 phase, OR declare R5 incomplete and add hole-implementation tasks to it before dogfood.
- **§15 open questions.** Add new ones if any surface during your design.

### 7.5 Re-derive the plan

After spec amendments are approved, re-run the writing-plans skill to produce either:

- **Option A:** an addendum to the R5 plan adding hole-implementation tasks (Tasks 12.5? 18 → 19?). Bundle dogfood with existing R5 dogfood.
- **Option B:** a new R5.5 (or R6 reordering) plan that ships holes as its own phase. R5 closes with a documented limitation; R5.5 closes the gap.

Decide based on the size of the hole work: if it fits in 4–6 tasks, Option A; if it's 10+, Option B.

### 7.6 What you do NOT need to do

- **Re-implement v0.** v0 is gone. The legacy `LiveProjectionLayer` files (`libs/markoff-view-qml/src/LiveProjectionLayer.{h,cpp}`) are reference-only.
- **Touch the master branch.** All work is on `exploration/new-foundation` in `.worktrees/foundation-exploration/`.
- **Delete or rewrite Tasks 1–11 of R5.** Their commits (`7c6f7f6..b8fb639`) are correct as far as they go. The hole work *adds* to them, doesn't replace them.
- **Run the dogfood gate.** That's blocked until holes are designed and shipped or until the criterion is amended with user approval.

## 8. State at the time of this writing

### 8.1 What's landed

R5 Tasks 1–11 (commits `7c6f7f6..b8fb639`):
- `LiveCursorState::requestTextCaretAtRow` + `noteParseArrived` (pending-row mechanism, spec §5.3 step 6).
- `UndoCoalescer` (printable-coalesce policy, spec §6.1 L5).
- `LiveStructuralKeyHandler` (descriptor-driven dispatcher, spec §5.4) with built-in handlers for paragraph (Enter all positions, Shift-Enter, Backspace at start, Delete at end), heading (same), code-block (Backspace + Delete only).
- `BlockKindRegistry::registerBuiltins` populates `consumedStructuralKeys`.
- All structural tests pass: 21/21 in `tst_live_render_structural`; 7/7 live-render fast-tier executables green.

What you have in your hands is a structural-key dispatcher that correctly handles every case where the parser produces the new row. The mid-block-split case works. The Backspace-merge cases work. The Delete-merge cases work. The Shift-Enter soft break works. The cases that don't work are the ones that need holes: end-of-paragraph Enter and start-of-paragraph Enter, where the parser produces zero or one rows and the structural-key handler has nowhere to park the cursor.

### 8.2 What's not landed

R5 Tasks 12–18 (paused):
- Task 12: `LiveEditBinding` integrates `UndoCoalescer` (independent of holes; should land regardless).
- Tasks 13–15: QML delegate wiring (paragraph, heading, code-block) — remove R4 Enter-swallow, route through structural-key handler. Should land for the cases that work; holes may add a separate routing path.
- Task 16: `LiveView` focus-routing on `cursorChanged` — focus-routing into resolved rows. Independent of holes.
- Task 17: App title bump.
- Task 18: Dogfood gate.

The integration tasks (12, 13–15, 16) are still implementable. They just don't deliver the dogfood criterion alone; they deliver the half of structural editing that doesn't need holes.

### 8.3 Status doc

`docs/restoration-status.md` has been updated to reflect:
- R5 Tasks 1–11 in the Recent-changes log.
- A **Spec-amendment log** entry referencing this document as the basis for an upcoming amendment proposal.
- The TL;DR pointer redirected to this document for the next session.

---

## 9. Reference index

The five documents that define this question, in dependency order:

1. **This document** — `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md` *(call for design)*
2. **The legacy projection-layer spec** — `docs/specs/2026-05-01-live-projection-layer.md` *(prior art; v1 design)*
3. **The deferred v1 redesign brief** — `docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md`
4. **The C-architecture restoration spec** — `docs/specs/2026-05-02-live-render-restoration-design.md` *(the spec to be amended)*
5. **The audit** — `docs/2026-05-02-live-view-architectural-audit.md` *(why holes were retired in the first place)*

Plus:

- **The R5 plan** — `docs/plans/2026-05-02-live-render-r5-structural-keys.md`
- **The legacy projection-layer code** — `libs/markoff-view-qml/src/LiveProjectionLayer.{h,cpp}`
- **The R5 test corrections** — commit `b8fb639` in `tst_live_render_structural.cpp`

---

*End of document.*
