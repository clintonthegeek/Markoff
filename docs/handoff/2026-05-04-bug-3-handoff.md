# R5.5 Bug 3 — atStart cursor delivery — unresolved (2026-05-04)

**Branch:** `exploration/new-foundation`
**HEAD at handover:** `dd64de5 r5.5(marker): atStart cursor delivery — properly target user content (Bug 3 v2)`
**Status:** R5.5 dogfood gate (Task 18) blocked. Three implementation attempts at the atStart cursor-delivery code path have all reproduced the same user-visible symptom in real-document dogfood. No synthetic unit test reproduces the failure on any of the three commits.

## Symptom

User opens `docs/phase-c-status.md` (a real ~838-line working document with mixed paragraphs, headings, lists, code fences). Scrolls to the latter portion of the document. Puts the caret at qtPos 0 of a regular paragraph. Presses Enter. Expected: a blank line appears above; the cursor stays at qtPos 0 of the user's content (now shifted down one row). **Observed**: the blank line appears, but the cursor lands at qtPos 0 of the paragraph that was *originally following* the user's content (i.e., one paragraph past the user's intended target).

The bug reproduces consistently. The user reproduced it 3 times in a row in pass 2, and again in pass 3 after the byte-keyed fix.

## Evidence

The dogfood pass 2 log is at `/tmp/dogfood2.txt`. Key signals:

- `tryHandle` fires with the correct `blockIndex` and `blockTextLen` for the user's content.
- The Bug 1 list-gate (`rowIsListOrQuoteContent`) correctly does NOT fire — the affected blocks are regular paragraphs, not list items.
- `requestTextCaretAtAnchor` (or `requestTextCaretAtByte` in v2) fires.
- `onRowsInserted [N, N]` fires for the marker insertion.
- The resolver requests `TextCaret(innerRow=N+1, qtPos=0)`.
- `LiveView.onCursorChanged` fires with `innerRow=N+1, itemFound=true`.
- `ParaDelegate.focusEditAt` fires at `modelIndex=N+1` but with an `editLen` that matches the *originally-following* paragraph's length, NOT the user's content's length.

So row N+1 in the post-edit model contains the originally-following paragraph's content. The user's content is somewhere else (perhaps row N+2, or perhaps the byte-range search in v2 returns a row whose content disagrees with the byte position).

Specific data points pulled from the pass-2 log:

- Pre-edit caret positions seen at `modelIndex=99`, `106`, `117`; pre-edit `blockTextLen=426`, `171`, `86` respectively.
- Post-edit `focusEditAt` lands at `modelIndex=100`, `107`, `118` with `editLen=51`, `173`, `242` — none of which match the pre-edit user content lengths.
- Source-line analysis confirmed the cursor is landing on the *originally-following* paragraph each time, not the user's shifted content.

## What has been tried

| Commit | Approach | Result |
|---|---|---|
| `7718c54` (Bug 2 fix) | Row-keyed: `requestTextCaretAtNewRow(blockIndex+1, 0)` with relaxed `onRowsInserted` bound check (resolve when `first <= pendingRow`, not just when pending in `[first, last]`). | Single-paragraph test passes. Multi-paragraph dogfood reproduces Bug 3. |
| `3c86b76` (Bug 3 v1) | Anchor-keyed: new `requestTextCaretAtAnchor(BlockAnchor, qtPos)`. Captures pre-edit `c.blockAnchor`. Resolver searches model for matching anchor on every `rowsInserted`. Pending-anchor swap on `onAnchorRenumbered`. Reverted the bonus check. | All synthetic tests pass. Dogfood pass 2 reproduces Bug 3. Resolver consistently maps to row N+1 (which holds the originally-following paragraph, not the user's content). |
| `dd64de5` (Bug 3 v2) | Byte-keyed: new `requestTextCaretAtByte(document, byte, qtPos)`. Computes `userContentByte = c.currentBlockStart + markerBytes` and asks foundation which row's `blockByteRange` contains that byte. | All synthetic tests pass. Dogfood pass 3 reproduces Bug 3. |

Three fundamentally different identity mechanisms for the cursor target — row index, CRDT anchor, source byte position — and the bug persists across all three. That is the strongest signal that the bug is not in the cursor-delivery key.

## Hypotheses to investigate next

The structural handler's atStart branch has been rewritten three times in fundamentally different ways (row index, CRDT anchor, byte position) and the bug persists. **The bug may not be in the structural handler at all.** Candidate downstream sites:

1. **QML delegate recycling.** `LiveView.qml`'s `ListView` recycles `ParagraphDelegate` instances. After parse-back, the delegate at QML index N+1 is a recycled instance that previously displayed the originally-following paragraph. The delegate receives a new `model.text` binding update (to the user's content), but the timing of focus delivery vs text refresh may cause `focusEditAt` to fire on the old text. The `editLen` in the dogfood log reflects the QTextDocument's current text length, which may be the stale pre-edit content if the delegate hasn't refreshed. **If the cursor IS landing on row N+1 with the user's content but the visual hasn't refreshed, the user perceives the bug.**

2. **`LiveEditBinding::setText`'s freshness rule.** When the model emits `dataChanged` for row N+1 (now holding the user's content), `LiveEditBinding::setText` may reject the update if the row's `lastEditEditSequence` is greater than the parse's input edit sequence. This was the v2 staleness-mark mechanism. The atStart edit calls `setRowEditSequence(c.blockIndex, ...)` for the OLD row index — which is now the marker row. The new row N+1 (user's content) doesn't have the staleness mark, so the freshness rule should accept the parse update. But verify.

3. **`LiveBlockModel::dataChanged` ordering.** AstBlockDiff may emit ops in an order that confuses the consumer chain. E.g., Insert(marker at N) followed by Equal(user's content at N+1, with renumbered anchor). The Equal might emit `dataChanged(N+1, N+1)` AFTER the cursor has already been delivered.

4. **`LiveListModelBinding::cursorState->setSignalModel(m_model.get())`.** The cursor state is wired to the inner block model. Verify the signal-model wiring is correct after Task 13's proxy retirement.

5. **The QML side's `onCursorChanged` handler.** `LiveView.qml`'s `ANCHOR innerRow=N+1 itemFound=true` log shows the cursor was delivered to QML correctly. But `ParaDelegate.focusEditAt modelIndex=N+1 qtPos=0 editLen=L` — the `L` is the QTextDocument length at that delegate. If `L` corresponds to the originally-following paragraph (e.g., 173 chars in pass 2's repro 2 vs. 171 for the user's content), the delegate is bound to the wrong text — even though the model row is correct.

The `editLen` discrepancy in the dogfood logs is the strongest evidence that the bug is downstream of the cursor state. The cursor state correctly resolved to row N+1 (whatever its identity-mechanism); the QML delegate at row N+1 is somehow showing the wrong text.

## Recommended starting point for the next agent

1. Add a diagnostic logging line in `LiveBlockModel::data` that prints (row, role, returned text length) for any text-role lookup. Run dogfood. Compare what the model says is at row N+1 vs what `ParaDelegate` displays.
2. Add a diagnostic in `ParaDelegate.qml`'s `text:` binding to log when it updates (with the new text length).
3. Trace: model says row N+1 has user's content (171 chars). Delegate at row N+1 shows 173 chars. Where's the gap?

If the model genuinely has the wrong content at row N+1, look at AstBlockDiff. If the model is correct but the delegate is stale, look at QML binding propagation and the freshness rule.

## Scope reminder

The R5.5 marker design is fundamentally about top-level paragraphs (spec §0). Bugs in other block kinds (lists, code blocks, blockquotes, etc.) are out of scope for the marker design itself — they need their own R7+ work. Bug 1 was the obvious "list-item Enter shouldn't trigger marker insertion" gate; it's fixed. Bug 3 is specifically about top-level paragraphs in mid-document position, which is squarely in the marker design's scope.

## Files affected by the three Bug 3 attempts

- `libs/markoff-live-render/include/markoff/live-render/LiveCursorState.h`
- `libs/markoff-live-render/src/LiveCursorState.cpp`
- `libs/markoff-live-render/src/LiveStructuralKeyHandler.cpp`
- `libs/markoff-live-render/tests/tst_live_render_structural.cpp`
- `docs/specs/2026-05-03-marker-paragraph-design.md`

The structural handler's atStart branch is currently the byte-keyed path. The cursor state has both anchor-keyed (`requestTextCaretAtAnchor`, `resolvePendingForAnchor`) and byte-keyed (`requestTextCaretAtByte`) public methods; consider whether the next investigation needs either or whether the right answer is upstream/downstream of these entirely.

## Related docs

- `docs/specs/2026-05-03-marker-paragraph-design.md` — the active R5.5 design. §4.2 now carries an "UNRESOLVED" status note pointing at this handoff.
- `docs/restoration-status.md` — phase board; R5.5 is `dogfood-blocked`. Dogfood log has entries for passes 1, 2, 3.
- `docs/handoff/2026-05-04-r5.5-dogfood-architectural-review.md` — earlier review that retired v2 holes (background, not Bug 3 specific).
- `docs/handoff/2026-05-03-section-3-1-spike-findings.md` — parser-acceptance spike that validated the marker character.
- `/tmp/dogfood2.txt` — pass 2 log (529 lines). Not committed; ephemeral. If the file is gone by the time you read this, re-run dogfood on `docs/phase-c-status.md` to regenerate.
