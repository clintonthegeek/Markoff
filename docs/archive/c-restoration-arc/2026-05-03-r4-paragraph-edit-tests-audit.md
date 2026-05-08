# R4 paragraph_edit failing-tests audit (2026-05-03)

## TL;DR

All 5 failing tests are **implementation regressions, not stale tests**. Every one of them probes behaviour that the current spec still intends (spec §4.5, §7.1). The tests were introduced at commit `fa8e80d` (2026-05-02) and their assertions remain correct. The regression was introduced at `7d49718` (same day, during the R4 dogfood fix), which added a `pushTextToDocument()` call to `setRawTextDocument()`. That call pushes `m_text` (the QML-bound `text` property, default `""`) into the test's already-populated `QTextEdit` document, silently clearing it before the test performs any edits. The tests never set `eb.text = "..."` because in the QML production path the `text` property is bound to `model.text` before `setTextDocument` is called; but in the test path using `setRawTextDocument`, the `text` property is never set, so `pushTextToDocument()` pushes an empty string. R5.5 can proceed without fixing these, but fixing them is a small contained change (update the test setup, not the implementation) and is worth doing before R5.5 Task 17 (the stress-typing test) to keep the test suite honest.

## Per-test verdicts

### 1. typing_one_char_emits_one_apply_local_edit

- **Asserts:** One `QTextDocument::insertText` call triggers exactly one `MarkoffDocument::contentsChanged` emission, advances `editSequence()`, produces correct output text `"helloA world"`, and stamps the row's `rowEditSequence` to `document.editSequence()`.
- **Origin:** Commit `fa8e80d` (2026-05-02). Motivated by R4 plan §Task 5 "Create the test file" acceptance criterion: "typing-during-in-flight-parse, mid-block insert, no-Qt.callLater invariant, IME deferral, applyingModelUpdate guard" and spec §7.1 steady-state typing data flow.
- **Architecture context:** At `fa8e80d`, `setRawTextDocument` called only `rewireTextDocument(td)` — no `pushTextToDocument()`. The test populated `editor.setPlainText("hello world")` before calling `setRawTextDocument`, and the document was correctly wired to receive user edits. Commit `7d49718` (the dogfood content-duplication fix, same day) added `pushTextToDocument()` to both `setTextDocument` and `setRawTextDocument`. In the QML production path, the `text` Q_PROPERTY is bound to `model.text` **before** `setTextDocument` is called, so `m_text` holds the correct block text when `pushTextToDocument()` runs, making it a no-op (or confirming write). In the test path, `m_text` is never set (the tests predate the `text` property and never call `eb.setText(...)` or `eb.text = ...`), so `pushTextToDocument()` pushes `""` into the test's QTextEdit — clearing it. After the clear, `m_previousText = ""`. The test then inserts 'A' at position 5, but the document is empty/cleared, so the inserted text lands at position 0 and `m_previousText` is wrong.
- **Spec-check:** Spec §7.1 still describes this exact flow as the intended steady-state typing path. Spec §4.5 still mandates the `rowEditSequence` stamp. No spec change since `fa8e80d` has invalidated this test's assertions.
- **Failure-mode diagnosis:** `document.toMarkdown()` returns `"helloA world"` but the actual is something else (likely wrong offset or wrong content). The failure is consistent with the document being cleared by `pushTextToDocument()` before the test's edit runs. The test's expected output is correct; the code path diverged by the later post-dogfood fix.
- **Verdict:** **Test is correct; implementation has regressed.** The regression site is `LiveEditBinding::setRawTextDocument` at `libs/markoff-live/src/LiveEditBinding.cpp` lines 50–54. The `pushTextToDocument()` call is correct for the QML production path but breaks the test-only `setRawTextDocument` seam. Fix: before calling `setRawTextDocument(td)` in each test, also call `eb.setText(initialContent)` (or `eb.text = "..."`), so that `m_text` holds the correct initial value and `pushTextToDocument()` becomes a no-op (the document already has that text). Alternatively, the test setup could mirror the R4 plan's original design: use `TextDocHost : public QQuickTextDocument` and `setTextDocument` instead of `setRawTextDocument` — the production path that already works correctly.

---

### 2. deletion_emits_correct_old_range

