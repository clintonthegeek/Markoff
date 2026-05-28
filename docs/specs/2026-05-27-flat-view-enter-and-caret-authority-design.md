# Flat-view Enter semantics + caret-authority chokepoint — design

> **2026-05-27. Branch `master`.** Supersedes the diagnosis in
> [`../handoff/2026-05-27-cursor-authority-fix-handoff.md`](../handoff/2026-05-27-cursor-authority-fix-handoff.md):
> that brief assumed the only gap was caret re-assertion after a structural
> edit (guide §B.1). Empirical reproduction (below) showed the structural
> edit itself is **dropped** for Enter-at-paragraph-end — so the fix has a
> forward-path half the handoff did not anticipate. This spec is authoritative
> for the work; the handoff remains useful for its references and the §B
> orientation.
>
> Required reading: [`../VIEW-IMPLEMENTORS-GUIDE.md`](../VIEW-IMPLEMENTORS-GUIDE.md)
> §B, [`../INVARIANTS.md`](../INVARIANTS.md),
> [`2026-05-22-cursor-authority-decision.md`](2026-05-22-cursor-authority-decision.md)
> (the live-side L3 chokepoint this mirrors),
> [`2026-05-27-markoff-core-binding-robustness-design.md`](2026-05-27-markoff-core-binding-robustness-design.md)
> (the forward/reverse path this extends).

## 1. Problem — established by reproduction, not by report

Pressing Enter (`Qt::Key_Return`) in the `markoff-styled` `QTextEdit` was
reproduced at HEAD with a throwaway diagnostic against the real `Editor`
(loaded doc → set caret → `QTest::keyClick(Return)` → inspect model + caret):

| Scenario | Model after Enter | Caret after Enter | Verdict |
|----------|-------------------|-------------------|---------|
| Mid-paragraph (`HelloWorld`, caret @5) | splits → `Hello` / `World` (2 blocks) | sep-view pos 7 = start of `World` | **correct** |
| End-of-paragraph (`Hello`¶`World`, caret @ end of `Hello`) | **NO-OP** — still 2 blocks, `flatView` unchanged | drifts to pos 6 — inside the `\n\n` gap | **broken: no new paragraph** |
| End-of-document (`Hello`, caret @ end) | **NO-OP** — still 1 block | stays at pos 5 | **broken: Enter does nothing** |

**Root cause.** A lone `\n` inserted at a paragraph boundary is dropped by
`MarkoffDocument::applyFlatEdit`. The cursor-edit "start-of-next-block" bias
(`MarkoffDocument.cpp:1490`) attributes the newline to the *start* of the
following block; empty-head suppression (`:1571`) and the canonical-structure
invariant *"flat views never contain empty blocks"* then collapse it to a
no-op. The reverse-sync diff (`onD2DocumentChanged`) erases the user's typed
`\n` because the model never changed; the visible caret is left stranded in
the inter-block gap. With styling applied this reads to the user as *"the
caret jumped into the next paragraph"* — the symptom the handoff recorded —
but the mechanism is a dropped structural edit, **not** a mis-placed caret
after a successful one.

Consequence: the handoff's proposed fix (port `establishFocus`, re-assert the
caret) would not fix the reported bug — there is no successful structural
edit to re-assert against. The real fix needs a **forward-path** change
(make Enter create the paragraph) *and* the **caret-authority** change (place
the caret in the new block).

## 2. Decision — word-processor semantics

Confirmed with the user (2026-05-27): Enter in the flat-text views behaves
like a word processor / Obsidian. Pressing Enter **always creates a new
paragraph and moves the caret into it**, including at end-of-paragraph and
end-of-document. A still-empty paragraph is allowed to exist **transiently**;
it collapses on save/reload (it serialises as the ordinary `\n\n` separator
and tree-sitter re-parses adjacent paragraphs without an empty block — so it
does not round-trip, which is the desired "trailing blank line is trimmed"
behaviour).

This is a deliberate, **named exception** to the canonical-structure
invariant established in
[`2026-05-27-markoff-core-binding-robustness-design.md`](2026-05-27-markoff-core-binding-robustness-design.md):
the invariant ("no empty blocks") continues to hold for the **programmatic /
canonical ingress** (`applyFlatEdit`); the new **interactive ingress** may
create transient empty blocks. The two ingresses are kept physically separate
(§3) so the canonical path's well-tested behaviour is untouched.

### L3 authority decision (INVARIANTS §2, written before the plan)

