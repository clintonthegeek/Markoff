# Live Projection Layer — design

**Date:** 2026-05-01
**Branch:** `exploration/new-foundation`
**Phase:** Phase-2 follow-on inside `markoff-view-qml`. Builds on the live-editing design (`docs/specs/2026-04-30-live-editing-design.md`) and the walking skeleton (`docs/specs/2026-04-29-live-render-design.md`).
**Status:** **partially shipped — Stages 1-3 (the layer infrastructure + refactors) implemented; Stage 4 (empty-paragraph hole) reverted, redesigned, and deferred to a fresh session.** Plan: [`docs/plans/2026-05-01-live-projection-layer.md`](../plans/2026-05-01-live-projection-layer.md). Handoff brief: [`docs/handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md`](../handoff/2026-05-01-projection-layer-stage4-redesign-SESSION-BRIEF.md). Stages 1-3 landed in commits `19094bb` (skeleton) → `ef62f57` (fence refactor) → `3e1c437` (inline highlighter refactor) → `b07b07c` (`InlinePrediction` `byte→char` rename). Stage 4 v0 (`a463bca` + `ef0433b` + `030d581`) was reverted at `cfbc30f` after dogfood surfaced five distinct failure modes (see §3.6 below).
**Depends on:** `markoff-foundation` BlockAnchor/TextAnchor APIs (already shipped); `LiveBlockModel`, `LiveListModelBinding`, `InlineFormatHighlighter`, `LiveSpeculativeFenceController` (already shipped).

## 0. TL;DR

The editing model is a *superset* of the source model. The projection downward to source is *lossy by design*. Today that gap is filled by two ad-hoc speculations (`InlineFormatHighlighter` for open inline delimiters, `LiveSpeculativeFenceController` for unclosed fences) plus a *missing third citizen* — **holes** — for user intent that the markdown source language cannot represent yet.

This spec promotes "view-state ahead of source" to a first-class architectural layer: the `LiveProjectionLayer`. It absorbs the two existing speculations (Stages 1-3, **shipped**) and proposes holes as siblings under one shared protocol (Stage 4, **deferred** after a failed v0 attempt; see §3.6). The motivating user bug — pressing Enter at end of a paragraph appears to do nothing in the live editor — remains open at the tip of this branch.

**Shipped scope (Stages 1-3):** the projection-layer abstraction; the refactors of `InlineFormatHighlighter` and `LiveSpeculativeFenceController` behind it with no behavior change. The architecture is named, and predictions cohabit cleanly.

**Deferred scope (Stage 4 v1):** the empty-paragraph hole, redesigned around an IME-preedit pattern after the v0 reify-on-first-keystroke design failed dogfood (§3.6). The redesign is in §3.1-§3.5.

**Explicitly out (all versions):** the rest of the hole inventory (list items, checklists, code-fence interior, blockquote, callout, table cells, links, wikilinks, footnotes, math); collaborative-edit hole invalidation beyond the simple "anchor deleted → drop" case; configurable abandonment timeouts.

---

## 1. Background and motivation

### 1.1. The bug that surfaced this

User report (2026-05-01): in the live demo (`markoff-view-qml-app --live`), pressing Enter does nothing visible. Investigation:

| Doc before | Cursor | Doc after Enter | `listView.count` | Behavior |
|---|---|---|---|---|
| `Hello` | end (5) | `Hello\n\n` | **1** | "Enter does nothing" |
| `HelloWorld` | mid (5) | `Hello\n\nWorld` | 2 | Splits correctly |

`LiveStructuralKeyHandler::tryHandle` does the right thing at the byte layer — it inserts `\n\n` at `currentBlockEnd`. Tree-sitter then reports one paragraph followed by trailing whitespace, because markdown's source language has no glyph for "empty paragraph." `LiveBlockModel` faithfully renders one block. The user's *intent* — "I am now in a new paragraph to type into" — has no destination at the bottom of the stack.

The mid-block case works only incidentally: both halves of the split have content, so two real blocks materialise.

### 1.2. The architectural gap

Walking up from this bug, the gap is bigger than one keystroke:

