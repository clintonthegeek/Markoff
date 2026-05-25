# collabtext scope line — the six "won't do" items

**Date:** 2026-05-04
**Source:** `~/dev/collabtext/docs/specs/2026-05-04-d-evolution-response.md` §"What we won't do, and want to be explicit about now"
**Status:** Binding cross-arc constraint. **No D-arc spec may take a design decision that depends on collabtext shipping any of the items below.**

This document quotes the maintainer response verbatim. Do not paraphrase. If a design pressure arises that pushes against one of these lines, the answer is "build it Markoff-side or use an external dependency" — never "ask collabtext to extend the line."

---

## Verbatim quote (from the maintainer response)

> ## What we won't do, and want to be explicit about now
>
> These aren't deferred decisions — these are the line we're drawing as a condition of saying yes to β.
>
> 1. **No `moveAfter` in v1.** Concurrent move semantics are genuinely subtle and we don't want to litigate them under deadline pressure. Express moves as remove + insert, accept the "loses intent" cost, and revisit only if a real Markoff feature concretely needs structural-move CRDT semantics. Our suspicion is it never will.
>
> 2. **No per-element values, ever.** Not in v1, not in v2. The moment `IdList` carries application data with its own merge rules, we've started building the framework the README disclaims. Sibling maps live in `Markoff::DocumentStructure`. If you later want a `Crdt::Map` primitive, that is a separate proposal with a separate scoping conversation, not a follow-on tweak to `IdList`.
>
> 3. **No cross-CRDT undo log primitive (your Q2 (b)/(c)).** Your tentative preference (a) is correct. The application owns the cross-CRDT edit log. Anything else turns collabtext into a transaction manager.
>
> 4. **No cross-CRDT GC coordination primitive (your Q3).** Each `IdList` and each `Buffer` exposes `compact(watermark)`. Coordinating watermarks across the document is `DocumentStructure`'s job. We will not invent a "document watermark" abstraction inside collabtext.
>
> 5. **No `CollabDocument` generalization.** `CollabDocument` stays Buffer-bound. Markoff composes `DocumentStructure` directly on top of public `IdList` + `Buffer` + the existing `StreamSync` (which is already multi-stream and won't need changes — register one stream for structure plus one per block, payloads are already opaque to the transport).
>
> 6. **No precedent for further primitives.** We want to flag this explicitly. `IdList` is defensible to us because it's the same algorithm as `Buffer` with a different element type — list CRDT over `uint64` instead of list CRDT over UTF-16 code units. It is *not* a precedent for `Map`, `Counter`, `Tree`, or nested compositions. If Markoff's roadmap eventually wants those, the answer will likely be "build it outside collabtext or fork." We'd rather say that now than discover it under feature pressure later.

---

## How each line maps to D-arc decisions

| Line | D-arc consequence |
|---|---|
| 1. No `moveAfter` v1 | `StructuralOp` variant in D2 has no `MoveEntry` verb. Move-block UX (drag to reorder, post-D5) decomposes to remove + insert; intent is lost. Acceptable per maintainer judgement; revisit only if a real feature requires intent preservation. |
| 2. No per-element values | `IdList` elements are opaque `uint64`. All per-block metadata (kind, attrs, link refs, footnote defs, frontmatter) lives in Markoff-owned causal-LWW sibling maps (D2 §2.2). |
| 3. No cross-CRDT undo log | `Markoff::UndoLog` is application-side (D2 §4.2). One Markoff-owned `UndoEntry(actionId, targets[])` per user action; document undo dispatches `.undo()` to each target's per-CRDT stack in reverse op-order. |
| 4. No cross-CRDT GC coordination | `Markoff::WatermarkCoordinator` is application-side (D2 §7). Save-triggered watermark advance; dispatches `compact(watermark)` to each CRDT individually. |
| 5. No `CollabDocument` generalization | D5 wires transport directly via `StreamSync` (one stream for `IdList`, one stream per `Buffer`). No expectation of a Markoff-friendly `CollabDocument` wrapper. |
| 6. No precedent for further primitives | If post-D5 features want `Crdt::Map` or `Crdt::Tree`, the design choice is build-Markoff-side or external dependency — not "ask collabtext for another primitive." Frame any future request as a fully separate proposal with its own scoping conversation. |

---

## What the maintainers DID commit to (for completeness)

For symmetry with the "won't do" list, here are the affirmative commitments — also from the maintainer response. These are guarantees Markoff can rely on:

- A single new primitive `CollabText::Crdt::IdList`: opaque `uint64` elements, sharing collabtext's existing op-causality model, anchor model, undo machinery, GC primitives.
- API surface: `insertAfter(anchor, id)`, `removeAt(anchor)`, `ids()`, `anchorOf(id, bias)`, `applyRemote(op)`, `setOnChange(cb)`, `undo()/redo()`, `collect_garbage()`, `compact(watermark)`.
- Anchor type: the existing `Crdt::Anchor` (not a new `ListAnchor`).
- Op type: a separate `IdListOperation` variant; wire schema bumps additively.
- Convergence/fuzz coverage from day one; fixtures public for Markoff foundation tests to use as ground truth.
- Documentation depth comparable to `Buffer`'s on concurrent semantics, memory complexity, wire format, undo model.
- Joint review pass on `Markoff::DocumentStructure` when D2 is in design (offered explicitly).
- Markoff D2 design must record this scope line in writing — explicit ask in the response (this document is that record).

---

*This document does not get updated. The scope line is fixed; if a future change to it occurs (e.g., a follow-up exchange with maintainers), that change is recorded as a new dated doc — never by editing this one.*