- **Asserts:** Deleting characters "cd" from "abcdef" (qtPos 2, 2 chars removed) produces `document.toMarkdown() == "abef"` — i.e. the CRDT receives a delete of the correct byte range.
- **Origin:** Commit `fa8e80d` (2026-05-02). Same motivation as test 1: R4 plan §Task 5, spec §7.1. Specifically tests the `charsRemoved > 0` branch of `onContentsChange` and the `Coordinates::qtPosToByte` translation of the deletion range.
- **Architecture context:** At `fa8e80d`, `setRawTextDocument` was clean (no `pushTextToDocument`). The test editor had "abcdef" correctly wired. After `7d49718`, `setRawTextDocument` clears the document to `""` via `pushTextToDocument()`. With `m_previousText = ""` and an empty document, the removal of "cd" from position 2..4 has undefined behaviour (the document is empty or contains only a newline). `Coordinates::qtPosToByte("", 2)` returns `0` (clamped), so both `oldStartLocal` and `oldEndLocal` are `0`, and no bytes are actually deleted. The CRDT still has "abcdef" intact.

  The actual result `"abcdef"` (nothing deleted) is consistent with this. The test expected `"abef"`.
- **Spec-check:** Spec §7.1 data flow, step 2 (deletion): `oldEnd_block = qtPosToByte(modelTextUtf8, qtPos + charsRemoved)`. The deletion path is unchanged in all R5 commits. The test correctly probes this path.
- **Failure-mode diagnosis:** Failure consistent with the document being cleared before the deletion runs. The test is right; `pushTextToDocument()` corrupted the pre-edit reference.
- **Verdict:** **Test is correct; implementation has regressed.** Same regression site as test 1. Fix: add `eb.setText("abcdef")` before `eb.setRawTextDocument(editor.document())`.

---

### 3. utf8_multibyte_byte_offsets_correct

- **Asserts:** Inserting "X" at QChar position 2 of "héllo" (where `é` is 2 UTF-8 bytes) produces `"héXllo"` — i.e. `qtPosToByte` correctly maps QChar index 2 to byte offset 3.
- **Origin:** Commit `fa8e80d` (2026-05-02). Motivated by the R4 plan's explicit note: "Tests cover ASCII insert, mid-block insert, deletion, and multi-byte UTF-8 (é = 2 bytes)". Also corresponds to spec §7.1 step 2 ("Convert qtPos (UTF-16 code units, block-local) to a UTF-8 byte offset").
- **Architecture context:** At `fa8e80d`, `setRawTextDocument` did not call `pushTextToDocument()`. After `7d49718`, it does. Document is cleared to `""`. `m_previousText = ""`. The test inserts "X" at qtPos 2. With an empty document (the cursor silently clamps to position 0), "X" is inserted at position 0. `Coordinates::qtPosToByte("", 2)` returns 0. The CRDT receives an insert at byte 0 (the very start of "héllo"), yielding `"Xhéllo"`. The reported actual `"Xhéllo"` matches this analysis exactly.
- **Spec-check:** Spec §7.1 and `Coordinates::qtPosToByte` are unchanged and still correct. The test probes genuine R4 behaviour.
- **Failure-mode diagnosis:** Actual `"Xhéllo"` vs expected `"héXllo"` is precisely what happens when the pre-edit reference is empty: `qtPosToByte("", 2)` → 0, so the insert goes to byte 0. This is the `pushTextToDocument` side-effect, not a `Coordinates` bug.
- **Verdict:** **Test is correct; implementation has regressed.** Fix: add `eb.setText("héllo")` before `eb.setRawTextDocument(editor.document())`.

---

### 4. typing_two_chars_before_parse_arrives_does_not_scramble