- The **propagation path** from user input to authoritative source is well-defined (delegate → `LiveEditBinding` → `applyLocalEdit` → CRDT → reparse → model → view), cycle-guarded, and tested. That part is fine.
- What is missing is a formal home for **user-introduced state that does not yet have a source representation but eventually will**. Markdown's source cannot express the empty paragraph the user is reaching for; the architecture has to hold that intent *somewhere* until it can be reified by subsequent input.

The two existing speculations (`InlineFormatHighlighter`, `LiveSpeculativeFenceController`) solve a *different* problem along the same axis: the parser is slower than the keystroke, so the view runs ahead and reconciles when truth catches up. Both speculations rest on bytes that are *already* in the source — they only differ in *when* the parser will agree. The Enter-empty-paragraph case is the dual: bytes are in source, but no parser confirmation will ever arrive because the source legitimately lacks the block.

### 1.3. Established paradigm

This is well-trodden ground from at least three directions:

- **Optimistic UI / client-side prediction** (Bernier-style game networking; React 19's `useOptimistic`; every collaborative editor's local-prediction-with-server-reconciliation). Latency-bridging.
- **IME preedit / composition buffer** (CJK input methods; `compositionstart`/`compositionend` on the web; `inputMethodEvent` in Qt — already used by `LiveEditBinding`). Visible-but-tentative overlay on the document.
- **Holes** in projectional / structural editors (JetBrains MPS, Lamdu, Hazel) and proof assistants (Agda, Idris, Lean). First-class typed placeholders for "intent will fill this slot."

Optimistic UI handles the "projection runs ahead in time" axis. Holes handle the "editing surface has structure the source can't express" axis. Mature editors implement both, named separately. We have the first; we need the second; both want to live behind a single named layer because their reconciliation protocols are structurally identical.

---

## 2. Concept: the Live Projection Layer

### 2.1. The defining sentence

> The editing model is a superset of the source model. The projection downward is lossy by design. The `LiveProjectionLayer` owns the difference.

### 2.2. Two item kinds, one protocol

The layer holds two kinds of items:

- **Predictions** (latency-bridging). Bytes are present in source; view runs ahead of parser. Examples: open `**` styled bold before close; ```` ``` ```` flips paragraph kind to code_block before the close fence arrives. Confirmed when parser truth aligns; dropped when it diverges.
- **Holes** (intent-holding). Source has no representation yet; view holds the intent. Examples: empty paragraph after Enter at end-of-block; empty list item after Enter inside a list; empty code-fence interior. Reified by the user's next character (which becomes a real source edit) or abandoned (focus departs, undo, idle threshold).

Both kinds share a common shape:

```
ProjectionItem {
    Anchor  origin;            // where in source this projects from
    Kind    kind;               // semantic type (which block/inline kind)
    Trigger reificationRule;    // what input commits to source
    Trigger abandonmentRule;    // what input drops the projection
    bool    isHole;             // false = prediction, true = hole
}
```

Predictions reconcile against parser output; holes reconcile against subsequent user input. The reconciliation engine doesn't care which is which — it just runs the rules each item carries.

### 2.3. Where the layer sits

```
MarkoffDocument (CRDT, authoritative source)
    │
    │ parseUpdated(parsed, parseSequence, blockAnchors)
    ▼
LiveProjectionLayer  ◄────── user input intents (Enter, etc.)
    │ (parsed blocks ⊕ projections)
    ▼
LiveBlockModel  (QAbstractListModel surfaced to QML)
    │
    ▼
