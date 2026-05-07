# R5 holes post-mortem — response to the call for design

**Date authored:** 2026-05-03
**Branch:** `exploration/new-foundation`
**Worktree:** `.worktrees/foundation-exploration/`
**Responds to:** `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md` (the call for design)
**Author context:** fresh agent context, post-R5-Tasks-1–11; full read of the document trail per the call's §7.1.
**Status:** post-mortem only. The design doc, spec amendment, and re-derived R5.5 plan are sequenced after this document — they are not part of it. This report is the input to all three.

---

## 0. TL;DR

The call for design asked four things of this session: (1) post-mortem of the v0 holes implementation reverted in legacy `markoff-view-qml`; (2) verification that the v1 IME-preedit-pattern design (specced at `docs/specs/2026-05-01-live-projection-layer.md` §3.1–§3.5, never implemented) holds under the C architecture; (3) concrete amendments to the restoration spec; (4) a re-derived R5 plan. This document covers (1) and (2), and frames (3) and (4) as the next steps.

**Findings:**

1. v0 was broken by *both* its three implementation choices and its surrounding architecture, compounding. Neither v1 alone nor C alone would have sufficed; both axes had to flip. C is now in place; v1 has never been built. (§1.)

2. Each of v0's five named failure modes (`2026-05-01-live-projection-layer.md` §3.6) maps to a v1 mitigation that **holds under the C architecture**. C does not weaken any v1 mitigation; it strengthens two — post-commit cursor delivery is now via deterministic `LiveCursorState::requestTextCaretAtRow` resolving on `LiveBlockModel::rowsInserted` rather than `Qt.callLater × 10`; abandon-routing is a single `LiveCursorState::request` call rather than five concurrent retry-loops. (§2.)

3. The audit (`docs/2026-05-02-live-view-architectural-audit.md` §c L9) prescribed that holes should be a **phantom-rows model that composes with the parsed-rows model via a concatenating proxy** — so that `applyOps` never sees holes at all. v1 §2.4 and §3.5 used a row-interleaving formulation that re-imports the audit's complaint at its second pairwise reconciliation seam. **This post-mortem adopts the audit's prescription as a structural correction relative to v1.** The proxy is the load-bearing architectural change. (§4.)

4. Seven concerns the v1 spec did not pin — some because v1 predates C's per-row sequence-tagging surface, some because v1 left them open for dogfood — are enumerated with proposed resolutions in §3.

5. v1's commit and abandon trigger sets are sound under C with no changes. (§5.)

6. The closing v0 gap that the v1 spec acknowledged but did not codify — `QTest::keyClick`'s synchronous-between-event-loop-spins delivery model masks the async races that broke real keyboard input — is converted into a standing test pattern (§6).

The recommended next step is a design doc that ratifies §3's seven resolutions and specifies the proxy model's API. Spec amendments to the C-restoration spec (premise 6, §4.4, §5.4, §6.1 L6, §7.2, §11 R5/R6, §15) follow the design doc and require explicit user approval per the session brief's §3.6 spec-amendment protocol. R5.5 — packaged as its own phase, not folded into R5 — derives from the spec amendments.

---

## 1. Disentangling the failure: holes-as-concept vs surrounding architecture

The user-facing question behind the call for design is *"If we bring holes back, how do we keep them from breaking the way they did before?"* The honest answer requires separating two contributing axes that the v0 reversion treated as one: the holes-implementation choices and the surrounding live-view architecture. Each contributed independently; their compounding produced v0's dogfood failure.

### 1.1 The three v0 implementation choices

v0 made three discrete choices, each of which produced one or two of the named failure modes.

**Choice 1 — eager `\n\n` source edit at hole-creation.** The structural-key handler, on Enter at end-of-paragraph, did `applyLocalEdit("\n\n", currentBlockEnd)` *and* created the hole. The intent was to keep the parser ahead: by writing the paragraph break to source eagerly, the parser would already know about the new block by the time reification fired.

The cost was two distinct failure modes:

- **F1 (visual double-spacing).** The eagerly-written `\n\n` belonged to the previous block's byte range under the live-editing-design "delegate owns trailing whitespace" invariant. It rendered as a blank line below the previous paragraph. Stacked with the hole row's own visual line, the user saw two paragraph breaks for one Enter press.
- **F5 (source-state leak).** When the hole was abandoned silently (focus-out with empty buffer; Esc), the eagerly-written `\n\n` stayed in the source. Files accumulated invisible trailing newlines across save → quit → reopen.

Both follow from the same mistake: *the paragraph break is a view concern (the user sees a new paragraph) which v0 conflated with a source concern (the rope contains `\n\n`).* They are not the same. A faithful preedit doesn't put anything in source until commit; the visual paragraph break should come entirely from the hole row.

**Choice 2 — reify on first keystroke.** When the user typed the first character into the hole, v0 did three things in rapid succession: drop the hole row from the model; call `applyLocalEdit` again to write that character; await parse-back; route focus into the new real row when its delegate materialised.

The cost was the most damaging failure:

