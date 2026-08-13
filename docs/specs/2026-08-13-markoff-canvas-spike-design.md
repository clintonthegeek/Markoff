# markoff-canvas spike — projection view leaf on QTextLayout

**Date:** 2026-08-13
**Status:** authorized, not started
**Decision record:** [`2026-08-13-view-authority-direction-decision.md`](2026-08-13-view-authority-direction-decision.md)
**Implementation plan (task sequence + session protocol):**
[`../plans/2026-08-13-markoff-canvas-spike.md`](../plans/2026-08-13-markoff-canvas-spike.md)
**Timebox:** ~3 weeks of working sessions from first commit. At expiry:
explicit user decision (extend once / archive), per decision record §6.
**Cites (invariant 1):**
`docs/handoff/2026-05-07-live-binding-developmental-history.md` (what
the live pipeline's machinery exists to work around — none of it may
be needed here; if it is, that's a finding),
`docs/specs/2026-05-22-cursor-authority-decision.md` (the chokepoint
contract this leaf must not need),
`docs/specs/2026-05-21-audit-L7-ime-composition.md` (IME scenarios),
`docs/INVARIANTS.md`.

---

## 1. Premise under test

A view leaf that renders `MarkoffDocument` directly — one
`QTextLayout` per block, custom input pipeline, **no `QTextDocument`,
no QML `TextEdit`, no widget-private text model of any kind** — needs
none of the arbitration machinery the existing leaves accumulated:
no re-entrance guards, no zero-timer deferrals, no focus chokepoint,
no cursor round-trip translation, exactly one coordinate space.

The spike exists to falsify this. It is an experiment, not a product
increment: the deliverable is the verdict, the criteria table in §7
filled in, and (on pass) enough of a skeleton to cost the real leaf.

## 2. L4 authority decision (invariant 2, in writing, first)

**Model wins, totally.** The view holds **zero** editable text state.

- Text authority: `MarkoffDocument` per-block CRDT buffers. The
  view's `QTextLayout` objects are a derived cache keyed by
  `(BlockId, per-block edit sequence)`; a stale entry is relaid out
  from `blockText()`, never patched in place from view-side state.
- Cursor/selection authority: a single `CanvasCursor` value —
  `{BlockId block, int byteOffset}` for the caret, plus an optional
  `{BlockId, int}` anchor for selection. Owned by the view leaf's one
  controller object. There is no second cursor to reconcile with:
  `QTextLayout` has no cursor, it only answers
  `lineForTextPosition`/`cursorToX` queries at paint/hit-test time.
- Undo authority: `UndoLog`, via the existing `Cmd::*` paths. The
  view never records undo state.
- Kind authority: the document (`Cmd::changeKind` via the same
  prefix-rule inference the other leaves use — reuse
  `KindTransition::inferBlockKind`, called from the leaf's
  `d2DocumentChanged` handler exactly as markoff-live does).

**Retiring authority (invariant 3):** none inside the spike — the
spike is additive (a fourth leaf beside the three canonical ones) and
retires nothing. The contingent retirement (markoff-live, possibly
markoff-styled) is named in the decision record §5.3 and is written
as its own plan only on a pass verdict, inside the D5 design.

## 3. Architecture sketch

```
Markoff::Canvas::View : QAbstractScrollArea
 ├─ BlockLayoutCache      QTextLayout per block, keyed (BlockId, seq);
 │                        lazy: layout only viewport ± margin, estimated
 │                        heights elsewhere, scrollbar corrected on
 │                        realization
 ├─ CanvasCursor          {block, byteOffset} caret + optional anchor
 ├─ CanvasInputRouter     keyPressEvent → StructuralKeyHandler (reused,
 │                        pure) or d2ApplyBufferEdit for printables;
 │                        inputMethodEvent/inputMethodQuery for IME;
 │                        mouse → hit-test → caret/selection
 └─ paintEvent            walk visible blocks, layout.draw(); caret via
                          drawCursor(); selection via FormatRange;
                          inline spans via QTextLayout::setFormats()
                          from inlineSpansFor(id)
```

Data flow is one-directional: input → `Cmd::*` /
`d2ApplyBufferEdit` / `applyFlatEdit`-free (see §4) → document emits
`d2DocumentChanged` (debounced) + targeted block signals → leaf
invalidates affected cache entries + repositions cursor → repaint.
There is no path by which the view writes view-side text state that
later flows back into the document — that path not existing is the
whole point.

Reused as-is from the existing stack: `StructuralKeyHandler`,
`KindTransition`, `Cmd::*`/`UndoLog`, `Theme`,
`inlineSpansFor` + `InlineParseCache`, `MarkdownView` base contract
(as far as practical; full contract-v2 conformance is **not** an exit
criterion — see §5).

## 4. Coordinate-space rule

One space: **UTF-8 byte offsets within a block**, the CRDT's native
space. `QTextLayout` works in UTF-16 `QChar` indices; the leaf owns
exactly one conversion helper (byte↔QChar within a single block's
text, the moral equivalent of `Coordinates::qtPosToByte`) and it is
used only at the layout boundary (hit-testing, caret x-position,
setFormats ranges). **No global/flat coordinate space is introduced
or used** — no `flatView()`, no `widgetFlatView()`, no
`applyFlatEdit`. Cross-block operations (merge, cross-block selection
delete, paste) go through `Cmd::*` / direct merge primitives with
`(BlockId, byte)` pairs. Any commit that adds a summed-across-blocks
byte offset is a spike finding to log, and a strong fail signal
(it's the Discipline Log's most-repeated bug class).

## 5. Scope

**In scope** (what the exit criteria exercise): paragraphs, headings,
list items (marker painted as decoration, content-only buffer, same
convention as the other leaves), code blocks (monospace, no token
highlighting), inline emphasis/strong/code rendering with
cursor-aware delimiter visibility for emphasis/strong, a minimal
read-write table (grid of per-cell layouts, caret enters cell, typing
edits the cell; **no** row/col ops, alignment, or resize), light/dark
`Theme`, wheel + keyboard scrolling.

**Out of scope, explicitly** (listing per invariant-2 discipline so
their absence is a decision, not an oversight): accessibility
(`QAccessibleTextInterface` — a pass verdict must record it as a
known cost of the real leaf, order weeks; whether it's required is a
user decision at that point), math/mermaid/embeds/footnotes/
frontmatter rendering, find integration, full `MarkdownView`
contract-v2 conformance, drag-drop, middle-click paste, kinetic/
smooth scrolling polish, printing, session/multi-view.

## 6. Constitutional constraints (violating any one = premise falsified)

These are grep-gated by a script committed with the spike
(`libs/markoff-canvas/tests/check-constitution.sh`), run in CI with
the leaf's tests. The gate scans `libs/markoff-canvas/` only.

- **C1 — zero re-entrance guards.** No member or local whose role is
  "suppress reaction to our own write" (`m_applying*`, `isApplying*`,
  boolean set-around-call patterns). Grep pattern plus honest
  review — renaming the guard doesn't dodge the constraint.
- **C2 — zero deferrals.** No `QTimer::singleShot(0, …)`,
  `Qt.callLater`, `QMetaObject::invokeMethod(..., QueuedConnection)`
  used to escape an ordering problem. (Consuming the document's
  existing debounced `d2DocumentChanged` is fine — that debounce is
  the document's, already on file; adding a *view-side* defer is not.)
- **C3 — zero QTextDocument / QML text instances.** The leaf links
  no `QTextDocument`, `QPlainTextEdit`, `QTextEdit`, or Quick text
  types.
- **C4 — one coordinate space** per §4: no cross-block byte
  arithmetic, no `applyFlatEdit`/`flatView` callers.

If satisfying an exit criterion appears to *require* violating C1–C4,
stop, write the finding into this spec's §9, and take the fail exit.
That is a successful spike — it answered the question.

## 7. Exit criteria

Every functional criterion is a QtTest slot on the **production
widget** via real events (`QTest::keyClick`/`mouseClick` on the
window — invariant 5: production callsite, not a synonym), running
offscreen via `scripts/run-tests.sh`. Every slot must be **proven
falsifiable** (invariant 4): break the target seam in a throwaway
stub commit, watch the test fail, revert. Record the falsification
commit SHA in the table on completion.

| # | Criterion | Falsifiable test |
|---|---|---|
| E1 | Typing printable chars at an arbitrary caret position updates `blockText()` and advances the caret; rendered text matches the buffer after each keystroke. | `typing_updates_buffer_and_caret` |
| E2 | Enter mid-block splits the block; caret lands at byte 0 of the new block. Backspace at byte 0 merges with the previous block; caret lands at the join point. Block count and content verified via `iterateBlocks()`. | `enter_splits_backspace_merges_caret_at_join` |
| E3 | Undo after E2's split restores the merged block **and** the caret references a block that exists (never a vanished `BlockId`); redo likewise. This is the queue-#10 invariant, natively. | `undo_redo_never_strand_caret` |
| E4 | Cross-block drag selection with real mouse events works in **both directions** (the 2026-05-21 asymmetry class); Ctrl+C then puts all selected blocks' text on the clipboard; a printable key on the selection collapses it and inserts at the first corner. | `drag_selection_both_directions_copy_and_collapse` |
| E5 | Kind transition: typing `# ` at byte 0 of a paragraph promotes it to Heading without losing focus or caret; the block re-renders in heading style. | `hash_space_promotes_heading_caret_survives` |
| E6 | IME: the five audit-L7 scenarios (commit-after-preedit, preedit-replace-commit, cancelled composition, commit into non-empty block, lifecycle probe) pass via `QInputMethodEvent`, with preedit visibly rendered (asserted via the layout's format ranges, not a screenshot). | `ime_l7_scenarios` (5 slots) |
| E7 | Delimiter visibility: `**bold**` renders with delimiters hidden and content styled when the caret is outside the span; moving the caret into the span reveals the delimiters; editing while revealed round-trips correctly. | `delimiter_visibility_follows_caret` |
| E8 | Minimal table: caret can enter a cell by mouse, typing edits that cell's buffer only, adjacent cells and the following block are unaffected, and no crash on cell edit + repaint (the QML-Repeater UAF class must have no analogue). | `table_cell_edit_isolated` |
| E9 | **Perf** (offscreen, release build, `-j 4` build cap per standing rule): on a 500-block synthetic document — load-to-first-paint < 500 ms; p95 keystroke→paint < 16 ms over a 200-keystroke run mid-document; scroll through the full document without layout of all blocks (assert the cache realized < 30% of blocks); RSS delta for the widget < 100 MB. Bench binary modeled on `tst_live_render_table_typing_perf`. | `tst_canvas_perf_500` |
| E10 | **Constitution**: `check-constitution.sh` passes on the final spike tree (C1–C4). | grep gate in CI |

Pass = all ten. Anything less at timebox = the §6/§8 fail-or-extend
path; there is no partial credit that quietly becomes "good enough."

## 8. Execution shape

1. **Week 1:** skeleton — `View` + `BlockLayoutCache` + paint +
   caret + hit-test; E1, E2 green (with falsification proofs).
2. **Week 2:** selection + clipboard + undo + kind transitions +
   inline spans; E3, E4, E5, E7.
3. **Week 3:** IME, table, perf harness; E6, E8, E9, E10; fill in
   §9; verdict.

Per-criterion commits, tests-first where the seam allows (invariant
4's break-the-stub proof makes "first" meaningful even when the
widget must exist for the test to compile). The leaf lives at
`libs/markoff-canvas/` with its own `CLAUDE.md` stub pointing here.
No changes to the other leaves except additive test fixtures; the
standstill rule from the decision record §5.2 applies to the rest of
the tree.

## 9. Findings log (fill during the spike)

> Append findings here as they surface: constraints that bit,
> Qt-layer surprises (QTextLayout quirks, IME event ordering),
> anything that changes the cost estimate for the real leaf. On the
> fail path this section *is* the deliverable.

- *(empty — spike not started)*

## 10. Verdict (fill at close)

- **Result:** —
- **Criteria table with falsification SHAs:** —
- **Recommendation to the D5 design:** —