ListView delegates
```

The layer consumes parser output *and* user-input intent signals; it emits a unified block stream (real blocks + projection rows) into `LiveBlockModel`. Delegates don't know the difference — a hole-rendering ParagraphDelegate looks identical to a real one, except its anchor resolves to "no source position yet."

### 2.4. What "owns the difference" means concretely

- The CRDT rope is canonical source. It only ever changes through `applyLocalEdit`.
- `LiveBlockModel` rows = parser blocks ⊕ projection blocks, interleaved by anchor.
- Delegates bind to model rows the same way as before. The shape of `LiveBlockModel`'s public roles does not change.
- Inline-level projections (the existing `InlineFormatHighlighter` work) sit *inside* a row, painting `QTextCharFormat` ranges that the parser hasn't confirmed.

---

## 3. Holes: the new citizen

### 3.1. The problem holes solve

When the user presses Enter at end of `Hello`, they want to be in a new paragraph. Source becomes `Hello\n\n`. Parser reports one block. View shows one row. Without holes, the user's cursor remains in `Hello` and the next keystroke extends `Hello`. With holes, the layer inserts a transient empty-paragraph row after `Hello`'s row, focus is routed into it, and the next keystroke reifies it into a real block.

### 3.2. Hole lifecycle (v1 — IME-preedit pattern)

The v1 design treats a hole as a **preedit buffer** — a real local-typing surface that does not write to source until commit. This mirrors how IMEs handle multi-keystroke input that doesn't yet have a committed representation in the document.

```
   user input intent (e.g. Enter at EOB)
              │
              ▼
   ┌──── layer creates BlockHole ────┐
   │   kind         = "paragraph"    │
   │   reifyOffset  = currentBlockEnd│  (NO source edit yet)
   │   bufferText   = ""             │
   └──────────────┬──────────────────┘
                  │
   ┌──────────────┴──────────────────┐
   │ keystroke into hole's delegate  │
   │   - TextEdit accepts locally    │
   │   - bufferText updated          │  (still no source edit)
   │   - idle timer restarts (250ms) │
   └──────────────┬──────────────────┘
                  │
   ┌──────────────┴──────────────────┐
   │ commit triggers (any of):       │
   │   - idle 250ms after keystroke  │
   │   - focus leaves delegate AND   │
   │     bufferText is non-empty     │
   │   - explicit Enter / Tab / Esc  │  (decided by spec; see §3.3)
   │   - save (Ctrl+S) flushes       │
   │ → applyLocalEdit("\n\n" + buf)  │
   │   at reifyOffset                │
   │ → drop hole                     │
   │ → focus routes to new block at  │
   │   end-of-buffer offset          │
   └─────────────────────────────────┘
                  │
   ┌──────────────┴──────────────────┐
   │ abandon triggers (any of):      │
   │   - focus leaves AND buffer is  │
   │     empty                       │
   │   - Esc                         │
   │   - Backspace at qtPos 0 with   │
   │     empty buffer                │
   │ → drop hole                     │
   │ → no source mutation            │
   │ → focus routes to nearest live  │
   │   neighbor (previous block end) │
   └─────────────────────────────────┘