- **F2 (character scramble during fast typing).** Between the synchronous hole-drop and the asynchronous new-delegate-incubation (parse on a worker thread, ~30–100ms typical, plus ListView delegate creation latency), focus was in transit. Subsequent keystrokes during that window landed on the wrong delegate. User reproduction: "this is interesting" → "t is inhteresg. tinis". The 10-attempt `Qt.callLater` retry loop added in `ef0433b` only routed *focus* into the new delegate after it materialised; it did nothing about *intermediate keystrokes* delivered to whichever delegate held focus at each tick during the in-transit window.

This was a fundamental mistake about what reification is. v0 treated reification as an event that happens at the first keystroke. v1 treats reification as an event that happens at *quiescence* (idle, focus-out, save, explicit Enter), when no keystroke is in flight. The async window is the same length; what changed is whether the user is actively typing through it.

**Choice 3 — drop on any focus-out.** v0's abandon condition fired on any focus-out from the hole's delegate, including arrow-key navigation off the delegate's TextEdit. The intent was that "if the user moves away without typing, the hole should disappear."

The cost was two failures:

- **F3 (arrows destroy the hole).** Up/Down at TextEdit edges (top of TextEdit / bottom of TextEdit) yields focus to QML's focus chain, which v0 interpreted as abandon. Pressing Up to navigate to the previous paragraph killed the hole; pressing Down past EOB did the same. Normal navigation became destructive.
- **F4 (focus went nowhere after abandon).** v0's abandon path dropped the hole row but did not route focus to a neighbor. The user was left with no caret and no recovery path other than clicking. Invariant 16 in the v1 spec mentioned neighbor routing for the *collab-edit* hole-invalidation case but not for the *local-abandon* path; the local path was an oversight.

These three choices are not orthogonal in their visibility — F1, F2, and F5 are dramatic; F3 and F4 are small UX scratches — but they are orthogonal in their root cause. v1 inverts each (no eager edit; commit-at-quiescence-not-first-keystroke; arrows are normal nav with commit-or-abandon by buffer state).

### 1.2 The surrounding architecture's contribution

Even with v1's three inversions on the legacy architecture, the v0 dogfood would have surfaced a different set of races, and the legacy test discipline would have missed them.

Three architectural properties contributed:

**Six sources of truth.** The audit's diagnosis: legacy live-view had six overlapping authority claims for "what is the content of block N right now," reconciled pairwise. The hole's `bufferText` was the sixth. Reconciling against the other five produced cycle guards stacked on cycle guards: `m_applyingModelUpdate`, `m_applyingModelBuffer`, `m_composing`, `if (textEdit.activeFocus) return`, `m_applyingSessionSelection`, plus the structural-key handler's mid-block-Enter local-truncation hack. Adding a hole as a 6th authority meant each existing seam needed a hole-aware exception; v0's seven new code paths (`commitBlockHole` rowsInserted listener, `holeReified` / `holeCreated` / `holeDropped` connections, detach-around-`applyOps`, `reattachAndAbandon`, `commitBlockHole` rowsInserted leak…) were exactly that — exceptions that didn't compose.

**Pairwise reconciliation, not global.** Each cycle guard was scoped to one specific seam. None knew about the others. When mid-block Enter fired, the focused-skip rule (`if (textEdit.activeFocus) return` at `ParagraphDelegate.qml:264`) would otherwise leave the pre-Enter text visibly duplicated, so the structural-key handler did its own out-of-band visual mutation (local TextEdit truncation). That is a smell-of-a-smell: the focused-skip rule forced a structural-edit to do view-side work it shouldn't be doing. Holes added another seam, which would have been another exception, which would have produced another structural-handler-level workaround.

**Detach / reattach `applyOps` was the smoking gun.** `LiveListModelBinding::onParseUpdatedAt` had to detach the hole, run `applyOps`, then `reattach` or `reattachAndAbandon` based on whether the hole's anchor row was still in range (`LiveListModelBinding.cpp:175-194`). This is direct evidence that the hole abstraction did not compose with the parser-driven diff abstraction. The audit's L9 prescription — that holes should be a phantom-rows model that composes via a concatenating proxy *so that `applyOps` never has to know holes exist* — names exactly this failure.

The test suite did not catch any of these because tests bypassed the real timing of the seams. The unit test (`tst_view_qml_live_paragraph_hole.cpp`) drove `LiveProjectionLayer` and `LiveStructuralKeyHandler` directly, bypassing QQuickView entirely. The integration test (`tst_view_qml_live_paragraph_hole_integration.cpp`) used `QQuickView` + `QTest::keyClick`, but `QTest::keyClick` delivers events synchronously between event-loop spins; the very async-race window that broke F2 was masked by the test harness's timing model. Both tests passed while the demo app was visibly broken.

### 1.3 What this means for v1

v1 must inherit none of v0's three implementation choices. C already retired the surrounding architecture. The combination is required and sufficient.