**The `SourceTextDocumentBinding` is the single authority for the post-
structural-edit caret in the flat-text views. The widget (`QTextEdit` /
`QPlainTextEdit`) is a dumb applier.** This is the single-document analogue
of `LiveCursorState` as the chokepoint
([`2026-05-22-cursor-authority-decision.md`](2026-05-22-cursor-authority-decision.md)).
Cursor placement after a structural edit happens in exactly one place — the
tail of `onD2DocumentChanged` — and is delivered to the widget through one
signal. No second path writes the caret.

## 3. Forward path — `applyInteractiveNewline` (`markoff-core`)

A new public `MarkoffDocument` method, symmetric with `applyFlatEdit` but
**non-canonicalising**:

```cpp
// Interactive ingress: split the block containing the no-separator global
// byte offset `atByte` on a single newline. The head ([blockStart, atByte))
// stays in the current block; the tail ([atByte, blockEnd)) moves into a NEW
// Paragraph block inserted immediately after. The tail MAY be empty — this is
// the deliberate WYSIWYG exception to the no-empty-block canonical rule.
// Unlike applyFlatEdit, performs NO newline-run collapsing / canonicalisation.
// Returns the BlockId the caret should occupy at offset 0 (the new tail block).
BlockId MarkoffDocument::applyInteractiveNewline(uint32_t atByte, Origin origin);
```

Implementation outline (own `UndoLog::Transaction`, like `applyFlatEdit`):

1. Resolve `atByte` to `(blockId, byteInBlock)` by walking `iterateBlocks()`
   accumulating `blockText(id).size()` (no-sep coordinates; same walk
   `applyFlatEdit` uses). **Interior-boundary bias = previous block:** when
   `atByte` equals the cumulative end of block N (== start of N+1), resolve to
   `(block N, end-of-N)`, *not* `(N+1, 0)`. This matches the binding handing in
   `sepViewToNoSepByteForEdit(sepStart, biasForward=false)` and is what makes
   Enter-at-end-of-paragraph split the paragraph the user is *leaving* — the
   new empty block lands *after* it and the caret lands *on the new empty
   line*, not on the following paragraph. (Choosing next-block bias would put
   the empty block before N+1 and strand the caret on N+1's content — the
   wrong feel for end-of-paragraph Enter.) End-of-document resolves to the last
   block at its end. Empty document → auto-create one Paragraph then split it
   (two empty paragraphs; caret in the second — expected for Enter on an empty
   doc).
2. `tail = blockText(blockId).mid(byteInBlock)`.
3. If `tail` non-empty: `d2ApplyBufferEdit(blockId, byteInBlock, tail.size(),
   {}, t)` to trim the head.
4. `newBlk = d2InsertBlock(blockId, BlockKind::Paragraph, t)` (predecessor =
   `blockId`, so it lands immediately after).
5. If `tail` non-empty: `d2ApplyBufferEdit(newBlk, 0, 0, tail, t)`.
6. Return `newBlk`.

