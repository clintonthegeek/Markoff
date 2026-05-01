# Live Projection Layer — design

**Date:** 2026-05-01
**Branch:** `exploration/new-foundation`
**Phase:** Phase-2 follow-on inside `markoff-view-qml`. Builds on the live-editing design (`docs/specs/2026-04-30-live-editing-design.md`) and the walking skeleton (`docs/specs/2026-04-29-live-render-design.md`).
**Status:** draft (pending user spec review)
**Depends on:** `markoff-foundation` BlockAnchor/TextAnchor APIs (already shipped); `LiveBlockModel`, `LiveListModelBinding`, `InlineFormatHighlighter`, `LiveSpeculativeFenceController` (already shipped).

## 0. TL;DR

The editing model is a *superset* of the source model. The projection downward to source is *lossy by design*. Today that gap is filled by two ad-hoc speculations (`InlineFormatHighlighter` for open inline delimiters, `LiveSpeculativeFenceController` for unclosed fences) plus a *missing third citizen* — **holes** — for user intent that the markdown source language cannot represent yet.

This spec promotes "view-state ahead of source" to a first-class architectural layer: the `LiveProjectionLayer`. It absorbs the two existing speculations and introduces holes as siblings under one shared protocol. Holes solve the user-visible bug where pressing Enter at end of a paragraph appears to do nothing — the resulting `\n\n` lands in the source rope but the parser produces no second block, so the view shrugs and the user's intent evaporates.

**v0 scope:** the projection layer abstraction, the refactor of `InlineFormatHighlighter` and `LiveSpeculativeFenceController` behind it (no behavior change), and a single new hole — *empty paragraph after Enter*. **Explicitly out:** the rest of the hole inventory (list items, checklists, code-fence interior, blockquote, callout, table cells, links, wikilinks, footnotes, math); collaborative-edit hole invalidation beyond the simple "anchor deleted → drop" case; configurable abandonment timeouts.

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

### 3.2. Hole lifecycle

```
   user input intent (e.g. Enter at EOB)
              │
              ▼
   ┌──── layer creates BlockHole ────┐
   │   anchor = end-of-source        │
   │   kind   = "paragraph"          │
   │   reify  = printable char       │
   │   abandon = focus-out, undo,    │
   │             idle 30s            │
   └──────────────┬──────────────────┘
                  │
   ┌──────────────┴──────────────────┐
   │ next event                      │
   │   reify-trigger fires? ─── yes ─┼─► build canonical MarkoffEdit
   │                                 │     for kind+content; drop hole;
   │                                 │     applyLocalEdit
   │   abandon-trigger fires? ── yes ┼─► drop hole; route focus
   │                                 │
   │   parse arrives without         │
   │     reifying input?       ───── ┼─► hole persists (intent is still
   │                                 │     valid; no source change)
   └─────────────────────────────────┘
```

### 3.3. The v0 hole: empty paragraph after Enter

**Trigger.** `LiveStructuralKeyHandler` detects Enter at `qtPos == blockText.length()` *and* the resulting source would have no parser-visible block past `currentBlockEnd`. Source-byte edit (`\n\n` insert) still happens, *and* the layer creates a `BlockHole(kind="paragraph", origin=currentBlockEnd+2)`.

**Reification.** First printable character (or paste, or IME commit) into the hole's delegate triggers reification: the layer builds a `MarkoffEdit` inserting that text at `origin`, drops the hole, calls `applyLocalEdit`. Next parse produces the real paragraph; the row swap is invisible because the hole was at the same model index.

**Abandonment.** Focus leaves the hole's delegate, the layer drops the hole. Undo (Ctrl+Z) before reification: drop the hole; no CRDT rollback needed because the `\n\n` insert was already a real source edit and is undone separately by the CRDT undo stack. Idle 30 seconds without reification: drop the hole.