```

The defining property of v1: **source is never written to until commit.** The hole is invisible to the parser, the dirty flag, and save. Navigation away from an *empty* hole leaves no trace.

### 3.3. The v1 hole: empty paragraph after Enter (deferred implementation)

**Trigger.** `LiveStructuralKeyHandler` detects Enter at `qtPos == blockText.length()` AND the resulting block-after position would have no parser-visible content. Layer creates `BlockHole(kind="paragraph", reifyOffset=currentBlockEnd, bufferText="")`. **No source edit is performed.** The trailing whitespace problem from v0 is gone — source remains exactly as it was before Enter was pressed.

**Local typing.** The hole's delegate is a regular `ParagraphDelegate` with `isHole === true`. Its `TextEdit` accepts keystrokes through Qt's normal text-input path. Each keystroke updates `bufferText` in the layer (the binding mirrors the QTextDocument's contents into the hole's buffer). The TextEdit shows the buffer locally; visually identical to typing into a real paragraph.

**Commit triggers** (when the hole has non-empty `bufferText`):

- **Idle debounce:** 250ms after the most recent keystroke. Default v1 trigger; ensures the source is at most 250ms behind the user's typing without forcing a per-keystroke parse round-trip.
- **Focus-out with content:** the user clicks elsewhere, presses arrow keys to navigate, etc. Commit before yielding focus.
- **Save (Ctrl+S):** flush the buffer first, then save. Guarantees the saved file contains what the user typed.
- **Explicit Enter** while the buffer is non-empty: opens a *new* hole below the just-committed paragraph. (User reaction: "Enter twice creates two new paragraphs to type in" — natural.)

**Commit semantics.** One `MarkoffEdit` inserts `"\n\n" + bufferText` at `reifyOffset` in a single CRDT operation. The layer drops the hole synchronously (model-row count drops by one). `applyLocalEdit` returns synchronously; the parse runs async; the parse's `applyOps` reports an Insert at the correct row, and the new real delegate materialises with the typed text already in it. **Critical for stress-typing:** the user has been typing into a stable delegate the whole time; the destroy-and-recreate happens *once*, at commit, not per keystroke.

**Abandon triggers** (when `bufferText` is empty):

- **Esc:** drop the hole, route focus to the previous block's end.
- **Focus-out with empty buffer:** drop the hole, no source mutation. Equivalent to the user changing their mind.
- **Backspace at qtPos 0:** drop the hole, route focus to the previous block's end.
- **Arrow keys at edges:** v0 made arrow-keys destroy the hole. **v1 reverses this**: arrow keys are normal navigation; if they would leave the delegate (e.g. Up at qtPos 0 of an empty hole), commit-or-abandon based on `bufferText` non-empty/empty.

**Focus routing on commit.** The layer emits `holeReified(viewRow, qtPos)` *after* `applyLocalEdit` returns AND the next `parseUpdated` lands. The view binds focus only when the real delegate is materialised, not via async polling. This requires either (a) the binding subscribes to `parseUpdated` to detect when the new row is ready and emits a delayed signal, or (b) the layer queues `holeReified` on a `parseUpdated`-once connection. v0's bug — focus race during async delegate materialisation — is structurally impossible in v1 because we don't try to route focus until the new delegate exists.

**Edge case — Enter on an already-pending hole.** User presses Enter, types nothing, presses Enter again. v1: idle commits "" (no-op — nothing to insert), and the second Enter creates… well, this case is degenerate. Spec proposal: second Enter in an empty hole is a no-op (the hole stays). Validate during dogfood.

### 3.4. The full hole inventory (deferred)

For Obsidian-flavored markdown, the eventual hole catalog is:

1. Empty paragraph (Enter at EOB / blank-line creation) — **v0**.
2. Empty list item (Enter inside list, including ordered/unordered/checklist).
3. Empty heading (`# ` + Enter before content).
4. Empty code-fence interior (Enter inside open fence).
5. Empty blockquote (`> ` with no content).
6. Empty callout (`> [!note] ` Obsidian-specific).
7. Empty table cell / new row.
8. Empty link / wikilink target (`[](`, `[[`).
9. Empty footnote definition.
10. Empty math block (`$$ $$`).

These all share the same archetype-shape (trigger keystroke → kind → preedit/local-typing → commit/abandon rules). The v1 hole #1 ships first; the rest land in follow-on plans once the abstraction has soaked.

### 3.5. Architectural changes required for v1 (vs. v0)

Stages 1-3 (the layer infrastructure for predictions) need no changes. The Stage 4 v1 work needs:

1. **`BlockHole` value type:** add `QString bufferText` (the local preedit buffer). Drop `pairedSourceEditByteCount` (no paired source edit anymore).
2. **`LiveProjectionLayer`:**
   - `createBlockHole(...)` no longer pairs with a source edit — pure view-state addition.
   - New `appendToBlockHoleBuffer(holeId, text)` and `setBlockHoleBuffer(holeId, text)` for the binding to mirror local TextEdit changes into the buffer.
   - New `commitBlockHole(holeId)` — builds `MarkoffEdit("\n\n" + bufferText)`, calls `applyLocalEdit`, schedules `holeReified` to fire on the next `parseUpdated`.
   - `dropBlockHole` is unchanged for the abandon path.
