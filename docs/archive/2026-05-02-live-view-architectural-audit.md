# Live view — architectural audit (first impressions)

**Date:** 2026-05-02
**Branch:** `exploration/new-foundation`
**Scope:** `libs/markoff-view-qml/qml/LiveView.qml` and its supporting C++ classes (`LiveListModelBinding`, `LiveBlockModel`, `LiveEditBinding`, `LiveStructuralKeyHandler`, `LiveProjectionLayer`, `LiveSelectionView`, `LiveSpeculativeFenceController`, `InlineFormatHighlighter`, `BlockWalker`, `AstBlockDiff`) and the five v0 delegates.
**Constraint:** delegates remain per-block; no architectural option that retires that shape is considered here.
**Method:** code-only first impressions. Comments and docs were deliberately *not* used as input — only behaviour deducible from sources. Subsequent passes can layer commit history, specs, and dogfood notes on top of this baseline.

The aim of this audit is to characterise (a) what the live view is trying to do, (b) why it is not stable in its current form, and (c) what layers should have been built up incrementally so that the same end-state could be reached without the present fragility.

---

## (a) What the live view is trying to do

Render a Markdown document as a `ListView` of per-block delegates (paragraph / heading / hr / image / code-block), each text-bearing delegate being its own `TextEdit` whose plaintext is *source-faithful* for the block's range, with inline Markdown formatting (bold/italic/code/strike/highlight/link) overlaid by a per-delegate `QSyntaxHighlighter` (`InlineFormatHighlighter`).

Editing flow:

1. Keystroke in a delegate's `TextEdit` →
2. `LiveEditBinding::onContentsChange` translates `(qtPos, charsRemoved, charsAdded)` into a UTF-8 `MarkoffEdit` against the CRDT (`MarkoffDocument::applyLocalEdit`).
3. The parser asynchronously returns a new AST snapshot.
4. `LiveListModelBinding::onParseUpdatedAt` walks it via `BlockWalker` into `BlockRecord`s.
5. Myers-diff over `BlockKey` (kind + `BlockAnchor`) emits `Equal | Insert | Delete` ops.
6. `LiveBlockModel::applyOps` translates the diff into Qt-model signals so existing delegates persist across edits and only the diff churn is paid.

Above this base sits the **projection layer**, providing two view-only constructs:

- **Predictions** — speculative inline formatting (`InlineFormatHighlighter::publishInlinePredictions`) and speculative `paragraph→code_block` kind flips (`LiveSpeculativeFenceController`) that run ahead of the parser. Reconciliation rule: every parse arrival drops all predictions; the next setSource republishes whatever still applies.
- **Holes** — empty-paragraph rows the parser cannot represent yet (Enter at EOB before any character has been typed). The hole is held with a `bufferText` on the layer; commit produces a single CRDT edit at `reifyOffset` on Enter / focus-out / Up-Down / save.

Selection is `Session::primarySelection` projected into block-local ranges by `LiveSelectionView`. A top-level `MouseArea` runs the cross-block hit-test (the `hit(mouseX, mouseY)` JS function in LiveView.qml). Structural keys (Enter / boundary-Backspace / boundary-Delete / Tab) bypass `LiveEditBinding` and call `LiveStructuralKeyHandler` directly so that paragraph-shape edits can reach the CRDT without going through the per-delegate text bridge.

That is a coherent ambition. The individual codepaths above are each defensible.

---

## (b) Why it is not succeeding

The system has **six** overlapping authority claims for "what is the content of block N right now," and the reconciliation rules between them are defined pairwise, not globally:

1. The CRDT rope (`MarkoffDocument`) — canonical, updated synchronously on keystroke.
2. The most recent parse snapshot (`Markoff::Document` + `BlockAnchor`s) — drives model rows; lags keystrokes by one parse round-trip.
3. `LiveBlockModel` rows — the parsed snapshot mirrored into Qt-model land, *plus* a speculative-kind overlay (`m_speculativeOriginals`).
4. Each delegate's `QTextDocument` — what the user actually sees and types into.
5. Projection-layer **predictions** (inline format + block kind) — a parallel speculative registry duplicating (3)'s overlay.
6. Projection-layer **hole** + `bufferText` — a phantom row whose contents live nowhere in (1)–(5).

Every cross-domain seam grew its own ad-hoc cycle guard or skip rule as a new feature broke an old assumption, and those rules do not compose.

### Cycle-guard archaeology