**Edge case — backspace inside an empty hole.** Backspace at qtPos 0 in an empty hole drops the hole and routes focus back to the end of the previous block. No CRDT edit; the trailing `\n\n` from the original Enter remains in source until the user takes another action that touches it.

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

These all share the same archetype-shape (trigger keystroke → kind → reification rule → abandonment rule). v0 ships only #1; the rest land in follow-on plans once the abstraction has soaked.

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

## 6. Save and undo semantics

**Save (Ctrl+S).** Source rope is what hits disk. Projections — predictions and holes — are not flushed. If the user has a pending hole when they save, the saved file does not contain it. This is the correct behavior: the user has not committed the intent yet (no character typed into the hole), so there is nothing to persist. After save, the hole persists in view-state because `editSequence` (the dirty-tracking source) only counts real CRDT edits; an in-flight hole does not mark the document dirty by itself.

**Undo (Ctrl+Z).** Three cases:

1. *Hole exists, not yet reified.* Ctrl+Z drops the hole. If the hole's creation was paired with a real CRDT edit (e.g. the `\n\n` insert that paired with the empty-paragraph hole), the CRDT undo runs and undoes that edit too, in the same Ctrl+Z. The pairing is the layer's job to register at hole-creation time.
2. *Hole was reified by a printable character.* Ctrl+Z runs normal CRDT undo on the reification edit. The hole does not re-appear; the previous block regains focus naturally because the CRDT state matches pre-reification.
3. *Predictions.* Predictions don't enter the undo stack at all — they are a function of source state, so undoing source automatically reconciles them.

**Redo (Ctrl+Y).** Symmetric to undo. A redo of an Enter-empty-paragraph step replays the `\n\n` CRDT edit; the layer observes the edit and re-creates the hole as a fresh projection. (This is *not* re-creating the same hole from saved view state — it is a new hole produced by the same trigger, identical for user purposes.)

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

    // Hole creation hooks (called by structural-key handler, list-item-key
    // handler, etc.).
    void createBlockHole(BlockHole hole);
    void createInlineHole(InlineHole hole);

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

## 9. Open questions

These do not block implementation but should be resolved before the layer leaves draft.

- **Idle-abandonment threshold.** 30 seconds is a guess. The right number probably comes from dogfooding. v0 hard-codes; revisit after a week of use.
- **Multiple stacked Enter presses.** Pressing Enter twice in succession at EOB: does the second Enter create a *second* empty-paragraph hole, or does it merge / no-op? Proposal: second Enter is a no-op (the hole is already there and empty; nothing to split). Validate with users.
- **Backspace traversal across a hole.** Backspace from inside an empty hole drops the hole. Backspace from the *start of a real block following a hole row*: does it drop the hole, or does it merge across the trailing `\n\n` in source? Proposal: drop the hole first, leave source untouched; second Backspace does the source-level merge. Two-step, predictable.
- **Save-while-hole-exists.** Spec says save flushes only the rope, hole persists. But the dirty indicator behavior — does the hole alone make the doc "dirty"? Proposal: no, only CRDT edits make it dirty. The Enter that created the hole already did, via its `\n\n` source edit.

---

## 10. References

- `docs/specs/2026-04-29-live-render-design.md` — walking-skeleton design.
- `docs/specs/2026-04-30-live-editing-design.md` — editing invariants (§4) extended by §5 above.
- `libs/markoff-view-qml/src/InlineFormatHighlighter.cpp` — first prediction citizen, refactored in this work.
- `libs/markoff-view-qml/src/LiveSpeculativeFenceController.cpp` — second prediction citizen, refactored in this work.
- `libs/markoff-view-qml/src/LiveStructuralKeyHandler.cpp` — site of the empty-paragraph bug; gains a hole-creation hook in this work.
- Yahn Bernier, "Latency Compensating Methods in Client/Server In-game Protocol Design and Optimization" (2001) — the canonical optimistic-UI / client-prediction reference.
- Hazel project (Cyrus Omar et al.) — academic treatment of holes as first-class typed entities in editor models.