Position correctness (all reuse the one rule "head stays, tail→new block,
caret at new-block offset 0"):
- **End of block** → tail empty → empty `newBlk`, caret in it. (Fixes the bug.)
- **Mid-block** → ordinary split; caret at start of the tail block.
- **Start of block** → head empty (current block becomes empty), tail (all
  content) → `newBlk`; caret stays with the content = blank line inserted
  above. (Word-processor-correct.)

The transaction emits `d2DocumentChanged` (debounced) exactly as the existing
D2 mutators do, so the reverse path runs normally.

### Binding dispatch (`SourceTextDocumentBinding::onQtContentsChange`)

Add one branch **above** the existing structural (`applyFlatEdit`) branch:

> If the edit is a **pure single Enter** — `charsRemoved == 0` and
> `insertedUtf8 == "\n"` — translate `sepStart` to a no-sep offset
> (`sepViewToNoSepByteForEdit(doc, sepStart, /*biasForward=*/false)`), call
> `applyInteractiveNewline(noSep, Origin::UserEdit)`, and set the pending
> caret (§4) to `{returnedBlockId, 0}`.

Everything else is unchanged: the single-block fast path, the cross-block
non-structural merge path, and the structural `applyFlatEdit` path (paste,
multi-newline inserts, selection-replace-with-newline) all keep their current
routing. Rationale for the narrow trigger: only a bare Enter should be able to
mint a transient empty block; pasted markdown must still canonicalise.

> **Selection + Enter** (`charsRemoved > 0` and inserted `== "\n"`) is **out
> of scope** for this spec and continues through the existing structural path
> (which deletes the selection and inserts via `applyFlatEdit`). Its caret is
> not specially re-asserted here. Noted as a follow-up in §7.

## 4. Caret authority — the chokepoint

### Pending-caret store (mirrors live's `m_pendingFocus`)

```cpp
struct PendingCaret { Markoff::BlockId block; int offsetInBlock; };  // bytes
std::optional<PendingCaret> m_pendingCaret;
```

Set by structural operations that declare an intended landing:
- Enter → `{newBlk, 0}` (from `applyInteractiveNewline`'s return).
- Backspace-at-block-start / Delete-at-block-end **merge** (the existing
  cross-block-non-structural path in `onQtContentsChange`) → `{mergedInto,
  joinOffsetBytes}`, where `joinOffsetBytes` is the byte length of the
  start block's surviving head (the merge point). This closes guide §B.3.

### Resolution + delivery

At the **tail of `onD2DocumentChanged`**, after the incremental reverse diff
has settled the `QTextDocument`:

```cpp
if (m_pendingCaret) {
    const int sepPos = sepViewPosOf(m_pendingCaret->block,
                                    m_pendingCaret->offsetInBlock);
    emitCaret(sepPos, sepPos);          // collapsed caret
    m_pendingCaret.reset();
}
```

- `sepViewPosOf(BlockId, byteOffset)` walks `iterateBlocks()` summing each
  preceding block's text length **in UTF-16 code units** plus `2` for each
  `interBlockSeparator()` (`"\n\n"`), then adds `byteOffsetToQtPos(blockText,
  byteOffset)` for the in-block offset. Result is a `QTextDocument` position
  (UTF-16), i.e. sep-view coordinates — the space the `QTextEdit` holds.
- `emitCaret(start, active)` is the **single emit point**: `Q_EMIT
  caretResolved(start, active);`.

No `QTimer::singleShot` / `Qt.callLater`: `d2DocumentChanged` is already
debounced to a later event-loop turn, so the re-assert lands after the
synchronous keystroke without any added deferral (INVARIANTS §6 stays clean —
nothing to justify).

Ordinary typing sets **no** pending caret, so `caretResolved` is not emitted
and the natural `QTextEdit` caret is left exactly where the user's keystroke
put it.

### New signal + widget wiring

```cpp
signals:
    // Sep-view (QTextDocument) positions. The owning widget applies this to
    // its real caret. start==active for a collapsed caret.
    void caretResolved(int start, int active);
```

`Markoff::Styled::Editor` and `Markoff::Source::Editor` each connect it to a
one-line slot:

```cpp
connect(m_binding, &SourceTextDocumentBinding::caretResolved, this,
        [this](int start, int active) {
            QTextCursor c(m_editor->document());
            c.setPosition(start);
            if (active != start) c.setPosition(active, QTextCursor::KeepAnchor);
            m_editor->setTextCursor(c);
        });
```

### Retire the dead int-property layer (INVARIANTS §3, same plan)

A repo-wide search (styled, source, live, app, tests) confirmed **zero
consumers** of the binding's int cursor layer. Delete, in this plan:
`cursorPosition()`/`selectionStart()`/`selectionEnd()` getters + setters, the
`cursorPositionChanged`/`selectionStartChanged`/`selectionEndChanged` signals,
and the `m_cursorPosition`/`m_selectionStart`/`m_selectionEnd` members. The
new `caretResolved` signal is their sole successor. `pushSelectionToSession`
is removed along with the int-setters that were its only callers (user→Session
selection sync is not wired today regardless; see §7).

### `syncFromSession` survives, rewritten (the B.2/B.4 mechanism)

`syncFromSession` is **not** dead scaffolding — it is the model→view path for
externally-driven carets (a collaborator's edit, or undo *if* it restores the
Session selection). It is rewritten to:

1. resolve the Session `Selection` anchors with `resolveTextAnchor` (no-sep
   bytes), then map to **sep-view** positions via the same `sepViewPosOf`
   helper — **fixing the current coordinate bug** where it concatenated
   `blockText` with no separators (`SourceTextDocumentBinding.cpp:209–211`);
2. deliver through the **same single emit point** (`emitCaret` →
   `caretResolved`) — not the deleted int signals.

`onSessionPrimarySelectionChanged` only fires on genuine Session changes; the
local Enter path is binding-local (`m_pendingCaret`) and does **not** push to
the Session, so the two trigger sources are disjoint and feed one sink — one
chokepoint, no racing stores of truth (satisfies INVARIANTS §3's intent).

## 5. Tests (INVARIANTS §4 — falsifiable, at the widget level)

Modeled on `tst_styled_dogfood_invariants` (real `Editor`, offscreen,
`QTest::keyClick`). Each structural assertion is proven to **fail on HEAD
first**; falsifiability is additionally demonstrated by stubbing the
chokepoint to a no-op and confirming the caret assertions fail.

1. `enter_at_paragraph_end_creates_block_and_places_caret` — load
   `A`¶`B`; caret at end of `A`; `keyClick(Return)`; assert **block count is
   3** (A, empty, B) and the caret is at the start of the new middle block —
   not stranded in the gap, not at the start/end of `B`. *(Fails on HEAD:
   block count stays 2.)*
2. `enter_at_document_end_creates_block` — load `A`; caret at end;
   `keyClick(Return)`; assert block count 2 and caret in the new empty block.
   *(Fails on HEAD: block count stays 1.)*
3. `enter_mid_paragraph_splits_with_caret_at_new_block` — load `AB` (one
   block); caret between; `keyClick(Return)`; assert split + caret at start of
   the tail block. *(Locks the currently-correct behaviour against
   regression.)*
4. `backspace_at_block_start_merges_with_caret_at_join` (guide §B.3) —
   load `A`¶`B`; caret at start of `B`; `keyClick(Backspace)`; assert blocks
   merged to `AB` and caret at the join offset (byte length of `A`).
5. Typing-does-not-disturb-caret guard — type an ordinary character
   mid-block and assert no `caretResolved` was emitted (the chokepoint only
   fires for structural ops). Protects against over-firing.

Tests live with the styled leaf (they exercise the shared binding through the
real widget); a parallel slot or shared helper covers `markoff-source` where
the binding behaviour is identical.

## 6. Doc / invariant updates (part of this plan, not a follow-up)

- `libs/markoff-core/CLAUDE.md` "Single-document binding: canonical structure
  invariant" + the binding-robustness spec: add `applyInteractiveNewline` as
  the named **interactive-ingress exception** — transient empty blocks
  allowed, collapse on serialise; canonical `applyFlatEdit` unchanged.
- `docs/VIEW-IMPLEMENTORS-GUIDE.md`:
  - **Correct the §B.1 root-cause prose** — it currently repeats the
    handoff's wrong mechanism ("`applyFlatEdit` canonicalises the inserted
    `\n` to a `\n\n` separator … just re-assert the caret"). Replace with the
    dropped-newline mechanism + the `applyInteractiveNewline` resolution.
  - Status table + per-concern lines: B.1 → ✅ (source, styled); B.3 → ✅;
    B.2 → 🟡 (collab mechanism via fixed `syncFromSession`); B.4 → 🟡
    (as far as undo repopulates the Session selection).
- `libs/markoff-styled/CLAUDE.md` + `libs/markoff-source/CLAUDE.md`: §B
  "open" notes → closed for B.1/B.3.
- `docs/queue.md` #7 → resolved; Discipline Log entries for any smell touched.

## 7. Out of scope / follow-ups

- **Selection + Enter** (replace a selection with a paragraph break) keeps
  current routing; its caret is not specially re-asserted. Follow-up.
- **User→Session selection push** (so a collaborator sees the local caret) is
  not wired today and is not added here; B.2 is delivered only for the
  inbound (Session→view) direction.
- **Full B.4** (undo restores the exact pre-edit caret) depends on
  `undoD2`/`redoD2` repopulating the Session selection; this spec wires the
  delivery mechanism but does not add selection capture to the undo log.

## 8. Definition of done

- Tests §5.1–§5.5 green; §5.1/§5.2/§5.4 proven to fail on HEAD first.
- Fast suite (`scripts/run-tests.sh -E 'tst_realistic|tst_benchmark'`) green
  except the three known pre-existing live-side failures
  (`tst_live_render_e2_nav_shift_extend`,
  `tst_live_render_focus_chokepoint_invariant`,
  `tst_live_render_cursor_typing_invariant`).
- Dead int-property layer deleted; `caretResolved` is the sole caret-output.
- Docs in §6 updated.
- User dogfood confirms Enter creates a paragraph and the caret lands in it,
  at end-of-paragraph and end-of-document.