- **Asserts:** Typing 'A' then 'B' consecutively before any parse cycle completes produces `"helloAB"` in the CRDT — i.e. `m_previousText` caching prevents the second keystroke from computing its offset against stale model text and scrambling to `"helloBA"`.
- **Origin:** Commit `5e12f10` (2026-05-02), the immediate follow-on to `fa8e80d`. The commit message says: "Code reviewer flagged that LiveEditBinding::onContentsChange used record.text (the model's last-parsed text) as the pre-edit reference... two consecutive keystrokes before a parse cycle completed scrambled into reversed bytes (e.g. typing 'AB' produced 'BA' in the CRDT)." This test is a regression test for the `m_previousText` cache that `5e12f10` introduced.
- **Architecture context:** At `5e12f10`, `setRawTextDocument` still didn't call `pushTextToDocument()` (that came from `7d49718`). After `7d49718`, the document is cleared to `""` and `m_previousText = ""`. The test types 'A' at position 5 of what is now an empty document — the cursor clamps to position 0, so 'A' inserts at byte 0 of "hello", giving CRDT state "Ahello". Then types 'B' at position 6 (the test assumes the document has "helloA" at this point); position 6 clamps to position 1 in "Ahello" (or wherever the document is), so 'B' inserts somewhere unpredictable relative to the expected "helloAB". The actual is `"ABhello"` — consistent with both 'A' and 'B' being inserted at or near the front of the CRDT's "hello" because the empty `m_previousText` maps every position to offset 0.
- **Spec-check:** The `m_previousText` caching mechanism is correct and still active in the implementation. The test's assertion is valid and corresponds to spec §7.1 step 2 (pre-edit reference correctness). No R5 change has altered this path.
- **Failure-mode diagnosis:** Actual `"ABhello"` vs expected `"helloAB"`. The B-before-A pattern is a secondary effect: the document is cleared; both insertions compute offset 0 from the empty `m_previousText`; 'A' lands at byte 0 → "Ahello"; 'B' lands at byte 0 of "Ahello" → "BAhello" — but the scope guard updates `m_previousText = "Ahello"` after the first edit, so 'B' actually uses offset from `"Ahello"` with position 6 which may clamp to position 5 (length of "Ahello"), giving "AhelloB". The exact scramble depends on clamping; the reported `"ABhello"` indicates both go to offset 0. Either way, the root cause is `pushTextToDocument()` clearing the document.
- **Verdict:** **Test is correct; implementation has regressed.** Fix: add `eb.setText("hello")` before `eb.setRawTextDocument(editor.document())`.

---

### 5. ime_composition_defers_then_flushes_on_commit

- **Asserts:** While `composing == true`, intermediate `insertText` calls do not advance `document.editSequence()`; on `setComposing(false)`, a single `applyLocalEdit` fires and `document.toMarkdown()` becomes `"helloabc"`.
- **Origin:** Commit `77a84eb` (2026-05-02), the IME composition deferral feature commit. Motivated by spec §4.5 IME composition deferral cycle guard: "CRDT edits cannot be applied per-preedit-character (the preedit is not committed text). The composition-deferred path holds a pending state until the IME signals commit."
- **Architecture context:** At `77a84eb`, `setRawTextDocument` still did not call `pushTextToDocument()`. After `7d49718`, it clears the document. The test proceeds:
  1. `document.resetContent("hello")` — CRDT has "hello".
  2. `editor.setPlainText("hello")` — the test document has "hello".
  3. `eb.setRawTextDocument(editor.document())` → `pushTextToDocument()` clears the document to `""`. `m_previousText = ""`.
  4. `eb.setComposing(true)`.
  5. Three `insertText` calls ("a", "b", "c") land in the now-empty document at various positions. All are guarded by `m_composing == true` and skipped (CRDT unchanged, `seqBefore` holds).
  6. `eb.setComposing(false)` → `flushPendingComposition()`. This reads `blockByteRange(record.blockAnchor)` from the CRDT (which still has "hello"; range is [0, 5)). Then `postUtf8 = m_listenedDoc->toPlainText().toUtf8()`. The document now holds "abc" (typed into the empty document in steps 5). So the flush does `applyLocalEdit({oldStart=0, oldEnd=5, newText="abc"})` — replaces "hello" (bytes 0–5) with "abc". CRDT becomes "abc".
  7. `QCOMPARE(document.toMarkdown(), "helloabc")` fails; actual is `"abc"`.
- **Spec-check:** Spec §4.5 still mandates the IME deferral guard in its current form. The test's assertion `"helloabc"` is correct per the spec: the compose sequence appended "abc" to "hello", so the CRDT should hold "helloabc". The `flushPendingComposition` approach (replace the whole block range with the post-commit content) is the designed mechanism. The bug is not in the flush logic but in the CRDT mismatch: the CRDT's block range is "hello" (bytes 0–5) but the document the flush reads contains only "abc" (the empty document that was typed into). The CRDT range [0,5) is replaced with "abc" instead of "helloabc".
- **Failure-mode diagnosis:** The failure is caused by the document clearing in `setRawTextDocument`. The IME deferred characters were typed into an empty document (not "hello"), so the flush replaces the CRDT's "hello" with "abc" instead of "helloabc". The test is right.
- **Verdict:** **Test is correct; implementation has regressed.** Fix: add `eb.setText("hello")` before `eb.setRawTextDocument(editor.document())`.