3. **`LiveBlockModel` row interleaving:** unchanged — still serves a hole row at `viewRow = afterParsedRow + 1`.
4. **`ParagraphDelegate.qml`:**
   - Reify-on-first-keystroke logic removed. Hole rows are normal editable delegates.
   - Add an `onTextChanged` (or `onContentsChange` from the binding) that mirrors the TextEdit's text into `layer.setBlockHoleBuffer`.
   - Add commit triggers: `onActiveFocusChanged` with non-empty buffer → commit; idle timer (250ms restart on edit) → commit; explicit `Esc` → abandon.
   - Arrow-key navigation does NOT trigger drop. If navigation would naturally leave the TextEdit (Qt default), the commit/abandon decision happens *before* the focus actually leaves.
5. **`LiveView.qml`:**
   - The `_routeFocusToRow` retry loop stays for hole-creation focus (to land the user in the new hole row when it materialises).
   - The reify-focus path is replaced by a `parseUpdated`-once subscription that fires after commit. No more polling for delegate materialisation under the racy condition.
6. **`LiveStructuralKeyHandler`:**
   - Enter at EOB no longer emits a `\n\n` source edit. Only creates the hole.
   - The "stacked Enter" case (user presses Enter twice fast, second hits the empty hole) is handled by ParagraphDelegate's `Enter`-in-empty-hole rule (no-op, hole stays).
7. **Save path:** the binding (or wherever Ctrl+S is handled) calls `layer.commitAllPendingHoles()` before invoking the document save. Guarantees the saved file matches what the user sees on screen.
8. **Tests:** the v1 implementation MUST include a stress-typing test that mirrors the user's manual reproduction — fast successive `keyClick` events into a freshly-created hole, asserting the resulting source is in-order and matches the typed sequence. `tst_view_qml_live_paragraph_hole_integration.cpp` was deleted along with the v0 revert; the v1 version replaces it with this stress test as the load-bearing assertion.

### 3.6. Lessons from the v0 attempt (reverted at `cfbc30f`)

The v0 design (preserved in §3.3 of an earlier draft of this spec, reachable via `git log` for educational value) used a "reify-on-first-keystroke" pattern: insert `\n\n` at hole-creation, create the hole, reify on first keypress by issuing one more `applyLocalEdit` and dropping the hole. Dogfooding under a real `QQuickView` (not the offscreen test harness) surfaced five distinct failure modes:

1. **Visual double-spacing.** The `\n\n` inserted at hole-creation belonged to the previous block's byte range (per the live-editing-design "delegate owns trailing whitespace" invariant). It rendered as a blank line below the previous paragraph. Stacked with the hole row's own visual line, the user perceived two paragraph breaks for one Enter press.
2. **Character scramble during fast typing.** Between hole-drop (synchronous) and new-delegate-materialisation-after-parse (asynchronous, 30-100ms typical), focus is in transit. Every keystroke during that window landed on the wrong delegate, producing scrambled source. User reproduction: "this is interesting" → "t is inhteresg. tinis". The 10-attempt `Qt.callLater` retry loop the code-quality reviewer accepted as defensible only ensured focus *eventually* arrived; it did nothing about intermediate keystrokes. **No automated test at any granularity caught this** — `QTest::keyClick` calls land synchronously between event-loop ticks, masking the race.
3. **Arrow keys destroyed the hole.** v0's abandonment fired on any focus-out, including Up/Down/Left/Right navigation. Normal navigation should not be a hole-killing event.
4. **Focus went nowhere after abandonment.** Drop-on-focus-out left the user with no caret and no recovery path other than clicking. The "route focus to nearest live neighbor" requirement was mentioned in invariant #16 (collab-edit case) but never wired for the local-abandonment path.
5. **Source-state leak.** The `\n\n` we wrote at Enter-time stayed in the file even after silent abandonment. Files accumulated trailing newlines that were invisible to the user. Save → quit → reopen would reveal the damage.

**Root cause analysis.** The v0 design had two compounding mistakes:

- **The spec said "insert `\n\n` AND create the hole"** (§3.3 in the original draft). This was an attempt to keep the parser ahead of the user — by writing the paragraph break to source eagerly, the parser would already know about the new block by the time reification fired. But this conflated *visual paragraph break* (which the user wants to see) with *source paragraph break* (which is what the parser sees). A faithful preedit doesn't put anything in source until commit; the visual paragraph break should come entirely from the hole row, not from `\n\n` rendered as trailing whitespace by the previous block.
- **Reify-on-first-keystroke** assumed the `applyLocalEdit` → parse → new-delegate cycle was fast enough that focus could chase. It is, on a small synthetic test. It isn't on a real document with a real parse pool and real ListView delegate incubation timing. The async hop is mandatory; the fix is not to require it (preedit lets the user keep typing into a stable delegate that *becomes* the real one on commit).

**Process lessons:**

- The unit test (`tst_view_qml_live_paragraph_hole.cpp`) drove the C++ surfaces directly. It passed cleanly while the user-facing demo was completely broken. Driving directly past the QML keyboard path is fast and reliable but cannot catch bugs in async UI behavior.
- The integration test (`tst_view_qml_live_paragraph_hole_integration.cpp`) used `QQuickView` + `QTest::keyClick` and asserted post-typing focus position and source bytes. It also passed. `QTest::keyClick` synchronously delivers events between event-loop spins, which masks the very timing race that fails on real keyboard input where keystrokes arrive on real-time intervals an event loop can drain in between.
- Future stress-typing tests for v1 must either (a) inject keystrokes via `QCoreApplication::sendEvent` with deliberate `QTest::qWait(N)` gaps that mimic real keystroke spacing, or (b) involve a real keyboard hardware harness, or (c) at minimum, repeatedly call `QCoreApplication::processEvents()` between keystrokes to give the binding's async paths a chance to misbehave. Without one of these, stress tests will continue to pass while real users see scrambled letters.

---

## 4. Refactor: predictions move into the layer

### 4.1. `InlineFormatHighlighter` becomes an `InlinePrediction` producer

Today: a `QSyntaxHighlighter` attached to each delegate's `QTextDocument`, painting `QTextCharFormat` for open delimiters seen in the row's source. Direct, no shared state.

New: the layer owns a `Vector<InlinePrediction>` per row. Predictions are produced from the same source-scan logic but *registered with the layer* rather than painted directly. The highlighter becomes the layer's *consumer* — on each `formatBlock` call it asks the layer for predictions intersecting the block's range and applies them. Reconciliation against parser truth (an inline node confirms or contradicts a prediction) lives in the layer, not the highlighter.

This is structurally identical to today's behavior; the value is locating prediction-state in one place so future inline holes (e.g. empty wikilink target) can cohabit.

### 4.2. `LiveSpeculativeFenceController` becomes a `BlockKindPrediction` producer

Today: a Q_OBJECT that watches `editApplied` and flips a `LiveBlockModel` row's kind speculatively.

New: emits `BlockKindPrediction { row, originalKind, speculativeKind, parserConfirmCondition }` into the layer. The layer reconciles on parse return: confirm → drop the prediction (parser truth now matches); contradict → drop the prediction and let the model snap back. Reconciliation logic moves out of the controller into the layer.

### 4.3. No public-API churn

`LiveBlockModel`, `EditorBackend`, `Session` — none change shape. Delegates don't change. The refactor is *internal* to `markoff-view-qml`. Tests for inline highlighting and fence speculation should pass unchanged after the refactor; if they don't, the refactor is wrong.

---

## 5. Architectural invariants this layer must hold

These add to (do not replace) the ten invariants in `docs/specs/2026-04-30-live-editing-design.md` §4.

11. **Source rope is canonical.** Projections never write to the rope except through `applyLocalEdit` at reification time. Save serializes only the rope.
12. **Projections are not in the CRDT undo stack.** Holes that have not reified are pure view state; a hole's drop is not an undo entry. Reification produces a normal CRDT edit which enters the stack normally.
13. **Anchor opacity holds for projection anchors too.** A projection's `origin` is a `BlockAnchor` or `TextAnchor` value; delegates pass it through opaquely. Synthetic projection anchors (those that don't yet correspond to any real CRDT position) are typed distinctly so foundation translation APIs can refuse to translate them.
14. **One layer instance per `LiveListModelBinding`.** No globals. No singletons. The layer's lifetime equals the binding's.
15. **Reconciliation runs synchronously on its trigger.** Parse return → reconcile predictions before emitting model signals. User keystroke → reconcile holes before routing the keystroke. No deferred-reconciliation queues; the layer is a pure function of (parser state, projection set, latest input event).
16. **Collab safety: anchor invalidation drops the projection.** When a remote peer's edit invalidates a projection's `origin`, the layer drops the projection and routes focus to the nearest live neighbor. Same shape as the existing "selection-touched-block-removed" handling.