- `LiveEditBinding::m_applyingModelUpdate` (model→delegate echoes).
- `ParagraphDelegate::m_applyingModelBuffer` (model→hole-buffer echoes).
- `m_composing` deferral + `LiveBlockModel::setComposingRow` (IME).
- `if (textEdit.activeFocus) return` in `onBlockTextChanged` (ParagraphDelegate.qml:264) — **the single most load-bearing line in the file**: it says "the model is canonical, *except* the focused delegate's `TextEdit` owns its own content during a parse round-trip." Every downstream weirdness traces back to this exception.
- The mid-block-Enter handler has to *locally truncate the TextEdit prefix* before calling `applyLocalEdit` (ParagraphDelegate.qml:144-174) because the focused-skip rule above would otherwise leave the pre-Enter text visibly duplicated when the second-half row arrives. That is a smell-of-a-smell: the focused-skip exception forced the structural-edit path to do its own out-of-band visual mutation.
- `applyOps` cannot run with a hole present, so `LiveListModelBinding::onParseUpdatedAt` has to detach the hole, run the ops, then `reattach` *or* `reattachAndAbandon` based on whether the hole's anchor row is still in range (LiveListModelBinding.cpp:175-194). The hole abstraction does not compose with the diff abstraction.

### Focus delivery is five concurrent retry-loops

- `holeReified` (commitBlockHole → parse-back insert).
- `holeCreated` / `holeDropped` (synchronous `insertHoleRow`).
- `focusAfterStructuralEdit` (mid-block split's second-half row).
- `focusRowReady` / `requestFocusOnRowInserted` (gated retry-on-`Qt.callLater`, capped at 10 attempts).
- `focusRestoreRequested` + `isFocusRestoreTarget` (kind-change-in-place — Delete+Insert at the same row).
- `routeNeighbourFocus` for HR/image (block has no cursor of its own).

Each path was added when a specific dogfood scenario broke the previous one. None of them know about the others. The `Qt.callLater(function() { Qt.callLater(tryFocus) })` (LiveView.qml:127) and the ten-attempt retry loop are giving up on understanding the timing and brute-forcing it.

### Performance is downstream of the architecture

`InlineFormatHighlighter::rebuildSpans` constructs a fresh `TreeSitterParser` on every keystroke per delegate (line 107) — because the highlighter has to re-derive the span map from raw source on every `setSource`, because the model role gives it raw text and not pre-baked spans, because the model is fed by a single AST walk that does not bake per-block inline span data. `LiveEditBinding::onContentsChange` does three full-document UTF-16↔UTF-8 round trips per keystroke (lines 197-218). Each is its own layer that wasn't separated.

### Stated invariants are routinely bent

The cross-cutting invariants in `libs/markoff-view-qml/CLAUDE.md` are not all enacted by the code:

- Invariant 1 ("qtPos translates to canonical byte offset by simple arithmetic, never by reverse-mapping rendered text to source") is contradicted by `LiveStructuralKeyHandler.cpp:130` (`blockText.left(qtPos).toUtf8().size()`) and `LiveEditBinding.cpp:200-211`.
- Invariant 7 ("no focus-chain workarounds, no `setFocusProxy` chains, no `QApplication::sendEvent` re-dispatch") is partly contradicted by the QML retry loops and the `Qt.inputMethod.commit()` ahead-of-snapshot pattern.
- Invariant 4 ("no `TextEdit.MarkdownText`") is honoured.
- Invariant 2 ("single source of truth: `MarkoffDocument` only") is honoured *modulo* the focused-skip rule, which is the whole problem.

### Summary diagnosis

The architecture *describes* a unidirectional data-flow with one canonical truth. It *enacts* a bidirectional gossip protocol with six sources of truth and a rule book of pairwise exceptions. That is why each new feature surfaces a new race.

---

## (c) The layers that should have been built up

Keeping the per-block-delegate constraint:

### L0 — coordinate primitives

One module that converts between `(UTF-8 byte offset in document)` ↔ `(UTF-16 qtPos in document)` ↔ `(block-index, block-local qtPos)` ↔ `(block-index, block-local byte offset)`. Tested in isolation. Currently the conversions are open-coded in three places (`LiveEditBinding`, `LiveStructuralKeyHandler`, `SourceTextDocumentBinding` static helpers), each subtly different.

### L1 — read-only render of a parsed snapshot

`ListView` + `DelegateChooser` + per-kind delegates with read-only `TextEdit`s, fed from a `BlockRecord` list built by `BlockWalker`. No diff, no editing, no predictions, no holes, no selection, no parser-as-async-event. Lock down sizing, wrapping, image/code rendering, theming. The current "walking skeleton" already shipped *with* selection and the `hit()` MouseArea wired in — those concerns were mixed in too early.

### L2 — Myers-diff–driven model updates

`applyOps(Equal | Insert | Delete)` over `BlockKey`. Driven only by reload-the-file or simulated parses. Verify: delegates persist across edits, no spurious `dataChanged` on `Equal`, image/code delegates do not reload, model rebuilds are O(diff) not O(rows). No editing yet.

### L3 — selection projection

`LiveSelectionView` reading `Session::primarySelection` into per-block ranges. Mouse hit-test. Copy. Still read-only on the document side. Get cross-block selection right with zero editing in the picture; prove the `hit()` math is robust *before* any other concern depends on it.

### L4 — single-delegate editing, parse-authoritative

Keystrokes in one focused delegate produce a CRDT edit; the parser eventually returns; the model updates the row; the delegate receives the new `text` role and rebuilds via `setModelText` **unconditionally**. No "skip if focused" exception. Force the question:

> When there is a parse-text-vs-typed-text divergence, who wins?

- If the answer is **"model wins"**, accept the cursor jitter and design the cursor protocol around that (e.g. anchor-tracked cursor that survives the rebuild).
- If the answer is **"the focused delegate is canonical between parses"**, design *that* into the model layer (per-row pending-edit state on the model itself), not as an opt-out in QML.

Pick one. Decide once. The current code picks "delegate wins" implicitly while the rest of the system assumes "model wins" — that is the foundational fault. Every layer above L4 inherits it.

### L5 — structural edits

Backspace-at-start, Delete-at-end, Enter-at-EOB, Enter-mid-block. One module produces one CRDT edit and emits one "post-parse focus hint." One focus-routing strategy with one retry policy. The mid-block-Enter local-truncation hack disappears once L4 is decided correctly: if the model wins, the parse-back rebuilds both halves; if the delegate wins, the structural-key handler hands the prefix to this delegate and the suffix to the new one explicitly, and the model's parse-back is a no-op on both.

### L6 — IME composition

Defer `applyLocalEdit` during preedit; commit replaces the block in one edit. Genuinely a new concern and deserves to be its own layer. The existing implementation is roughly right.

### L7 — inline-format speculation (render-only)

`**foo` rendered bold while still typing. This is *purely* a `QTextCharFormat` application on the highlighter side — it never mutates the model, the delegate's text, or the CRDT. Reconciliation: parse-arrival drops all predictions; the next `setSource` republishes whatever is still applicable. **Single home** (the layer's registry). No fallback path — tests use a real layer instance.