---

## Cross-cutting findings

### File scaffolding

The test file `tst_live_render_paragraph_edit.cpp` uses `QTextEdit` and `setRawTextDocument` as the test seam. This is a valid and intentional test-only bypass (the friend-grant in `LiveEditBinding.h` documents it explicitly). The scaffolding is NOT architecturally stale — the seam is still needed because `QQuickTextDocument(nullptr)` crashes in Qt 6.11 (noted in the header comment at line 92–93 of `LiveEditBinding.h`). The `waitForModelRows` helper is also fine and still in use by the passing tests.

The one scaffold gap is that none of the failing tests call `eb.setText(...)` to initialise `m_text`. This was correct at the time of writing (before `pushTextToDocument()` was added to `setRawTextDocument`), but is now required. The fix pattern for each failing test is a one-line addition:

```cpp
eb.setText("hello");        // or whatever initial content
eb.setRawTextDocument(editor.document());
```

This reproduces the QML production ordering: `text` property bound first, document wired second.

### Passing tests — correctness check

The 5 passing tests are:

1. **`typing_at_block_offset_translates_to_whole_doc_offset`** — also uses `setRawTextDocument` but the text being "second" (row 1) is not initialised via `eb.setText()` either. Yet it passes. Why? The test is structured identically to the failing `deletion_emits_correct_old_range` except it does an INSERT at position 0 (not a deletion). After `pushTextToDocument()` clears the document to `""`, the test inserts "X" at position 0. `Coordinates::qtPosToByte("", 0) = 0`. So the edit becomes: insert "X" at byte 0 of the second block. Since `blockStart` for row 1 is 7 ("first\n\n"), the edit is `{oldStart=7, oldEnd=7, newText="X"}`. The CRDT applies the insert at byte 7 of "first\n\nsecond", producing "first\n\nXsecond". This is the expected result! The test passes **by coincidence**: the empty-document-clear happens to produce the same result as correct operation because the test inserts at position 0 (which maps to offset 0 whether or not the pre-edit text is correct). **This test probes the right behaviour but passes for the wrong reason when `m_previousText` is `""`** — if the test were changed to insert at a non-zero position it would fail. Flag for rewrite alongside the 5 failing tests.

2. **`in_flight_parse_does_not_clobber_model_text_when_stale`** — also uses `setRawTextDocument` without `eb.setText()`. The document is cleared by `pushTextToDocument()`. The test then inserts "X" at position 5 of the empty document, which clamps to position 0 and inserts at the front of "hello", giving CRDT "Xhello". The row sequence is stamped. Then a stale `applyOps` arrives; the test checks `model.recordAt(0).text == "hello"` (the last-parsed text, not the CRDT text). The stale check works because `rowSeqAfterType > parseInputEditSeq`, so the stale guard preserves the old model text. Whether the CRDT actually has "helloX" or "Xhello" is irrelevant to this specific assertion. Then a fresh parse arrives with `"helloX"` (hardcoded); it's accepted. The test checks `model.recordAt(0).text == "helloX"` — which is correct for the fresh path. **This test passes, but its premise is subtly wrong**: the CRDT has "Xhello" not "helloX" (because the insert landed at position 0), so a realistic fresh parse would produce "Xhello", not "helloX". The test uses a synthetic `freshRec` with hardcoded `"helloX"`, which bypasses this inconsistency. The test is a valid unit test of the `applyOps` freshness gate in isolation but does not faithfully test the end-to-end "insert X at position 5" chain. Flag for audit when the failing tests are fixed.

3. **`model_update_does_not_echo_back_to_apply_local_edit`** — does NOT use `setRawTextDocument`. It creates a `QTextEdit` and calls `setRawTextDocument(editor.document())` but the document is `"aaa"`, `m_text = ""`. The clear happens. However, the test immediately drives `document.resetContent("bbb")` and waits for a parse — this resets the entire document to "bbb" and a new parse pushes the model to `"bbb"`. The test then calls `resetContent("ccc")`. The test only checks `applyingModelUpdate() == true` during `dataChanged`, which is driven by `applyOps`, not by any user edit via `LiveEditBinding`. The cleared document state is irrelevant to what this test probes. It passes because it never actually exercises the user-edit path. **The test is a valid unit test for the `applyingModelUpdate` flag**, correctly passing.