---

## 6. Save and undo semantics (v1)

**Save (Ctrl+S).** v1 reverses v0's "save flushes only the rope, hole persists" rule. Instead: **save flushes the preedit buffer first, then writes the rope.** If the user has a pending hole with non-empty `bufferText` when they hit Ctrl+S, the layer commits it (one `applyLocalEdit("\n\n" + bufferText)`) and then the document save proceeds normally. If the buffer is empty, save proceeds without committing — there's nothing to write. This makes the saved file always equal to what the user sees on screen.

**Undo (Ctrl+Z).** Three cases:

1. *Hole exists, not yet committed (buffer empty or non-empty).* Ctrl+Z drops the hole. **No CRDT undo is needed** because v1 doesn't write to source at hole creation. Predictions and the buffer evaporate. (v0's undo had to undo the paired `\n\n` edit — that complexity is gone.)
2. *Hole was committed by idle/focus-out/Enter.* Ctrl+Z runs normal CRDT undo on the commit edit (which removed `"\n\n" + bufferText` in one step). The hole does not re-appear; focus restores to the previous block's end via the CRDT undo's normal flow.
3. *Predictions.* Predictions don't enter the undo stack at all — they are a function of source state, so undoing source automatically reconciles them.

**Redo (Ctrl+Y).** Symmetric to undo. A redo of a committed empty-paragraph step replays the `applyLocalEdit("\n\n" + bufferText)` edit. The hole is *not* re-created — the redo lands the real block directly, mirroring the normal "did → undid → did again" cycle.

---

## 7. Public-API surface

The layer is C++ internal to `markoff-view-qml`; no QML-visible API changes for v0.

```cpp
namespace Markoff::View::Qml {

class LiveProjectionLayer : public QObject {
    Q_OBJECT
public:
    explicit LiveProjectionLayer(QObject *parent = nullptr);

    void setEditorBackend(EditorBackend *backend);
    void setBlockModel(LiveBlockModel *model);

    // Hole creation + buffer mutation hooks (called by structural-key handler,
    // delegate's binding, etc.). v1 preedit-pattern: createBlockHole does NOT
    // write to source; setBlockHoleBuffer mirrors local TextEdit content into
    // the layer's buffer; commitBlockHole flushes the buffer to source via
    // applyLocalEdit and drops the hole.
    quint64 createBlockHole(BlockHole hole);
    void    setBlockHoleBuffer(quint64 holeId, const QString &text);
    void    commitBlockHole(quint64 holeId);
    void    dropBlockHole(quint64 holeId);  // abandon path — no source mutation
    void    commitAllPendingHoles();         // called by save path
    void    createInlineHole(InlineHole hole);

    // Prediction creation hooks (called by InlineFormatHighlighter,
    // LiveSpeculativeFenceController).
    void createInlinePrediction(InlinePrediction p);
    void createBlockKindPrediction(BlockKindPrediction p);

    // Lookups for consumers.
    QList<InlinePrediction> predictionsForRow(int row) const;
    BlockKindPrediction *blockKindPredictionFor(int row) const;
    bool rowIsHole(int row) const;

Q_SIGNALS:
    void rowsChanged(int firstRow, int lastRow);  // model-shaped signal
    void holeReified(BlockAnchor newAnchor);      // for focus routing

private Q_SLOTS:
    void onParseUpdated(/* parsed, parseSeq, blockAnchors */);
    void onLocalEditApplied(/* edit list */);

private:
    // Reconciliation core.
    void reconcileAgainstParse(/* ... */);
    void reconcileAgainstInput(/* ... */);

    QList<ProjectionItem> m_items;
    EditorBackend        *m_backend = nullptr;
    LiveBlockModel       *m_model = nullptr;
};

struct BlockHole {
    BlockAnchor origin;          // synthetic until reified
    QString     kind;            // "paragraph", "list_item", ...
    quint32     pairedSourceEditByteCount = 0;  // for undo coalesce
    // ... reification + abandonment closures
};

struct InlineHole { /* analogous */ };
struct InlinePrediction { /* range + format */ };
struct BlockKindPrediction { /* row + kinds + confirm-rule */ };

}  // namespace Markoff::View::Qml
```

