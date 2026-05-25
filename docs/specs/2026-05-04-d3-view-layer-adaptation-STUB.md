# D3 — View-layer adaptation (STUB)

**Date stub created:** 2026-05-04
**Status:** STUB — intended scope and inputs captured. **Substantive design happens after D2 lands** and is gated on D2 implementation reaching the point where the foundation public API is stable enough to consume.

---

## What this stub is for

This file exists so a fresh agent context picking up D3 can immediately understand:
- What inputs D3 consumes
- What scope D3 owns
- What's explicitly out of scope
- Where to start brainstorming when the time comes

It is **not** a substantive design. Do not implement against this stub.

---

## Inputs

When you sit down to design D3, you'll need:

1. **`docs/specs/2026-05-04-d2-foundation-reshape-design.md`** — the D2 spec. The foundation's public API after D2 (BlockEdit, StructuralOp, Cmd::*, the per-block + structural signals) is what D3 consumes.
2. **`docs/archive/c-restoration-arc/2026-05-02-live-render-restoration-design.md`** — the C-restoration spec. The lower-layer decisions (L0 coordinate primitives, L1 read-only render, L2 diff-driven model, L3 cursor model in Shape 1 discriminated form) carry forward to D3 unchanged. The upper-layer decisions (L4 sequence-tagged staleness, L5 structural keys with marker-paragraph machinery) **retire**.
3. **The current `libs/markoff-live/` source tree** — the in-tree code that D3 reshapes. Specifically the L4 `LiveEditBinding`, L5 `LiveStructuralKeyHandler`, and the marker-paragraph machinery (`MarkerScrubber`, atomic-bundled-edit primitive, ZWSP scrubbing) that delete entirely.
4. **D2's `Migration` section (§9)** — the layer-by-layer transition table is D3's starting point. D3 owns the substantive design of how each transformed layer actually works.

---

## Intended scope

D3 designs and implements:

1. **L4 reshape.** `LiveEditBinding` becomes a thin keystroke-to-`applyBlockEdit` translator. The freshness gate, three cycle guards, previousText cache all delete (the staleness window vanishes under D2's per-block CRDT model). Per-keystroke routing flows directly to the per-block `Buffer`.
2. **L5 reshape.** `LiveStructuralKeyHandler` keeps its kind-keyed dispatch shape but each handler now calls `Cmd::*` instead of issuing low-level `MarkoffEdit`s. `Cmd::enterAtEnd`, `Cmd::backspaceMerge`, `Cmd::deleteMerge`, `Cmd::insertSoftBreak`, etc.
3. **Marker-paragraph machinery deletion.** `MarkerScrubber`, atomic-bundled-edit primitive, ZWSP scrubbing on copy, no-op stacked-Enter rule, marker-aware initial-qtPos rule — all delete. Enter is `Cmd::enterAtEnd` → `IdList::insertAfter` → cursor delivery via the standard parser-driven row pipeline (which in D2 is structural-CRDT-driven, not parser-driven).
4. **Cursor delivery via structural CRDT signals.** Today's `requestTextCaretAtNewRow` mechanism (added during R5.5 dogfood) becomes "wait for `IdList::structureChanged.rowsInserted` matching the requested anchor." Substantially simpler than the existing pending-resolve gate.
5. **`UndoCoalescer` removal.** The view-side coalesce policy retires; coalescing now lives in `UndoLog` per D2 §4.3.
6. **Per-block undo UI.** Optional: D3 may expose a per-block undo gesture (e.g., right-click → "Undo in this block"). API exists in D2 (`MarkoffDocument::undoForBlock`); D3 decides UX.
7. **Inline span consumption.** Switch from R1B's pre-baked `TopLevelBlock::inlineSpans` to D2's per-block `InlineParseCache`. Read API: `block.inlineSpans()` (synchronous on calling thread per D2 §6).
8. **L6 / L7 / L8 (R6+ in C-restoration nomenclature).** Other text blocks (heading, code-block, hr, image), structured text blocks (lists, blockquotes), interactive blocks (math). These were never written as plans in C; D3 owns them under the D model.

---

## Explicitly out of scope

- **Foundation API changes.** D3 consumes the D2 API. If D3 needs an API change, it's a D2 spec amendment, not a D3 design.
- **Parser library changes.** D4 owns parser library trimming. D3 consumes D2's per-block inline parse contract.
- **Collab features.** D5 owns wire format, presence, conflict UI. D3's view layer is collab-ready (per the C-restoration's premise 9) but doesn't ship collab.
- **`libs/markoff-view-qml`.** The legacy QML library stays in service of source mode. D3 doesn't touch it.

---

## Open questions for D3 brainstorm time

These will need explicit answers when D3 is brainstormed:

- Plugin block-kind registration interface (D2 §11 Q2 deferred this here).
- `BlockAttrsMap` `AttrValue` variant scope (D2 §11 Q4 deferred this here).
- Per-block undo gesture UX (right-click? keyboard shortcut? not exposed at all?).
- How does the structural-key handler determine cursor delivery for compound intents (Backspace-merge → cursor goes to (N-1, end-of-N-1); the structural-key handler must compute this from D2 state and request it via a cursor-state mechanism that's still TBD for D3).
- Test strategy for the view layer reshape (likely matches C-restoration's per-layer test contract; confirm).

---

## Brainstorming checklist (when ready)

When the user is ready to pick up D3:

1. Read all four inputs above.
2. Invoke `superpowers:brainstorming` with the inputs as context.
3. Resolve open questions through ABC-style decisions (matching the D2 brainstorming pattern).
4. Present design in sections; get approval per section.
5. Write the substantive D3 spec, replacing this stub. Update `docs/d-arc/d-arc-status.md` to mark D3 active.
6. Update `docs/d-arc/2026-05-04-d-arc-roadmap.md` to point at the substantive D3 spec.