4. **`initTestCase`** and **`cleanupTestCase`** — Qt infrastructure slots, not present in the file (they use the implicit Qt test framework infrastructure). Not applicable.

### R4 plan acceptance criterion delta

The R4 plan (line 13) states the acceptance criterion as:
> "`tst_live_render_paragraph_edit` passes covering: typing-during-in-flight-parse, mid-block insert, no-Qt.callLater invariant, IME deferral, applyingModelUpdate guard."

The plan's Task 5 Step 1 (lines 668–829) specifies the test file using `TextDocHost : public QQuickTextDocument` and `eb.setTextDocument(&host)` (the QML path), not `setRawTextDocument`. The actual implementation used `QTextEdit` + `setRawTextDocument` instead — a divergence noted in the plan's test-design note: `QQuickTextDocument(nullptr)` was found to crash in Qt 6.11, so the `QTextEdit`-backed approach was adopted. This is a reasonable pragmatic adjustment. The test assertions are identical to what the plan specified.

The divergence in test setup (QTextEdit vs TextDocHost) was fine at `fa8e80d`. It became a problem only when `7d49718` added `pushTextToDocument()` to `setRawTextDocument` — a post-plan dogfood fix that the plan authors couldn't have anticipated.

### R5 silent invalidations

R5 Tasks 1–11 (commits `7c6f7f6..b8fb639`) do not touch `LiveEditBinding.cpp` or its test file. They add `LiveStructuralKeyHandler`, `UndoCoalescer`, and `LiveCursorState::requestTextCaretAtRow`. None of these invalidate the 5 failing tests' assertions.

The R4-era Enter-swallow commit `37b97bf` is not relevant to these tests (it adds `Keys.onPressed` in QML, not C++).

Commit `b8fb639` (R5 Task 11, "populate consumedStructuralKeys on built-in descriptors") is mentioned in the status doc as having "corrected 3 test assertions about parser behaviour — surfaced the empty-paragraph gap." That correction was in `tst_live_render_structural` (R5 tests), not in `tst_live_render_paragraph_edit`. It did not touch the R4 tests.

**There is no R5 commit that silently invalidates any of the 5 failing R4 tests.** The single causal commit is `7d49718` (R4, during the dogfood fix window on 2026-05-02), which introduced `pushTextToDocument()` in `setRawTextDocument`. The tests failed from that point onward but were masked by a stale test binary in CMake's cache.

---

## Recommendation

**Fix the test setup in `tst_live_render_paragraph_edit.cpp`: add `eb.setText(initialContent)` before each `eb.setRawTextDocument(editor.document())` call in the 5 failing tests, and apply the same fix to `typing_at_block_offset_translates_to_whole_doc_offset` (the passing test that passes by coincidence).** This is a pure test-file change (6 tests, one line each). No implementation changes are required — `LiveEditBinding.cpp` and `LiveEditBinding.h` are correct; `setRawTextDocument` is now correct for both the QML and test paths as long as `m_text` is initialised before the document is wired.

This is approximately 1–2 hours of work including verification, and should be done before R5.5 Task 17 (the stress-typing test). The status-board's "8 paragraph_edit slots green" claim was inaccurate; the correct count at HEAD is 5 passing, 5 failing, 10 total. After the fix it should be 10/10 green.

R5.5 **can proceed to Task 5** without blocking on this fix — the failing tests are in `tst_live_render_paragraph_edit` (an isolated executable), and no R5.5 task depends on them. However, the R5.5 stress-typing test (Task 17) will test the same code path more rigorously, so having the R4 tests green first provides a cleaner regression baseline. Recommended order: fix R4 tests → proceed with R5.5.

**Most-load-bearing single finding:** The root cause is not architectural staleness or R5 breakage. It is one line added during the R4 dogfood window (`7d49718`, `setRawTextDocument`: `pushTextToDocument()`). The fix in the tests is to add `eb.setText(initialContent)` (or rewrite the test setup to use `TextDocHost` as the R4 plan originally specified). The implementation is correct; only the test setup is wrong.