---

## 8. Out of scope (for v0)

- The full hole inventory beyond empty-paragraph (deferred to follow-on specs).
- Configurable hole-abandonment timeouts. (v0 hard-codes 30s for empty-paragraph.)
- Persisting projection state across mode toggles. (Switching to source mode drops all projections; structurally equivalent to focus-out abandonment.)
- Multi-cursor / secondary-selection holes. Foundation supports multi-cursor; layer is single-primary-selection only in v0.
- Hole serialization (e.g. for crash recovery). Holes do not survive process exit.
- Heuristic hole creation outside Enter (e.g. clicking past EOB to "create-on-click"). Not in v0.

---

## 9. Open questions (v1)

These do not block v1 implementation but should be resolved during dogfooding.

- **Idle-debounce threshold.** Proposed 250ms (v1). Trade-off: shorter = more parse round-trips and earlier commits (less risk of unsaved work); longer = fewer commits but longer windows where what-you-see ≠ what-you-save. 250ms is a guess based on typical typing-burst pause durations; revisit.
- **Multiple stacked Enter presses on an empty hole.** Proposal: no-op (the hole stays). User pressed Enter once, got an empty paragraph to type in; pressing Enter again with no content typed shouldn't create *another* empty paragraph below.
- **Multiple stacked Enter presses on a non-empty hole.** Proposal: commit current buffer, then create a new hole below for the next paragraph. Mimics "I typed a paragraph and pressed Enter to start the next one."
- **Backspace traversal across a (real) committed paragraph break.** Backspace at qtPos 0 of a real block that follows a `\n\n`: does it merge the two paragraphs? This is base markdown-editor behavior, not hole-specific; current implementation (Stage 3 tip) likely already handles it correctly via `LiveStructuralKeyHandler::Key_Backspace`. Verify during v1 dogfood.
- **Save-while-hole-exists.** v1 commits the buffer first, then saves. Does the dirty indicator update correctly during the brief commit→save window? Proposal: yes, since the commit itself is a CRDT edit and `editSequence` increments naturally.
- **Concurrent collab edits during preedit.** A remote peer edits the document while the user is typing into a hole. The hole's `reifyOffset` may or may not still be valid. Proposal: on remote edit, if `reifyOffset` is still valid (no remote mutation in `[reifyOffset-2, reifyOffset+2]`), keep the hole; otherwise drop it (with the buffer's contents — user loses their preedit). This is acceptable v0 behavior for collab; v1 of v1 might preserve the buffer by re-targeting `reifyOffset` to the nearest stable anchor.

---

## 10. References

- `docs/specs/2026-04-29-live-render-design.md` — walking-skeleton design.
- `docs/specs/2026-04-30-live-editing-design.md` — editing invariants (§4) extended by §5 above.
- `libs/markoff-view-qml/src/InlineFormatHighlighter.cpp` — first prediction citizen, refactored in this work.
- `libs/markoff-view-qml/src/LiveSpeculativeFenceController.cpp` — second prediction citizen, refactored in this work.
- `libs/markoff-view-qml/src/LiveStructuralKeyHandler.cpp` — site of the empty-paragraph bug; gains a hole-creation hook in this work.
- Yahn Bernier, "Latency Compensating Methods in Client/Server In-game Protocol Design and Optimization" (2001) — the canonical optimistic-UI / client-prediction reference.
- Hazel project (Cyrus Omar et al.) — academic treatment of holes as first-class typed entities in editor models.