### L8 — block-kind speculation (model-mutating)

The fence-opener case. This is the *one* speculation that touches a model role (`KindRole`), and that is what makes it categorically different from L7. Reconciliation: parse-arrival clears speculative kinds in the model directly. The duplicate registry in the projection layer collapses: speculation lives in the model (where its effect lives) with the model's own clear-on-`applyOps` invariant.

### L9 — pre-parser intent ("holes")

Empty paragraph after Enter, empty list item, empty fence interior. This is the most invasive feature and should have been **last**. The right shape, in retrospect: a separate phantom-rows model that composes with the parsed-rows model via a concatenating proxy, so `applyOps` never has to know holes exist (the proxy handles row-index translation). The current "detach / re-run / reattach-or-abandon" dance is direct evidence that the hole was bolted into the parsed-rows model rather than composed alongside it.

### L10 — multi-cursor / collab safety

Out of scope here, but the architecture should be such that adding it does not require rewriting L4–L9.

---

## Single-sentence root cause

**L4 was decided implicitly under dogfood pressure rather than explicitly during design**, and every layer above it grew exception logic to compensate. Until that decision is reopened — either commit to "model wins, design the cursor protocol around parse round-trips" or commit to "focused delegate is the source of truth between parses, and the model exposes that" — predictions, holes, structural edits, and focus routing will keep producing new races, because they are all reconciling against a thing whose authority is conditional.

---

## What this audit deliberately does not do

- It does not consult `docs/specs/`, `docs/plans/`, `docs/handoff/`, commit history, or dogfood transcripts. Those are the next pass; they will either confirm, refine, or contradict the layering proposed above.
- It does not propose a migration plan from the current codebase to the layered design. That is a separate document, predicated on the L4 decision being made first.
- It does not assess whether `markoff-core`, `markoff-parser`, or the QML host shell need changes. The scope here is the live-view library.