| Axis | v0 | v1 | C |
|---|---|---|---|
| Source mutation at hole-create | Eager `\n\n` | None | (no opinion; consequence) |
| Reification timing | First keystroke | Quiescence (idle / focus-out / save / explicit-Enter) | (no opinion; consequence) |
| Drop-on-focus-out | Always | Only with empty buffer; arrows are normal nav | (no opinion; consequence) |
| Sources of truth | Six (rope, parse, model, delegate, predictions, hole+buffer) | Five (rope, parse, model, delegate, predictions+holes-as-projections) | One principle: sequence-tagged reconciliation; freshness rule per row |
| Cycle-guard count | 7+ | n/a | 3 (named, scoped, surviving) |
| `applyOps` ↔ holes | Detach / reattach | Phantom-rows model ⊕ concatenating proxy (audit L9) | Per-row freshness rule (parser-rows only) |
| Cursor delivery | `Qt.callLater × 10` retry | `LiveCursorState::requestTextCaretAtRow` on `rowsInserted` | Single deterministic primitive |
| Test timing model | `QTest::keyClick` synchronous | `QTest::qWait` + `QCoreApplication::processEvents` between keystrokes; standing harness | (no opinion; test discipline) |

The v1 column is what this post-mortem ratifies (with the audit's structural correction in row "applyOps ↔ holes"). The C column is what is in place at the tip of the branch post R5 Tasks 1–11. Both axes flipped means v0's compounding failure has no axis left to compound on.

---

## 2. Are v1's mitigations real under the C architecture?

Yes — and two of the five are strengthened by C, not merely preserved.

| # | v0 failure mode | v1 mitigation | Holds under C? | C effect |
|---|---|---|---|---|
| F1 | Visual double-spacing | No source edit at hole-creation; visual paragraph break is the hole row, not trailing whitespace from the previous block | Yes | Independent of C. Same correctness regardless of architecture. |
| F2 | Character scramble during fast typing | Stable preedit delegate during typing; commit fires at quiescence (idle / focus-out / save / explicit-Enter) when no keystroke is in flight | Yes — strengthened | Post-commit cursor delivery into the new real row is via `LiveCursorState::requestTextCaretAtRow(reifyRow, bufferText.length())` resolving on `LiveBlockModel::rowsInserted`. No `Qt.callLater × 10`. The intermediate-keystroke race that retry loops failed to address is structurally absent: there are no intermediate keystrokes during a quiescent commit by definition. |
| F3 | Arrows destroy the hole | Arrows are normal navigation; commit-or-abandon decided on what would be focus-out, by buffer-empty / buffer-non-empty | Yes | Independent of C. Hole-delegate's QML `Keys` handlers gate focus-yielding navigation explicitly; no special wiring needed. |
| F4 | Focus went nowhere after abandon | Explicit neighbor routing on abandon: `LiveCursorState::request(TextCaret(prev_block, prev_block_end))` | Yes — strengthened | C's single canonical `LiveCursorState` (§5.3 of the restoration spec) replaces the legacy's five concurrent retry-loops. One call. The "nearest live neighbor" lookup is on `LiveBlockModel`'s parser-pure rows, never confused by transient hole rows because the proxy abstraction (§4) means the hole layer adds and removes rows in a separate stream. |
| F5 | Source-state leak | No source edit until commit | Yes | Independent of C. Same correctness regardless. |

The mitigations were sound when v1 was specced. C does not change their soundness; in two cases (F2, F4) it makes the implementation cleaner because the primitives v1 needed are now available (`requestTextCaretAtRow`, single `LiveCursorState`, deterministic `rowsInserted`).

The remaining work is *integration* — wiring v1's hole layer into C's primitives — and *coverage of edges v1 didn't enumerate* (next section).

---

## 3. What v1 did not anticipate

The v1 spec was written on 2026-05-01 against the *legacy* `markoff-view-qml` architecture. It correctly identified its own out-of-scope items (full hole inventory, multi-cursor, configurable timeouts, hole serialization, heuristic create-on-click). It also left several open questions for dogfood (§9 of the spec). Some of those are sharper now under C; some are made answerable by C's primitives; some are entirely new because C's per-row sequence-tagging surface didn't exist when v1 was written.

Seven concerns surface under careful reading of the v1 spec against the C-architecture as it now stands. Each is given a proposed resolution; the design doc will ratify or revise.

### 3.1 Hole-vs-parser composition under concurrent parse-back

A hole sits at view-row N, anchored by `reifyOffset` (a CRDT byte position). The user types into it. While they type, an unrelated parse-back arrives that splits or merges a block elsewhere in the document. The hole's CRDT anchor remains valid (the byte stream did not change at `reifyOffset`), but its *view-row* index in the rendered list may shift because the parser produced a different block count or ordering.

v1 §2.4 says "`LiveBlockModel` rows = parser blocks ⊕ projection blocks, interleaved by anchor." Under that wording, `applyOps` against the merged stream has to know which rows are holes and skip them, or recompute their positions. This is the audit's L9 complaint at its second seam.

**Proposed resolution.** The proxy-model composition (§4 of this report). `LiveBlockModel` stays parser-pure; holes live in a sibling `LiveHoleLayer`; a `LiveProxyBlockModel` (or equivalent — the design doc names it) merges them by anchor and is the model the QML ListView binds to. `applyOps` runs against the inner parser-pure model, never sees holes. The proxy recomputes hole row positions on each parse arrival (cheap: holes are sparse).

### 3.2 IME composition vs idle commit

A CJK / IME user is composing a glyph in the hole's TextEdit. Composition takes time. The 250 ms idle timer fires mid-composition. v1 spec's commit triggers say "idle 250 ms after the most recent keystroke" — but during IME composition there are no Qt keystroke events, only `inputMethodEvent` deliveries. Without an explicit guard, the timer would not fire (it resets on `keyPress`, but composition is not `keyPress`); however, after composition commits the timer starts ticking from the last keystroke that *preceded* composition, which can be already-elapsed-by-now. Worse case: idle fires on a partial composition state that has no committed text, and `bufferText` reflects pre-composition state.

The C architecture's IME-composition-deferral pattern (one of the three surviving cycle guards, §4.5 of the C-spec) handles per-keystroke-edit deferral but does not extend to hole-idle-commit timing.

**Proposed resolution.** The hole's idle-commit timer is *paused* while `LiveEditBinding` reports composition active for the hole's delegate, and resumes (with full 250 ms) on `inputMethodComposingChanged(false)`. The composition-deferral pattern extends one role: it gates the idle timer, not just per-keystroke `applyLocalEdit` calls. The implementation is a single boolean check; the test is "compose a CJK glyph, wait 300 ms before commit, verify no commit fired during composition."

### 3.3 Undo (Ctrl-Z) while typing in a hole

v1 spec §6.case-1 says: "Hole exists, not yet committed (buffer empty or non-empty). Ctrl-Z drops the hole. **No CRDT undo is needed** because v1 doesn't write to source at hole creation."

This treats the entire hole's bufferText as a single atomic undo entry. A user who has typed 10 characters into the hole and presses Ctrl-Z loses all 10 in one stroke. In any other context (typing into a real paragraph), `UndoCoalescer`'s policy (R5 Task 3) coalesces consecutive printables in the same focus context into one undo entry but breaks coalescence on idle, mode switch, structural change, paste. A hole that has accumulated 10 characters across, say, three idle pauses would naturally produce three undo groups in a paragraph, not one.

The user's mental model is: Ctrl-Z undoes typing in groups; a hole is just a paragraph that hasn't been confirmed yet; Ctrl-Z should behave the same way.

**Proposed resolution.** While a hole is open, Ctrl-Z runs undo *within the buffer* using the same `UndoCoalescer` policy that operates on real text. Each undo group's pop reduces `bufferText`; when the buffer reaches empty, the hole drops on the next Ctrl-Z. Reification still produces one CRDT undo entry (the `applyLocalEdit("\n\n" + bufferText)` is atomic); pre-reification, the buffer's local undo stack is owned by the hole.

This is a v1-spec-divergence and the design doc must ratify it. Cost: a small undo stack per hole; benefit: undo behaves the same way inside and outside holes, which is the user's mental model.

### 3.4 Mid-buffer Enter inside a hole

User types "hello world" into a hole. Cursor at qtPos 5 (between "hello" and " world"). Presses Enter. v1 spec doesn't specify.

Three possible answers, in increasing surprise:

(a) Treat as commit + new hole below: the hole commits with `bufferText = "hello world"`, a new hole is created below for the next paragraph. The cursor's position within "hello world" is lost; the user wanted to split.
(b) Reject the Enter (no-op).
(c) Split: commit the prefix as a real paragraph (`applyLocalEdit("\n\n" + "hello\n", reifyOffset)` — or however the prefix-rendering wants to spell it), leave the suffix " world" in a fresh hole below, focus into the new hole at qtPos 0.

(c) matches the structural meaning of mid-block-Enter on a real paragraph (R5 Task 5 already implements that pattern). The hole behaves the same way.

**Proposed resolution.** (c). The structural-key handler dispatches mid-buffer Enter on a hole identically to mid-block-Enter on a real paragraph, with the prefix taking the commit path and the suffix opening a new hole. Tested against the same test discipline as the mid-block Enter case in R5.

### 3.5 Selection across a hole

The handoff doc (`2026-05-03-r5-empty-paragraph-gap.md` §7.3) flagged this open. Three options:

(a) Refuse: selection that would extend through a hole stops at the hole's boundary.
(b) Collapse on reification: selection across a hole is allowed during the hole's life; on reification, the selection becomes invalid and collapses to a caret.
(c) Include: hole is a row like any other; selection extends through it.

(a) is surprising: the hole *looks* like a row; refusing to select through it breaks visual consistency.
(b) introduces a transient state that's hard to communicate to the user.
(c) is the audit-prescribed shape: the proxy model treats hole rows as first-class for the QML ListView's selection apparatus.

**Proposed resolution.** (c). The proxy model exposes hole rows uniformly. `LiveSelectionView` projects across them. `serializeForCopy` for a hole row returns the bufferText (so copying a multi-row range that includes a hole produces the user-visible content the user typed, before reification). On reification, the selection's anchor stays at its CRDT position; the active end re-anchors against the now-real row. No collapse. The design doc specifies the anchor mechanics; the implementation reuses `LiveSelectionView`'s existing TextAnchor translation under the new layer.

### 3.6 Save with multiple pending holes

v1 §6 says "save flushes the preedit buffer first, then writes the rope." Singular "the preedit buffer." With multiple pending holes, ordering matters because each commit increments byte offsets after its `reifyOffset`.

In practice, multiple pending holes are unlikely: focus-out commit fires when the user navigates away, so leaving hole A open and creating hole B requires a focus-out path that *doesn't* commit. That happens only if hole A's buffer is empty — in which case A abandons cleanly on focus-out. So a "multiple pending holes at save time" state arises only if Ctrl-S is invoked from a non-focus-stealing surface (a system menu shortcut, a parent-window button) while a hole is open *and* another hole was opened earlier without focus-out.

**Proposed resolution.** Define order even though it is unlikely to be exercised: commit holes in ascending `reifyOffset` order, recomputing offsets between commits (each commit shifts subsequent offsets by `2 + bufferText.length()`). The implementation is iterative; the test is contrived but cheap. Document the unlikelihood.

### 3.7 Test discipline against v0-style races

v0's tests passed while real users saw scramble. The deferred redesign brief (`2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md`) flagged this exactly: *"Why will my stress-typing test see the v0 race that the v0 stress test missed?"*

The brief's answer was right but never codified: realistic-keyboard timing requires `QTest::qWait(N)` and `QCoreApplication::processEvents()` between `keyClick` calls, mimicking the millisecond-scale gaps between real key-down events. Without this, the event loop's async paths never get a chance to run between keystrokes, and the very races we are trying to catch are masked.

For v1, this is not optional. The test discipline must be a **standing pattern** for every async-UX test in R5.5, R6, R7, R8, R10. Otherwise R6+ keep adding new async races and the test suite keeps lying about coverage.

**Proposed resolution.** Pin a `LiveRenderRealisticInputHarness` (or the design doc's chosen name) as a test utility under `libs/markoff-live/tests/`. API: `keyClickRealistic(QQuickWindow *, Qt::Key, modifiers, gapMs = 30)` which `keyClick`s, then `qWait(gapMs)`, then `processEvents()`. Every async-UX test uses this rather than raw `QTest::keyClick`. Pre-existing fast tests stay on raw `keyClick` if they don't exercise async paths (rationale: not every test needs the gap; the harness is for stress-typing and async-race assertions).

The R5.5 plan's first test task is the harness, not a hole feature. If that test pattern doesn't fail for the v0 race scenario *before any v1 feature exists*, the harness is too lenient and we fix it before building anything. (The plan's phrasing per the brief: *"the stress-typing test is load-bearing; if it doesn't fail before T20-T22 are wired, your test is too lenient."*)

---

## 4. The structural correction — phantom-rows + concatenating proxy

This is the load-bearing architectural change relative to v1.

### 4.1 What the audit said

`docs/2026-05-02-live-view-architectural-audit.md` §c, layer L9:

> **L9 — pre-parser intent ("holes")** […] The right shape, in retrospect: a separate phantom-rows model that composes with the parsed-rows model via a concatenating proxy, so `applyOps` never has to know holes exist (the proxy handles row-index translation). The current "detach / re-run / reattach-or-abandon" dance is direct evidence that the hole was bolted into the parsed-rows model rather than composed alongside it.

### 4.2 What v1 said

v1 spec §2.4:

> **Where the layer sits**
> ```
> MarkoffDocument (CRDT, authoritative source)
>     │
>     │ parseUpdated(parsed, parseSequence, blockAnchors)
>     ▼
> LiveProjectionLayer  ◄────── user input intents (Enter, etc.)
>     │ (parsed blocks ⊕ projections)
>     ▼
> LiveBlockModel  (QAbstractListModel surfaced to QML)
> ```

The bullet "`LiveBlockModel` rows = parser blocks ⊕ projection blocks, interleaved by anchor" makes the model itself the merge point. Under that wording, the model's `applyOps` either skips holes (the v0 detach/reattach dance) or has to reason about hole-vs-parser-row ordering. Both options re-import the audit's complaint.

The v1 spec was written one day before the audit; the discrepancy is chronological, not philosophical.

### 4.3 What this post-mortem adopts

Two classes, two responsibilities:

- `LiveBlockModel` (existing, **parser-pure**) — `QAbstractListModel` over `BlockRecord`, populated by parser-driven `applyOps`. Holds parsed blocks only. Per-row `lastEditEditSequence` continues to drive freshness. Holes are invisible to this class.
- `LiveHoleLayer` (new, **phantom-rows producer**) — owns the set of `BlockHole` items, each with `reifyOffset`, `bufferText`, lifecycle state. Emits change signals when a hole is created, its buffer changes, it commits, or it abandons.
- `LiveProxyBlockModel` (new, **concatenating proxy**) — the `QAbstractListModel` the QML ListView binds to. Composes `LiveBlockModel`'s rows + `LiveHoleLayer`'s rows in anchor order. Maps inner row indices to proxy row indices and back. `parser-row N` → `proxy-row N + (count of holes whose anchor < this row's anchor)`. `LiveBlockModel::rowsInserted` propagates to `LiveProxyBlockModel::rowsInserted` after the index translation. `LiveHoleLayer::holeInserted` does the same.

The proxy is what the QML ListView binds to. `LiveCursorState::requestTextCaretAtRow(row, qtPos)` operates in proxy-row coordinates. `LiveSelectionView` projects against the proxy. `LiveStructuralKeyHandler` dispatches against the proxy's row count.

`applyOps` runs against the inner `LiveBlockModel`. It sees only parser rows; it does not detach, reattach, or know about holes. The freshness rule (`row.lastEditEditSequence ≤ parseInputEditSequence`) applies per parser row only. Holes have no `lastEditEditSequence` because their content is not in the CRDT.

### 4.4 Why this is the load-bearing correction

Three failures of v0, recall, traced back to the bolt-on:

- The `commitBlockHole` rowsInserted listener leak (legacy `LiveProjectionLayer.cpp:155-177`) — a hole-aware path inside the parser-driven applyOps cycle.
- The detach / reattach dance in `LiveListModelBinding::onParseUpdatedAt` (`LiveListModelBinding.cpp:175-194`) — applyOps couldn't run with a hole present.
- The five concurrent focus-routing retry loops in `LiveView.qml` — every async path had its own retry because no single primitive owned hole-vs-parser-row position.

Under the proxy model, all three disappear:

- No commit-listener-on-applyOps: commit drops the hole synchronously from the proxy via `LiveHoleLayer::commitBlockHole`; the parse-back's Insert appears in `LiveBlockModel`'s row stream and is propagated through the proxy without hole-awareness.
- No detach/reattach: applyOps runs against the parser-pure inner model.
- No five retry-loops: focus delivery is one primitive, `LiveCursorState::requestTextCaretAtRow`, operating in proxy coordinates.

This is what makes §3.1 (concurrent parse-back during hole life) tractable as a one-line update inside the proxy rather than as a dance across `LiveListModelBinding`.

### 4.5 What the design doc must specify

This post-mortem does not pin the proxy's API. It pins the *shape*: parser-pure inner; phantom-rows producer; concatenating proxy. The design doc resolves:

- The exact anchor-comparison rule for ordering hole rows among parser rows.
- The signals and slots between `LiveBlockModel`, `LiveHoleLayer`, and `LiveProxyBlockModel`.
- How `LiveCursorState::requestTextCaretAtRow` resolves when the target row is a hole vs a parser row.
- How `LiveSelectionView`'s `Session::primarySelection` ↔ Shape-1 projection handles hole rows in cross-block ranges.
- How `LiveStructuralKeyHandler` dispatches when the focused block is a hole (paragraph kind, but a hole-row variant — does the descriptor change, or do we add a "hole" qualifier?).
- The proxy's behaviour during `LiveBlockModel::modelReset` / `beginResetModel` (Reset path on `MarkoffDocument::resetContent`): are pending holes preserved across a reset? Probably not — reset implies "load a different file" and dropping holes is correct.

These are the substantive integration choices. They are out of scope for this report; they are the design doc's deliverable.

---

## 5. v1 commit and abandon triggers — re-litigated

The handoff doc's §7.2 asked: *"Are the v1 commit triggers (idle 250 ms, focus-out, save, explicit Enter) the right set, or are some redundant / missing? Is the v1 abandon trigger set (Esc, focus-out empty, Backspace at qtPos 0 empty) right? Particularly: is 'Backspace at qtPos 0 of empty hole' the right intent — are users likely to press Backspace to mean 'cancel'?"*

### 5.1 Commit triggers — confirmed

| Trigger | Purpose | Verdict |
|---|---|---|
| Idle 250 ms after last keystroke | Bounded time to dirty-on-disk; matches typical typing-burst pause. | Confirmed. Subject to revisit during dogfood (v1 §9 already flagged this open). |
| Focus-out with non-empty buffer | User is moving their attention; preserve their work to source before yielding focus. | Confirmed. |
| Save (Ctrl-S) | Saved file always equals what the user sees. | Confirmed. With the §3.6 ordering rule for the multi-hole edge case. |
| Explicit Enter on non-empty buffer | User signalled "this paragraph is done; I want a new one below." Mirrors paragraph-end Enter on real paragraphs. | Confirmed, with §3.4's mid-buffer Enter clarification — Enter mid-buffer splits, end-of-buffer commits-then-creates-next-hole. |

No trigger is redundant; none is missing. The handoff's question was sound to ask, and the answer is no change.

### 5.2 Abandon triggers — confirmed

| Trigger | Purpose | Verdict |
|---|---|---|
| Esc | Universal "cancel" gesture. | Confirmed. |
| Focus-out with empty buffer | User changed their mind without typing. No work to preserve. | Confirmed. |
| Backspace at qtPos 0 with empty buffer | "Delete the paragraph I just created." Symmetric with Backspace at start of an empty real paragraph (which merges with previous). | Confirmed — see §5.3. |

### 5.3 Backspace-at-qtPos-0-empty as abandon — explicitly confirmed

The handoff doc raised this for re-litigation. The reasoning:

In a real editor, Backspace at qtPos 0 of an empty paragraph merges that paragraph with the previous block. The visible effect: the empty paragraph disappears; cursor lands at the end of the previous block. From the user's perspective, "the empty paragraph went away because I pressed Backspace at its start." 

For an empty hole, there is no source paragraph to merge — the hole is the entirety of the paragraph's existence. The visible effect should be the same as the real-paragraph case: hole disappears; cursor lands at end of previous block. Mechanically, this is "abandon and route focus to previous block end." The user's mental model is preserved without them needing to know whether they're in a hole or a real paragraph.

The alternative — Backspace at qtPos 0 of empty hole *not* abandoning — would mean Backspace does nothing in an empty hole, which is surprising. If the user's intent were "make this hole non-empty by deleting," there is no character to delete; if the intent were "go back," Backspace not abandoning leaves the user stuck in a hole they may not realise is preventing previous-block edits.

Backspace-at-qtPos-0-empty as abandon is the right intent. Confirmed.

(For the symmetric Delete-at-qtPos-end-empty case: less common, but should behave the same way under the same rationale — abandon, route focus to next block start. Add to the abandon trigger set.)

### 5.4 One small addition

§3.4 (mid-buffer Enter) added a derived trigger: explicit Enter at a non-end-of-buffer position commits the prefix and creates a fresh hole for the suffix. This is a refinement of "explicit Enter on non-empty buffer," not a new trigger. The design doc names this case explicitly; the trigger set is unchanged in cardinality.

---

## 6. Test discipline — closing the v0 gap

§3.7 sketched the proposal. Repeating it as a section because it is a standing requirement, not a one-time fix.

### 6.1 The v0 test failure mode

Two test files (`tst_view_qml_live_paragraph_hole.cpp`, `tst_view_qml_live_paragraph_hole_integration.cpp`) passed while the user-facing demo was visibly broken on F2 (character scramble). The unit test bypassed QQuickView entirely; its irrelevance is unsurprising. The integration test used the right harness (QQuickView) and the wrong timing model (`QTest::keyClick`'s synchronous-between-event-loop-spins delivery). Real keyboard events arrive on real-time intervals; `QTest::keyClick` does not. The async-race window that broke F2 is *invisible* to the synchronous test path.

### 6.2 The fix as a standing pattern

`QTest::qWait(N) + QCoreApplication::processEvents()` between `QTest::keyClick` calls is the minimum bar. The N value should mimic real-keyboard-spacing — 30–50 ms for sustained typing, with longer pauses (200–500 ms) for "burst then pause" patterns. Both shapes appear in dogfood; both must be tested.

R5.5's plan must:

1. Land the harness as the *first* task.
2. Write a standing test that reproduces v0's F2 against a *deliberately broken* hole implementation (synthetic — write a stub that destroys the delegate on first keystroke and watches what happens). The test must FAIL — proving the harness sees the race. Then delete the synthetic-broken stub before any real v1 feature lands.
3. Every subsequent async-UX test in R5.5+ uses the harness, not raw `QTest::keyClick`.

If the harness can't see F2 reproduced in a synthetic stub, the harness is broken and we fix it before writing v1 code. This is the brief's gate ("Do not start until you can answer this question") converted into a concrete test.

### 6.3 What this does not promise

The harness reduces masking, not eliminates it. There are race classes (multi-thread interleavings; specific GPU-paint timing) that no event-loop-driven test can reach. For those, dogfood remains the verification gate. Per spec §10.3 and the session brief's Anti-Patterns list ("Do not skip dogfood feedback"), the harness is a *supplement* to dogfood, not a replacement.

---

## 7. What this report does NOT do

Three things are deliberately out of scope, in priority order:

1. **No spec amendments.** Premise 6 ("Notion-style Enter; holes deleted") is wrong; §4.4's cycle-guards-retired table needs the `commitBlockHole` and detach/reattach rows restored with v1-pattern annotations; §5.4's structural-keys section needs paragraph-EOB-Enter to read "produces a hole" not "inserts \n\n"; §6.1 L6 needs `LiveHoleLayer` introduced as a sibling to `LiveSpeculationLayer`; §7.2's data-flow needs the hole-create / local-typing / commit-on-trigger flow added; §11 R5/R6 needs an R5.5 phase added or R5 amended with hole tasks. **None of this is edited yet.** Edits require explicit user approval per the session brief §3.6. The amendment proposal is the design doc's deliverable, not this post-mortem's.

2. **No design doc.** §3 names seven concerns with proposed resolutions; §4.5 lists six things the design doc must specify (proxy API, signals, cursor resolution, selection projection, structural-key dispatch, reset behaviour). Neither §3 nor §4.5 is the design doc itself. The design doc resolves the proposed resolutions into committed API shapes and pins behaviour. It is the next document in this sequence.

3. **No R5.5 plan.** The plan is `superpowers:writing-plans`'s deliverable, derived from the spec amendments after they land. The estimated task count is 10–14, packaged as R5.5 (rather than folded into R5) because it adds a new layer and a proxy model class — not "structural-keys" work in scope.

The sequence is: this post-mortem → design doc (with user approval gate) → spec amendments (with user approval gate) → R5.5 plan (with self-review gate per writing-plans skill) → implementation (subagent-driven, per session brief §3.1).

---

## 8. Recommended next steps

In order:

1. **User reviews this post-mortem.** Request: read in full. Push back on §3 (the seven proposed resolutions) and §4 (the proxy structure) — these are the load-bearing changes relative to v1 and any disagreement here changes the design.

2. **Design doc.** Topic: "v2 holes — paragraph-only, proxy-model composition, integrated with the C architecture." Path: `docs/specs/2026-05-03-v2-holes-design.md` (matching the date convention; not under `docs/superpowers/specs/` because the project's own spec convention sits at `docs/specs/`). Scope: ratify §3's seven resolutions; specify the `LiveHoleLayer` and `LiveProxyBlockModel` APIs; specify the integration with `LiveCursorState`, `LiveSelectionView`, `LiveStructuralKeyHandler`, the freshness rule, and `UndoCoalescer`; specify the realistic-input test harness API. Length: ~400–600 lines, citing this post-mortem and the v1 spec where shapes are unchanged.

3. **Spec amendments** to `docs/specs/2026-05-02-live-render-restoration-design.md`. Per session brief §3.6: identify the contradictions concretely; propose edits in `docs/restoration-status.md`'s spec-amendment log (entry A1 already exists as a placeholder; replace with concrete drafts); surface to the user; await explicit approval; apply inline; commit `docs(spec): R5/R5.5 amendment — v2 holes`. The amendments touch premise 6, §4.4, §5.4, §6.1 L6, §7.2, §11 R5/R6, and add an entry to §15 if any open questions surface during the design doc.

4. **R5.5 plan** via `superpowers:writing-plans`. Path: `docs/plans/2026-05-03-live-render-r5-5-holes.md`. Estimated 10–14 tasks: realistic-input harness (T1); `BlockHole` value type + `LiveHoleLayer` scaffold (T2); `LiveProxyBlockModel` (T3); structural-key handler hole-creation hook for paragraph EOB-Enter and start-of-paragraph Enter (T4); buffer-mirror from delegate to layer (T5); idle-commit timer with IME guard (T6); focus-out commit / abandon paths with neighbor routing (T7); Esc + Backspace-at-0-empty + Delete-at-end-empty abandon (T8); reification path with `applyLocalEdit` + cursor delivery via `requestTextCaretAtRow` (T9); save-flush integration (T10); hole-aware undo policy in `UndoCoalescer` (T11); selection-across-hole projection (T12); QML `isHole` plumbing on `ParagraphDelegate` (T13); R5.5 dogfood gate (T14).

5. **Implementation** subagent-driven per session brief §3.1, with each task's TDD cycle running the realistic-input harness for any async-UX assertion.

6. **Status updates.** `docs/restoration-status.md` gets a recent-changes entry per commit, an updated phase board (R5 stays `in-progress (paused)` through the design+spec phase; transitions to `complete` with the documented EOB-Enter limitation when R5 Tasks 12–17 land; R5.5 is added to the board as `pending` with this plan link). The TL;DR pointer redirects through the design doc → spec amendments → R5.5 plan as each lands.

---

## 9. Reference index

The documents this post-mortem rests on, in dependency order:

1. **The call for design** — `docs/handoff/2026-05-03-r5-empty-paragraph-gap.md` (the document this report responds to).
2. **The v1 hole design** — `docs/specs/2026-05-01-live-projection-layer.md` §3.1–§3.5 (the design carried forward by this report) and §3.6 (the v0 failure-mode forensics).
3. **The deferred v1 redesign brief** — `docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md` (prior art for what a v1 implementation would have looked like; the test-discipline gate originates here).
4. **The C-architecture restoration spec** — `docs/specs/2026-05-02-live-render-restoration-design.md` (the spec to be amended).
5. **The audit** — `docs/2026-05-02-live-view-architectural-audit.md` (the L9 prescription this report adopts as the structural correction).
6. **The R5 plan** — `docs/plans/2026-05-02-live-render-r5-structural-keys.md` (Tasks 1–11 executed; 12–18 paused).
7. **The session brief / working protocol** — `docs/handoff/2026-05-02-restoration-session-brief.md` §3.6 (the spec-amendment protocol governing the next steps).
8. **The status board** — `docs/restoration-status.md` (TL;DR points at the call for design; spec-amendment log entry A1 placeholder will be filled by the amendments derived from this post-mortem).

---

*End of post-mortem.*
